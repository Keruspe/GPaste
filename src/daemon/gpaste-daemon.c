// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-clipboard-gdk.h"
#include "gpaste-prompt-adw.h"

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
    C_DOCTOR,

    C_LAST_SIGNAL
};

typedef struct
{
    GApplication    *gapp;
    GtkApplication  *app;
    GPastePrompt    *prompt;
    GPasteSettings  *settings;
    GPasteDaemon    *daemon;
    GPasteBusObject *search_provider;
    GPasteBus       *bus;
    gulong           c_signals[C_LAST_SIGNAL];

    /* A passphrase change is showing its prompts. ChangePassphrase can be called
     * again at any time (the client and the preferences both expose it), and a
     * second run would capture the passphrase the first one is about to replace:
     * it would then fail against the histories that first one re-keyed, on top of
     * stacking a second prompt over the first. */
    gboolean         changing_passphrase;

    /* A check of the store is running (Doctor), which re-keys histories the
     * same way: one or the other at a time, a change asked for during a check
     * owed until it is done. */
    gboolean         checking;
    gboolean         passphrase_change_owed;

    /* The startup storage settle (migration, then encrypted-history unlock) is
     * running. It can sit on a dialog for minutes, and the bus dropping and
     * being re-acquired meanwhile runs on_name_acquired() again -- which
     * ctx->daemon cannot guard against, since it is only assigned once the
     * settle is over. A second settle would raise its own dialogs over the store
     * the first is rewriting, then build a second daemon over the first. The
     * gnome-shell host gates the same window with _settleStorage(). */
    gboolean         settling;
    /* A stop signal or a takeover is waiting for the store to be idle
     * (stop_when_storage_idle ()): the settle it interrupts goes no further,
     * and another one changes nothing. */
    gboolean         stopping;
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

typedef void (*StorageIdleFunc) (DaemonContext *ctx);

typedef struct
{
    DaemonContext  *ctx;
    StorageIdleFunc then;
} IdleStorageWait;

static gboolean
on_storage_idle_poll (gpointer user_data)
{
    IdleStorageWait *wait = user_data;

    if (g_paste_storage_is_busy ())
        return G_SOURCE_CONTINUE;

    wait->then (wait->ctx);

    return G_SOURCE_REMOVE;
}

/* Run @then once no storage worker is rewriting the store
 * (g_paste_storage_is_busy ()): ending, exec'ing or releasing the lock under
 * one cuts its write short. Polled, the worker reporting to the library rather
 * than to us. No test shows it, the daemon's own main () being what it guards. */
static void
when_storage_idle (DaemonContext  *ctx,
                   StorageIdleFunc then)
{
    if (!g_paste_storage_is_busy ())
    {
        then (ctx);
        return;
    }

    IdleStorageWait *wait = g_new0 (IdleStorageWait, 1);

    wait->ctx = ctx;
    wait->then = then;
    g_source_set_name_by_id (g_timeout_add_full (G_PRIORITY_DEFAULT, 100, on_storage_idle_poll, wait, g_free),
                             "[GPaste] wait for the storage to be idle");
}

static void reexec_once_idle (DaemonContext *ctx);

static gboolean
reexec (GPasteDaemon *g_paste_daemon,
        gpointer      user_data)
{
    DaemonContext *ctx = user_data;

    /* A stop or a takeover waiting on the store (stop_when_storage_idle ()) is
     * this process ending, which an exec let through by the same wait would
     * outlive: the successor, or no daemon at all, is what was asked for. No
     * test shows it, for the reason when_storage_idle () gives. */
    if (ctx->stopping)
        return FALSE;

    /* As for a stop signal (when_storage_idle ()). Accepted all the same: the
     * exec follows once the write it would cut short is done -- a request over
     * the bus being refused before it gets here (g_paste_daemon_reexecute ()). */
    if (g_paste_storage_is_busy ())
    {
        when_storage_idle (ctx, reexec_once_idle);
        return TRUE;
    }

    GDBusConnection *connection = g_application_get_dbus_connection (ctx->gapp);
    g_autoptr (GError) error = NULL;

    /* GApplication and GPasteBus share GIO's session connection. A handler
     * returning only queues its reply; flush that queue before exec closes the
     * socket, including mutations that resolved the pending expiry wait. */
    if (connection && !g_dbus_connection_flush_sync (connection, NULL, &error))
    {
        g_warning ("Could not flush D-Bus replies before restarting: %s", error->message);
        return FALSE;
    }

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

    return FALSE;
}

/* Whatever the wait let through -- a password copied, an upload started -- is
 * what a re-exec waits on and cancels too, so it goes through the daemon again
 * rather than straight to the exec (g_paste_daemon_reexecute ()). */
static void
reexec_once_idle (DaemonContext *ctx)
{
    /* As in reexec (), before any countdown is brought forward for it. */
    if (ctx->stopping)
        return;

    if (ctx->daemon)
        g_paste_daemon_reexecute (ctx->daemon);
    else
        reexec (NULL, ctx);
}

static void
quit (DaemonContext *ctx)
{
    flush_and_unlock (ctx);
    g_application_quit (ctx->gapp);
}

/* Quit once the store is idle, and once: when_storage_idle () says why, and
 * why no test shows it. */
static void
stop_when_storage_idle (DaemonContext *ctx)
{
    if (ctx->stopping)
        return;

    ctx->stopping = TRUE;
    when_storage_idle (ctx, quit);
}

#ifdef G_OS_UNIX
static gboolean
signal_handler (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    g_print ("%s\n", _("Stop signal received, exiting"));
    stop_when_storage_idle (ctx);

    /* Kept: once its last source goes, GLib gives a signal its default action
     * back, and a second Ctrl+C while the store is being rewritten would kill
     * the rewrite the stop is waiting for. */
    return G_SOURCE_CONTINUE;
}

static gboolean
usr1_handler (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    /* Through the daemon while there is one, so the passwords its reads are
     * still classifying come off first (g_paste_daemon_reexecute()); it then
     * ends in reexec() through "reexecute-self" like the D-Bus method does.
     * Before it is built there is nothing to expire nor flush, and reexec()
     * skips the flush for a NULL daemon. */
    if (ctx->daemon)
        g_paste_daemon_reexecute (ctx->daemon);
    else
        when_storage_idle (ctx, reexec_once_idle);

    /* Either way the exec comes after this handler returns: the expiry runs
     * asynchronously, and the store may have a worker to wait for. Keep
     * listening if that request is refused or the exec fails. */
    return G_SOURCE_CONTINUE;
}
#endif

/* Rebuild the backend around whatever key the store on disk now speaks (which
 * also resumes the recording the flush below stopped) so the daemon keeps
 * persisting with the right one: the new passphrase where the re-key applied,
 * the old one where it failed and the histories were put back. */
static void
on_passphrase_changed (GObject      *source G_GNUC_UNUSED,
                       GAsyncResult *result,
                       gpointer      user_data)
{
    DaemonContext *ctx = user_data;
    g_autoptr (GError) error = NULL;

    if (!g_paste_storage_rekey_finish (result, &error))
        g_warning ("Could not change the passphrase: %s", error->message);

    ctx->changing_passphrase = FALSE;

    /* A daemon stood down meanwhile (on_name_lost ()) has nothing to reload. */
    if (ctx->daemon)
        g_paste_daemon_reload_storage (ctx->daemon);
}

/* Changing the passphrase rewrites every history on disk, so stop persisting
 * first and make sure what is in memory is already down there — then re-encrypt
 * it. We can put the prompts to the user ourselves (libadwaita), so the re-key
 * runs right here. */
static void
do_change_passphrase (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    g_paste_daemon_flush (ctx->daemon);
    g_paste_storage_rekey_async (ctx->prompt, ctx->settings, on_passphrase_changed, ctx);
}

static void
change_passphrase (GPasteDaemon *g_paste_daemon G_GNUC_UNUSED,
                   gpointer      user_data)
{
    DaemonContext *ctx = user_data;

    /* Coalesce, as the gnome-shell host does: one change at a time, and one
     * asked for during a check run once the check is done, the gnome-shell
     * host queueing it the same way. */
    if (ctx->changing_passphrase)
        return;
    if (ctx->checking)
    {
        ctx->passphrase_change_owed = TRUE;
        return;
    }

    ctx->changing_passphrase = TRUE;

    /* This fires from inside the ChangePassphrase D-Bus dispatch, and the work
     * below blocks: a flush writes the whole history, and unlocking derives an
     * Argon2 key or waits on the keyring. Doing it here would stall the main
     * loop — the caller's synchronous call included, until it times out on a
     * change that is merely slow. Defer, as the gnome-shell host does. */
    g_source_set_name_by_id (g_idle_add_once (do_change_passphrase, ctx), "[GPaste] passphrase change");
}

/* Hand what the check found to the callers waiting on it, once the backend is
 * rebuilt around the passphrase the store now speaks -- the one the check
 * unlocked it with, where it had to -- which also resumes the recording the
 * flush below stopped. */
static void
on_store_checked (GObject      *source G_GNUC_UNUSED,
                  GAsyncResult *result,
                  gpointer      user_data)
{
    DaemonContext *ctx = user_data;
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) report = NULL;
    gboolean healthy = FALSE;

    ctx->checking = FALSE;

    /* Only the prompt failing ends the check with an error, which then is the
     * one thing the report has to say. */
    if (!g_paste_storage_doctor_finish (result, &report, &healthy, &error))
        g_set_strv (&report, (const gchar * const []) { error->message, NULL });

    if (ctx->daemon)
    {
        g_paste_daemon_reload_storage (ctx->daemon);
        g_paste_daemon_doctor_done (ctx->daemon, (const gchar * const *) report, healthy);

        if (ctx->passphrase_change_owed)
        {
            ctx->passphrase_change_owed = FALSE;
            change_passphrase (ctx->daemon, ctx);
        }
    }
}

