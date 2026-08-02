// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-clipboard-gdk.h"

#include <gpaste-gtk4/gpaste-gtk-macros.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-daemon/gpaste-bus.h>
#include <gpaste-daemon/gpaste-daemon.h>
#include <gpaste-daemon/gpaste-search-provider.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>

#include <errno.h>
#include <unistd.h>

#ifdef G_OS_UNIX
#  include <glib-unix.h>
#endif

enum
{
    C_EXPORT_FAILED,
    C_NAME_ACQUIRED,
    C_NAME_LOST,
    C_REEXECUTE_SELF,
    C_CHANGE_PASSPHRASE,

    C_LAST_SIGNAL
};

typedef struct
{
    GApplication    *gapp;
    GtkApplication  *app;
    GPasteSettings  *settings;
    GPasteDaemon    *daemon;
    GPasteBusObject *search_provider;
    GPasteBus       *bus;
    guint64          c_signals[C_LAST_SIGNAL];

    /* A passphrase change is showing its prompts. ChangePassphrase can be called
     * again at any time (the client and the preferences both expose it), and a
     * second run would capture the passphrase the first one is about to replace:
     * it would then fail against the histories that first one re-keyed, on top of
     * stacking a second prompt over the first. */
    gboolean         changing_passphrase;
} DaemonContext;

/* Persist the history synchronously and release the storage lock so a successor
 * daemon can take over cleanly. Idempotent, so every exit path can call it. */
static void
flush_and_unlock (DaemonContext *ctx)
{
    if (ctx->daemon)
        g_paste_daemon_flush (ctx->daemon);

    g_paste_storage_backend_unlock ();
}

static void
reexec (GPasteDaemon *g_paste_daemon,
        gpointer      user_data G_GNUC_UNUSED)
{
    /* The clipboards manager was already stored by g_paste_daemon_reexecute();
     * make sure the history hits the disk too before we hand over to the new
     * process, which blocks on the storage lock until we release it. The lock is
     * left held: a successful exec releases it automatically (its fd is CLOEXEC),
     * so the successor waits until we are actually gone. */
    if (g_paste_daemon)
        g_paste_daemon_flush (g_paste_daemon);

    /* execl replaces this process on success and only returns on failure, so do
     * NOT quit the application first: a failed exec (e.g. the binary is missing)
     * must leave the current daemon running rather than exit into no daemon.
     *
     * Prefer the installed binary — correct after an upgrade, where the same path
     * now holds the new inode — then fall back to the exact binary currently
     * running, so a daemon started uninstalled from the build tree (whose install
     * path does not exist) still re-execs itself. */
    execl (PKGLIBEXECDIR "/gpaste-daemon", "gpaste-daemon", NULL);
    execl ("/proc/self/exe", "gpaste-daemon", NULL);

    /* Reached only if both execs failed: keep this daemon running and resume
     * recording (g_paste_daemon_flush() stopped it above). */
    g_warning ("%s: %s", _("Failed to reexecute the daemon"), g_strerror (errno));

    if (g_paste_daemon)
        g_paste_daemon_resume (g_paste_daemon);
}

#ifdef G_OS_UNIX
static gboolean
signal_handler (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    g_print ("%s\n", _("Stop signal received, exiting"));

    flush_and_unlock (ctx);
    g_application_quit (ctx->gapp);

    return G_SOURCE_REMOVE;
}

static gboolean
usr1_handler (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    /* reexec() takes the daemon to flush (NULL before it is built, which simply
     * skips the flush) and ignores its second, signal-closure argument. */
    reexec (ctx->daemon, ctx);

    /* Only reached when the exec failed and the daemon resumed: keep the
     * source so a later SIGUSR1 (e.g. once the binary is back in place) can
     * still trigger a re-exec. A successful exec replaces the process. */
    return G_SOURCE_CONTINUE;
}
#endif

/* The store on disk now speaks the new passphrase: rebuild the backend around it
 * (which also resumes the recording the flush below stopped) so the daemon keeps
 * persisting with the right key. */
static void
on_passphrase_changed (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    ctx->changing_passphrase = FALSE;

    if (ctx->daemon)
        g_paste_daemon_reload_storage (ctx->daemon);
}

/* Changing the passphrase rewrites every history on disk, so stop persisting
 * first and make sure what is in memory is already down there — then re-encrypt
 * it. We are the GTK application that shows the migration dialog, so the prompts
 * run right here rather than through the gpaste-storage helper. */
