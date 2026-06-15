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

typedef struct
{
    GPasteDaemon *daemon;
    GApplication *app;
} Usr1Data;

static gboolean
usr1_handler (gpointer user_data)
{
    Usr1Data *data = user_data;

    reexec (data->daemon, data->app);

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

gint
main (gint argc, gchar *argv[])
{
    /* FIXME: remove this once gtk supports clipboard correctly on wayland */
    gdk_set_allowed_backends ("x11");

    G_PASTE_GTK_INIT_APPLICATION ("Daemon");

    /* Keep the gapplication around */
    g_application_hold (gapp);

    /* Get the history store ready (backend choice + encrypted-history unlock)
     * before the daemon starts persisting anything. */
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_paste_storage_migration_prepare (app, settings);

    g_autoptr (GPasteDaemon) g_paste_daemon = g_paste_daemon_new_gdk (settings);
    g_autoptr (GPasteBusObject) search_provider = g_paste_search_provider_new ();
    g_autoptr (GPasteBus) bus = g_paste_bus_new ();

    /* The bus registers these once the name is acquired and owns them afterwards. */
    g_paste_bus_add_object (bus, G_PASTE_BUS_OBJECT (g_paste_daemon));
    g_paste_bus_add_object (bus, search_provider);

    guint64 c_signals[C_LAST_SIGNAL] = {
        [C_NAME_LOST] = g_signal_connect (bus,
                                          "name-lost",
                                          G_CALLBACK (on_name_lost),
                                          gapp),
        [C_REEXECUTE_SELF] = g_signal_connect (g_paste_daemon,
                                               "reexecute-self",
                                               G_CALLBACK (reexec),
                                               gapp)
    };

#ifdef G_OS_UNIX
    g_source_set_name_by_id (g_unix_signal_add (SIGTERM, signal_handler, app), "[GPaste] SIGTERM listener");
    g_source_set_name_by_id (g_unix_signal_add (SIGINT,  signal_handler, app), "[GPaste] SIGINT listener");

    Usr1Data usr1_data = { g_paste_daemon, gapp };

    g_source_set_name_by_id (g_unix_signal_add (SIGUSR1, usr1_handler, &usr1_data), "[GPaste] SIGUSR1 listener");
#endif

    g_paste_bus_own_name (bus);

    g_paste_util_write_pid_file ("Daemon");

    gint64 exit_status = g_application_run (gapp, argc, argv);

    g_signal_handler_disconnect (bus, c_signals[C_NAME_LOST]);
    g_signal_handler_disconnect (g_paste_daemon, c_signals[C_REEXECUTE_SELF]);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Wipe the master passphrase from secure memory before exiting. */
    g_paste_storage_backend_set_passphrase (NULL);
#endif

    return exit_status;
}