/* As do_change_passphrase (): the history down on disk first, then the check
 * over it. */
static void
do_check_store (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    g_paste_daemon_flush (ctx->daemon);
    g_paste_storage_doctor_async (ctx->prompt, ctx->settings, on_store_checked, ctx);
}

static void
check_store (GPasteDaemon *daemon,
             gpointer      user_data)
{
    DaemonContext *ctx = user_data;

    /* A change of passphrase is rewriting the store: it would be the check's
     * histories under its feet. A call made during a check is answered with it
     * by the daemon, and never gets here. */
    if (ctx->changing_passphrase)
    {
        const gchar *busy[] = { _("The passphrase is being changed: check again once that is done"), NULL };

        g_paste_daemon_doctor_done (daemon, busy, FALSE);
        return;
    }

    ctx->checking = TRUE;

    /* Deferred for the reason change_passphrase () is. */
    g_source_set_name_by_id (g_idle_add_once (do_check_store, ctx), "[GPaste] store check");
}

/* Final step, once the name is owned, the backend choice is settled and any
 * encrypted history is unlocked: build the daemon and expose it on the bus. */
static void
on_storage_ready (DaemonContext *ctx)
{
    ctx->settling = FALSE;

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
    ctx->c_signals[C_DOCTOR] = g_signal_connect (ctx->daemon, "doctor", G_CALLBACK (check_store), ctx);

    /* The name is already owned, so the bus registers these immediately. */
    g_paste_bus_add_object (ctx->bus, G_PASTE_BUS_OBJECT (ctx->daemon));
    g_paste_bus_add_object (ctx->bus, ctx->search_provider);

    g_paste_util_write_pid_file ("Daemon");
}

