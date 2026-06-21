// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-macros.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-daemon/gpaste-bus.h>
#include <gpaste-daemon/gpaste-daemon.h>
#include <gpaste-daemon/gpaste-search-provider.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>

#ifdef G_OS_UNIX
#  include <glib-unix.h>
#endif

static void
reexec (GPasteDaemon *g_paste_daemon G_GNUC_UNUSED,
        gpointer      user_data)
{
    GApplication *app = user_data;

    g_application_quit (app);

    execl (PKGLIBEXECDIR "/gpaste-daemon", "gpaste-daemon", NULL);
}

#ifdef G_OS_UNIX
static gboolean
signal_handler (gpointer user_data)
{
    GApplication *app = user_data;

    g_print ("%s\n", _("Stop signal received, exiting"));
    g_application_quit (app);

    return G_SOURCE_REMOVE;
}

static gboolean
usr1_handler (gpointer user_data)
{
    /* reexec ignores its first argument, so the application is all it needs. */
    reexec (NULL, user_data);

    return G_SOURCE_REMOVE;
}
#endif

enum
{
    C_NAME_LOST,
    C_REEXECUTE_SELF,

    C_LAST_SIGNAL
};

G_GNUC_NORETURN static void
on_name_lost (GPasteBus *bus G_GNUC_UNUSED,
              gpointer   user_data)
{
    GApplication *app = user_data;

    fprintf (stderr, "%s\n", _("Could not acquire DBus name."));
    g_application_quit (app);

    exit (EXIT_FAILURE);
}

typedef struct
{
    GApplication    *gapp;
    GPasteSettings  *settings;
    GPasteDaemon    *daemon;
    GPasteBusObject *search_provider;
    GPasteBus       *bus;
    guint64          c_signals[C_LAST_SIGNAL];
} DaemonContext;

/* Final step, once the backend choice is settled and any encrypted history is
 * unlocked: build the daemon and expose it on the bus. */
static void
on_storage_ready (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    ctx->daemon = g_paste_daemon_new_gdk (ctx->settings);
    ctx->search_provider = g_paste_search_provider_new ();
    ctx->bus = g_paste_bus_new ();

    /* The bus registers these once the name is acquired and owns them afterwards. */
    g_paste_bus_add_object (ctx->bus, G_PASTE_BUS_OBJECT (ctx->daemon));
    g_paste_bus_add_object (ctx->bus, ctx->search_provider);

    ctx->c_signals[C_NAME_LOST] = g_signal_connect (ctx->bus, "name-lost",
                                                    G_CALLBACK (on_name_lost), ctx->gapp);
    ctx->c_signals[C_REEXECUTE_SELF] = g_signal_connect (ctx->daemon, "reexecute-self",
                                                         G_CALLBACK (reexec), ctx->gapp);

    g_paste_bus_own_name (ctx->bus);

    g_paste_util_write_pid_file ("Daemon");
}

static void
on_migration_done (gpointer user_data)
{
    DaemonContext *ctx = user_data;

    /* Unlock an already-encrypted history (or a no-op) before it is loaded. */
    if (g_paste_storage_decryption_needed (ctx->settings))
        g_paste_storage_decryption_show (GTK_APPLICATION (ctx->gapp), ctx->settings, on_storage_ready, ctx);
    else
        on_storage_ready (ctx);
}

gint
main (gint argc, gchar *argv[])
{
    /* FIXME: remove this once gtk supports clipboard correctly on wayland */
    gdk_set_allowed_backends ("x11");

    G_PASTE_GTK_INIT_APPLICATION ("Daemon");

    /* Keep the gapplication around */
    g_application_hold (gapp);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    DaemonContext ctx = { .gapp = gapp, .settings = settings };

    /* Also reachable later through the "storage-migration" action. */
    g_paste_storage_migration_register_action (app, settings);

#ifdef G_OS_UNIX
    g_source_set_name_by_id (g_unix_signal_add (SIGTERM, signal_handler, gapp), "[GPaste] SIGTERM listener");
    g_source_set_name_by_id (g_unix_signal_add (SIGINT,  signal_handler, gapp), "[GPaste] SIGINT listener");
    g_source_set_name_by_id (g_unix_signal_add (SIGUSR1, usr1_handler,   gapp), "[GPaste] SIGUSR1 listener");
#endif

    /* Get the history store ready (backend choice + encrypted-history unlock)
     * before the daemon starts persisting anything. Registering the application
     * above already initialised libadwaita (through AdwApplication's startup), so
     * any dialog can be shown right away and is processed once g_application_run()
     * spins the loop — no nested loop of our own. The chain settles the backend,
     * unlocks an encrypted history, then builds the daemon (on_storage_ready). */
    if (g_paste_storage_migration_needed (settings))
        g_paste_storage_migration_show (app, settings, on_migration_done, &ctx);
    else
        on_migration_done (&ctx);

    gint64 exit_status = g_application_run (gapp, argc, argv);

    if (ctx.bus)
        g_signal_handler_disconnect (ctx.bus, ctx.c_signals[C_NAME_LOST]);
    if (ctx.daemon)
        g_signal_handler_disconnect (ctx.daemon, ctx.c_signals[C_REEXECUTE_SELF]);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Wipe the master passphrase from secure memory before exiting. */
    g_paste_storage_backend_set_passphrase (NULL);
#endif

    g_clear_object (&ctx.search_provider);
    g_clear_object (&ctx.daemon);
    g_clear_object (&ctx.bus);

    return exit_status;
}
