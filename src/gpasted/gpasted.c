/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-daemon.h"

#include <glib/gi18n-lib.h>
#include <gpaste.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static GMainLoop *main_loop;

static void
signal_handler (int signum)
{
    g_print (_("Signal %d received, exiting\n"), signum);
    g_main_loop_quit (main_loop);
}

static void
rebind (GPasteSettings *settings G_GNUC_UNUSED,
        const char     *binding,
        gpointer        user_data)
{
    g_paste_keybinder_rebind (G_PASTE_KEYBINDER (user_data), binding);
}

static void
reexec (GPasteDaemon *g_paste_daemon G_GNUC_UNUSED,
        gpointer      user_data G_GNUC_UNUSED)
{
    g_main_loop_quit (main_loop);
    execl (PKGLIBEXECDIR "/gpasted", "gpasted", NULL);
}

static void
error_exit (const gchar *error)
{
    fprintf (stderr, "%s\n", error);
    g_main_loop_quit (main_loop);
    exit (EXIT_FAILURE);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const char      *name G_GNUC_UNUSED,
                 gpointer         user_data)
{
    GError *error = NULL;

    g_paste_daemon_register_object (G_PASTE_DAEMON (user_data),
                                    connection,
                                    "/org/gnome/GPaste",
                                   &error);

    if (error != NULL)
    {
        g_error_free (error);
        error_exit (_("Could not register DBus service."));
    }
}

static void
on_name_lost (GDBusConnection *connection G_GNUC_UNUSED,
              const char      *name G_GNUC_UNUSED,
              gpointer         user_data G_GNUC_UNUSED)
{
    error_exit (_("Could not aquire DBus name."));
}

int
main (int argc, char *argv[])
{
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    g_type_init ();
    gtk_init (&argc, &argv);

    GPasteSettings *settings = g_paste_settings_new ();
    GPasteKeybinder *keybinder = g_paste_keybinder_new (g_paste_settings_get_keyboard_shortcut (settings));
    GPasteHistory *history = g_paste_history_new (settings);
    GPasteClipboardsManager *clipboards_manager = g_paste_clipboards_manager_new (history, settings);
    GPasteDaemon *g_paste_daemon = g_paste_daemon_new (history, settings, clipboards_manager, keybinder);
    GPasteClipboard *clipboard = g_paste_clipboard_new (GDK_SELECTION_CLIPBOARD, settings);
    GPasteClipboard *primary = g_paste_clipboard_new (GDK_SELECTION_PRIMARY, settings);

    g_signal_connect (G_OBJECT (settings),
                      "rebind",
                      G_CALLBACK (rebind),
                      keybinder);
    g_signal_connect (G_OBJECT (g_paste_daemon),
                      "reexecute-self",
                      G_CALLBACK (reexec),
                      NULL); /* user_data */

    g_paste_history_load (history);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, clipboard);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, primary);
    g_paste_clipboards_manager_activate (clipboards_manager);

    g_object_unref (settings);
    g_object_unref (history);
    g_object_unref (clipboards_manager);
    g_object_unref (clipboard);
    g_object_unref (primary);

    signal (SIGTERM, &signal_handler);
    signal (SIGINT, &signal_handler);

    main_loop = g_main_loop_new (NULL, FALSE);

    guint owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                     "org.gnome.GPaste",
                                     G_BUS_NAME_OWNER_FLAGS_NONE,
                                     on_bus_acquired, 
                                     NULL, /* on_name_acquired */
                                     on_name_lost,
                                     g_object_ref (g_paste_daemon),
                                     g_object_unref);

    g_main_loop_run (main_loop);

    g_paste_keybinder_unbind (keybinder);
    g_object_unref (keybinder);
    g_object_unref (g_paste_daemon);
    g_bus_unown_name (owner_id);
    g_main_loop_unref (main_loop);

    return EXIT_SUCCESS;
}