static gboolean
do_change_passphrase (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    g_paste_daemon_flush (ctx->daemon);
    g_paste_storage_rekey_show (ctx->app, ctx->settings, on_passphrase_changed, ctx);

    return G_SOURCE_REMOVE;
}

static void
change_passphrase (GPasteDaemon *g_paste_daemon G_GNUC_UNUSED,
                   gpointer      user_data)
{
    DaemonContext *ctx = user_data;

    /* Coalesce, as the gnome-shell host does: one change at a time. */
    if (ctx->changing_passphrase)
        return;

    ctx->changing_passphrase = TRUE;

    /* This fires from inside the ChangePassphrase D-Bus dispatch, and the work
     * below blocks: a flush writes the whole history, and unlocking derives an
     * Argon2 key or waits on the keyring. Doing it here would stall the main
     * loop — the caller's synchronous call included, until it times out on a
     * change that is merely slow. Defer, as the gnome-shell host does. */
    g_idle_add (do_change_passphrase, ctx);
}

/* Final step, once the name is owned, the backend choice is settled and any
 * encrypted history is unlocked: build the daemon and expose it on the bus. */
static void
on_storage_ready (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    /* The GDK backend is ours alone — the gnome-shell-hosted daemon drives the
     * mutter one instead — so the providers are built right here rather than
     * behind a convenience constructor in the daemon library. */
    g_autoptr (GPasteClipboardProvider) clipboard = g_paste_clipboard_gdk_new_clipboard (ctx->settings);
    g_autoptr (GPasteClipboardProvider) primary = g_paste_clipboard_gdk_new_primary (ctx->settings);

    ctx->daemon = g_paste_daemon_new (ctx->settings, clipboard, primary);
    ctx->search_provider = g_paste_search_provider_new ();

    ctx->c_signals[C_REEXECUTE_SELF] = g_signal_connect (ctx->daemon, "reexecute-self",
                                                         G_CALLBACK (reexec), ctx);
    ctx->c_signals[C_CHANGE_PASSPHRASE] = g_signal_connect (ctx->daemon, "change-passphrase",
                                                            G_CALLBACK (change_passphrase), ctx);

    /* The name is already owned, so the bus registers these immediately. */
    g_paste_bus_add_object (ctx->bus, G_PASTE_BUS_OBJECT (ctx->daemon));
    g_paste_bus_add_object (ctx->bus, ctx->search_provider);

    g_paste_util_write_pid_file ("Daemon");
}

static void
on_migration_done (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    /* Unlock an already-encrypted history (or a no-op) before it is loaded. */
    if (g_paste_storage_decryption_needed (ctx->settings))
        g_paste_storage_decryption_show (ctx->app, ctx->settings, on_storage_ready, ctx);
    else
        on_storage_ready (ctx);
}

/* The name is ours: only now do we run any migration / encrypted-history unlock
 * and build the daemon. Deferring this until acquisition means a daemon that is
 * about to fail to own the name (another one already holds it) never pops a
 * migration dialog or a passphrase prompt. */
static void
on_name_acquired (GPasteBus *bus G_GNUC_UNUSED,
                  gpointer   user_data)
{
    DaemonContext *ctx = user_data;

    /* Acquisition normally happens once; guard against a spurious re-entry. */
    if (ctx->daemon)
        return;

    /* Get the history store ready (backend choice + encrypted-history unlock)
     * before the daemon starts persisting anything. libadwaita was initialised by
     * the application registration, so any dialog shows right away and is
     * processed by the running main loop — no nested loop of our own. */
    if (g_paste_storage_migration_needed (ctx->settings))
        g_paste_storage_migration_show (ctx->app, ctx->settings, on_migration_done, ctx);
    else
        on_migration_done (ctx);
}

/* We may well own the name, but an object we cannot export is a daemon no client
 * can talk to: fail the startup rather than blaming a name conflict that never
 * happened (the bus already warned about the actual cause). */
static void
on_export_failed (GPasteBus *bus G_GNUC_UNUSED,
                  gpointer   user_data)
{
    DaemonContext *ctx = user_data;

    fprintf (stderr, "%s\n", _("Could not export our objects on the session bus, exiting"));

    flush_and_unlock (ctx);
    g_application_quit (ctx->gapp);
    exit (EXIT_FAILURE);
}