static void
on_decryption_done (GObject      *source G_GNUC_UNUSED,
                    GAsyncResult *result,
                    gpointer      user_data)
{
    DaemonContext *ctx = user_data;
    g_autoptr (GError) error = NULL;

    /* The history stays locked, which loads as unreadable and is never written
     * over, so there is still a daemon to build either way. */
    if (!g_paste_storage_decryption_finish (result, &error))
        g_warning ("Could not unlock the encrypted history: %s", error->message);

    if (!ctx->stopping)
        on_storage_ready (ctx);
}

static void
start_decryption (DaemonContext *ctx)
{
    /* Unlock an already-encrypted history (or a no-op) before it is loaded. */
    if (g_paste_storage_decryption_needed (ctx->settings))
        g_paste_storage_decryption_async (ctx->prompt, ctx->settings, on_decryption_done, ctx);
    else
        on_storage_ready (ctx);
}

static void
on_migration_done (GObject      *source G_GNUC_UNUSED,
                   GAsyncResult *result,
                   gpointer      user_data)
{
    DaemonContext *ctx = user_data;
    g_autoptr (GError) error = NULL;

    if (!g_paste_storage_migration_finish (result, &error))
        g_warning ("The storage migration prompt failed: %s", error->message);

    /* A daemon stopping, or replaced, while the import ran asks nothing more
     * and builds nothing: it is about to go. */
    if (!ctx->stopping)
        start_decryption (ctx);
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

    /* Acquisition normally happens once; guard against a spurious re-entry, and
     * against a re-acquisition landing here while the first settle is still
     * showing its dialogs (see ctx->settling). */
    if (ctx->daemon || ctx->settling)
        return;

    ctx->settling = TRUE;

    /* Get the history store ready (backend choice + encrypted-history unlock)
     * before the daemon starts persisting anything. libadwaita was initialised by
     * the application registration, so any dialog shows right away and is
     * processed by the running main loop — no nested loop of our own. */
    if (g_paste_storage_migration_needed (ctx->settings))
        g_paste_storage_migration_async (ctx->prompt, ctx->settings, on_migration_done, ctx);
    else
        start_decryption (ctx);
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
         * loads our final state, then exit successfully so the systemd unit
         * does not land in the failed state. */
        g_print ("%s\n", _("Replaced by another GPaste daemon, exiting"));

        /* Off the bus, its history flushed, now rather than once the store is
         * idle: a daemon a storage worker keeps around meanwhile, still reading
         * the selections and answering the shortcuts through its portal
         * session, would do both beside the one that took over -- putting its
         * own history back on a selection that one emptied. Disposed as the
         * in-shell host disposes the daemon it lets go of; only the lock, which
         * the worker still needs, waits for the store. No test shows it, for
         * the reason when_storage_idle () gives. */
        if (ctx->daemon)
        {
            g_paste_daemon_flush (ctx->daemon);
            g_signal_handler_disconnect (ctx->daemon, ctx->c_signals[C_REEXECUTE_SELF]);
            g_signal_handler_disconnect (ctx->daemon, ctx->c_signals[C_CHANGE_PASSPHRASE]);
            g_object_run_dispose (G_OBJECT (ctx->daemon));
            g_clear_object (&ctx->daemon);
        }

        stop_when_storage_idle (ctx);
        return;
    }

    /* Never acquired: another owner holds the name and refused replacement (an
     * older daemon predating ALLOW_REPLACEMENT). This is a startup failure. */
    fprintf (stderr, "%s\n", _("Could not acquire the D-Bus name. Is another GPaste daemon already running?"));
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
    /* GTK doesn't support global clipboard events on wayland */
    gdk_set_allowed_backends ("x11");

    gboolean replace = extract_replace_arg (&argc, argv);

    G_PASTE_GTK_INIT_APPLICATION ("Daemon");

    /* Nothing holds the application otherwise: the daemon has no window. */
    g_application_hold (gapp);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    /* The libadwaita prompt backend: how the storage layer reaches the user from
     * here. The gnome-shell-hosted daemon supplies its own instead. */
    g_autoptr (GPastePrompt) prompt = g_paste_prompt_adw_new (app);
    DaemonContext ctx = { .gapp = gapp, .app = app, .prompt = prompt, .settings = settings };

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
        g_signal_handler_disconnect (ctx.daemon, ctx.c_signals[C_DOCTOR]);
    }

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Wipe the master passphrase from secure memory before exiting. */
    g_paste_storage_backend_set_passphrase (NULL);
#endif

    /* g_bus_own_name () holds a reference on the bus until the name is unowned,
     * and the bus holds the objects it exports: only the unown lets the
     * references dropped below be the last ones, so that the daemon is disposed
     * -- its history, its clipboards and its portal session with it. Nothing
     * tests this executable's exit, which is why this says so here. */
    if (ctx.bus)
        g_paste_bus_unown_name (ctx.bus);

    g_clear_object (&ctx.search_provider);
    g_clear_object (&ctx.daemon);
    g_clear_object (&ctx.bus);

    return exit_status;
}
