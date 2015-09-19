/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gpaste-bus.h>
#include <gpaste-daemon.h>
#include <gpaste-search-provider.h>

static GApplication *_app;

enum
{
    C_NAME_LOST,
    C_REEXECUTE_SELF,

    C_LAST_SIGNAL
};

static void
signal_handler (gint32 signum)
{
    g_print (_("Signal %d received, exiting\n"), signum);
    g_application_quit (_app);
}

G_PASTE_NORETURN static void
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

    g_idle_add (register_search_provider, user_data);
}

static void
reexec (GPasteDaemon *g_paste_daemon G_GNUC_UNUSED,
        gpointer      user_data)
{
    GApplication *app = user_data;

    g_application_quit (app);
    execl (PKGLIBEXECDIR "/gpaste-daemon", "gpaste-daemon", NULL);
}

gint
main (gint argc, gchar *argv[])
{
    /* FIXME: remove this once gtk supports clipboard correctly on wayland */
    gdk_set_allowed_backends ("x11");

    G_PASTE_INIT_APPLICATION ("Daemon");

    /* Keep the gapplication around */
    g_application_hold (gapp);

    g_autofree CallbackData *data = g_new0 (CallbackData, 1);
    g_autoptr (GPasteDaemon) g_paste_daemon = data->daemon = g_paste_daemon_new ();
    g_autoptr (GPasteBusObject) search_provider = NULL;

    data->search_provider = &search_provider;
    _app = data->gapp = gapp;

    g_autoptr (GPasteBus) bus = data->bus = g_paste_bus_new (on_bus_acquired, data);

    gulong c_signals[C_LAST_SIGNAL] = {
        [C_NAME_LOST] = g_signal_connect (bus,
                                          "name-lost",
                                          G_CALLBACK (on_name_lost),
                                          gapp),
        [C_REEXECUTE_SELF] = g_signal_connect (g_paste_daemon,
                                               "reexecute-self",
                                               G_CALLBACK (reexec),
                                               gapp)
    };

    signal (SIGTERM, &signal_handler);
    signal (SIGINT, &signal_handler);

    g_paste_bus_own_name (bus);

    gint64 exit_status = g_application_run (gapp, argc, argv);

    g_signal_handler_disconnect (bus, c_signals[C_NAME_LOST]);
    g_signal_handler_disconnect (g_paste_daemon, c_signals[C_REEXECUTE_SELF]);

    return exit_status;
}