static void
on_name_lost (GPasteBus *bus       G_GNUC_UNUSED,
              gboolean   was_owned,
              gpointer   user_data)
{
    DaemonContext *ctx = user_data;

    if (was_owned)
    {
        /* A takeover (typically the gnome-shell extension, or `gpaste-daemon
         * --replace`): flush the history and release the lock so the successor
         * loads our final state, then exit successfully so a Type=dbus systemd
         * unit does not land in the failed state. */
        g_print ("%s\n", _("Replaced by another GPaste daemon, exiting"));
        flush_and_unlock (ctx);
        g_application_quit (ctx->gapp);
        return;
    }

    /* Never acquired: another owner holds the name and refused replacement (an
     * older daemon predating ALLOW_REPLACEMENT). This is a startup failure. */
    fprintf (stderr, "%s\n", _("Could not acquire DBus name. Is another GPaste daemon already running?"));
    g_application_quit (ctx->gapp);
    exit (EXIT_FAILURE);
}

/* Pull a bare "--replace" out of argv (shifting the rest down) so it never
 * reaches GApplication's option handling, and report whether it was present. */
static gboolean
extract_replace_arg (gint  *argc,
                     gchar *argv[])
{
    for (gint i = 1; i < *argc; ++i)
    {
        if (!g_paste_str_equal (argv[i], "--replace"))
            continue;

        for (gint j = i; j < *argc; ++j)
            argv[j] = argv[j + 1];
        --*argc;

        return TRUE;
    }

    return FALSE;
}

gint
main (gint argc, gchar *argv[])
{
    /* FIXME: remove this once gtk supports clipboard correctly on wayland */
    gdk_set_allowed_backends ("x11");

    gboolean replace = extract_replace_arg (&argc, argv);

    G_PASTE_GTK_INIT_APPLICATION ("Daemon");

    /* Keep the gapplication around */
    g_application_hold (gapp);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    DaemonContext ctx = { .gapp = gapp, .app = app, .settings = settings };

#ifdef G_OS_UNIX
    g_source_set_name_by_id (g_unix_signal_add (SIGTERM, signal_handler, &ctx), "[GPaste] SIGTERM listener");
    g_source_set_name_by_id (g_unix_signal_add (SIGINT,  signal_handler, &ctx), "[GPaste] SIGINT listener");
    g_source_set_name_by_id (g_unix_signal_add (SIGUSR1, usr1_handler,   &ctx), "[GPaste] SIGUSR1 listener");
#endif

    /* Own the name first, then build the daemon once it is acquired (see
     * on_name_acquired). Owning first lets us fail fast when another daemon
     * already holds the name, before doing any storage work, and lets a manual
     * `--replace` (or the gnome-shell extension) evict the current owner. */
    ctx.bus = g_paste_bus_new ();
    ctx.c_signals[C_EXPORT_FAILED] = g_signal_connect (ctx.bus, "export-failed",
                                                       G_CALLBACK (on_export_failed), &ctx);
    ctx.c_signals[C_NAME_ACQUIRED] = g_signal_connect (ctx.bus, "name-acquired",
                                                       G_CALLBACK (on_name_acquired), &ctx);
    ctx.c_signals[C_NAME_LOST] = g_signal_connect (ctx.bus, "name-lost",
                                                   G_CALLBACK (on_name_lost), &ctx);
    g_paste_bus_own_name_full (ctx.bus, replace);

    gint64 exit_status = g_application_run (gapp, argc, argv);

    /* Covers a plain quit (SIGTERM/SIGINT already flush, but a takeover exit and
     * any other path funnel through here too); all of it is idempotent. */
    flush_and_unlock (&ctx);

    if (ctx.bus)
    {
        g_signal_handler_disconnect (ctx.bus, ctx.c_signals[C_EXPORT_FAILED]);
        g_signal_handler_disconnect (ctx.bus, ctx.c_signals[C_NAME_ACQUIRED]);
        g_signal_handler_disconnect (ctx.bus, ctx.c_signals[C_NAME_LOST]);
    }
    if (ctx.daemon)
    {
        g_signal_handler_disconnect (ctx.daemon, ctx.c_signals[C_REEXECUTE_SELF]);
        g_signal_handler_disconnect (ctx.daemon, ctx.c_signals[C_CHANGE_PASSPHRASE]);
    }

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Wipe the master passphrase from secure memory before exiting. */
    g_paste_storage_backend_set_passphrase (NULL);
#endif

    g_clear_object (&ctx.search_provider);
    g_clear_object (&ctx.daemon);
    g_clear_object (&ctx.bus);

    return exit_status;
}
