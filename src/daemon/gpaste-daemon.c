/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk3/gpaste-gtk-macros.h>

#include <gpaste-bus.h>
#include <gpaste-daemon.h>
#include <gpaste-search-provider.h>

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

typedef struct
{
    GPasteBus        *bus;
    GPasteDaemon     *daemon;
    GPasteBusObject **search_provider;
    GApplication     *gapp;
} CallbackData;

static void
register_bus_object (GPasteBus       *bus,
                     GPasteBusObject *object,
                     GApplication    *gapp)
{
    g_autoptr (GError) error = NULL;

    if (!g_paste_bus_object_register_on_connection (object, g_paste_bus_get_connection (bus), &error))
        on_name_lost (bus, gapp);
}

static gboolean
register_search_provider (gpointer user_data)
{
    CallbackData *data = user_data;
    GPasteBusObject *search_provider = *(data->search_provider) = g_paste_search_provider_new ();

    register_bus_object (data->bus, search_provider, data->gapp);

    return G_SOURCE_REMOVE;
}

static void
on_bus_acquired (GPasteBus *bus,
                 gpointer   user_data)
{
    CallbackData *data = user_data;

    register_bus_object (bus, G_PASTE_BUS_OBJECT (data->daemon), data->gapp);

    g_source_set_name_by_id (g_idle_add (register_search_provider, user_data), "[GPaste] register_search_provider");
}

gint
main (gint argc, gchar *argv[])
{
    /* FIXME: remove this once gtk supports clipboard correctly on wayland */
    gdk_set_allowed_backends ("x11");

    G_PASTE_GTK_INIT_APPLICATION ("Daemon");

    /* Keep the gapplication around */
    g_application_hold (gapp);

    g_autofree CallbackData *data = g_new0 (CallbackData, 1);
    g_autoptr (GPasteDaemon) g_paste_daemon = data->daemon = g_paste_daemon_new ();
    g_autoptr (GPasteBusObject) search_provider = NULL;

    data->search_provider = &search_provider;
    data->gapp = gapp;

    g_autoptr (GPasteBus) bus = data->bus = g_paste_bus_new (on_bus_acquired, data);

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

    return exit_status;
}
