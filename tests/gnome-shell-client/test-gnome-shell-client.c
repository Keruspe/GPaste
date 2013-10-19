/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-gnome-shell-client.h>

#include <stdio.h>
#include <stdlib.h>

#define EXIT_TEST_SKIP 77

typedef struct
{
    const gchar *accelerator;
    guint32      action;
} Accelerator;

static void
on_accelerator_activated (GPasteGnomeShellClient *client G_GNUC_UNUSED,
                          guint32                 action,
                          guint32                 deviceid,
                          guint32                 timestamp,
                          gpointer                user_data)
{
    Accelerator *accels = user_data;

    g_print ("Recieved action %u, deviceid %u, timestamp %u, was ", action, deviceid, timestamp);
    for (guint i = 0; i < 3; ++i)
    {
        if (accels[i].action == action)
        {
            g_print ("%s\n", accels[i].accelerator);
            return;
        }
    }
    g_print ("not a known accelerator\n");
}

static gboolean
kill_loop (gpointer user_data)
{
    g_main_loop_quit (user_data);
    return G_SOURCE_REMOVE;
}

gint
main (gint argc, gchar *argv[])
{
    if (argc != 2 || g_strcmp0 (argv[1], "--dont-skip"))
        return EXIT_TEST_SKIP;

    g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

    GError *error = NULL;
    G_PASTE_CLEANUP_UNREF GPasteGnomeShellClient *client = g_paste_gnome_shell_client_new (&error);
    if (!client)
    {
        g_error ("Couldn't connect to gnome-shell: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    } 

    g_print ("gnome-shell mode: %s\n", g_paste_gnome_shell_client_get_mode (client));
    g_print ("gnome-shell version: %s\n", g_paste_gnome_shell_client_get_shell_version (client));
    
    g_print ("Will now play with overview's state, don't worry.\n");
    g_usleep (1000000);
    gboolean expected = FALSE;
    for (guint i = 0; i < 3; ++i)
    {
        gboolean active = g_paste_gnome_shell_client_overview_is_active (client);
        g_print ("Overview is %sactive (should be %sactive)\n", active ? "" : "in", expected ? "" : "in");
        if (i != 2)
        {
            expected = !expected;
            if (!g_paste_gnome_shell_client_overview_set_active (client, expected, &error))
            {
                g_error ("Couldn't set gnome-shell overview state: %s", error->message);
                g_error_free (error);
                return EXIT_FAILURE;
            }
            g_usleep (1000000);
        }
    }

    gchar *result = NULL;

    if (!g_paste_gnome_shell_client_eval (client, "3 + 2", &result, &error))
    {
        g_error ("Failed to eval \"3 + 2\": %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }
    g_print ("Evaluated \"3 + 2\" as \"%s\"\n", result);

    if (g_paste_gnome_shell_client_eval (client, "foobar", &result, &error))
    {
        g_error ("Did not fail to eval \"fobar\"");
        return EXIT_FAILURE;
    }
    if (error)
    {
        g_error ("Error when trying to evaluate \"foobar\": %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }
    g_print ("As expected, couldn't eval \"foobar\"\n");
    g_clear_error (&error);

    g_print ("Should now focus search\n");
    g_usleep (1000000);
    g_paste_gnome_shell_client_focus_search (client, &error);
    if (error)
    {
        g_error ("Couldn't focus search: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }
    g_usleep (1500000);

    if (!g_paste_gnome_shell_client_overview_set_active (client, expected, &error))
    {
        g_error ("Couldn't set gnome-shell overview state: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }

    g_print ("Now testing OSD\n");
    g_usleep (1000000);
    g_paste_gnome_shell_client_show_osd (client, "gtk-paste", "Test GPaste OSD", 80, &error);
    if (error)
    {
        g_error ("Couldn't show OSD: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }

    g_print ("Should now focus firefox\n");
    g_usleep (3000000);
    g_paste_gnome_shell_client_focus_app (client, "firefox.desktop", &error);
    if (error)
    {
        g_error ("Couldn't focus firefox: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }
    g_usleep (3000000);

    if (!g_paste_gnome_shell_client_overview_set_active (client, expected, &error))
    {
        g_error ("Couldn't set gnome-shell overview state: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }

    g_print ("Should now show applications\n");
    g_usleep (1000000);
    g_paste_gnome_shell_client_show_applications (client, &error);
    if (error)
    {
        g_error ("Couldn't show applications: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }
    g_usleep (3000000);

    if (!g_paste_gnome_shell_client_overview_set_active (client, expected, &error))
    {
        g_error ("Couldn't set gnome-shell overview state: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }
    g_usleep (1000000);

    Accelerator accels[3] = {
        { "<Ctrl><Alt>D",  0 },
        { "<Super>F", 0 },
        { "<Super><Alt>G", 0 }
    };
    GPasteGnomeShellAccelerator gs_accels[3];
    GPasteGnomeShellAccelerator gs_accel = {
        accels[2].accelerator,
        G_PASTE_GNOME_SHELL_KEYBINDING_MODE_ALL
    };

    for (guint i = 0; i < 2; ++i)
    {
        gs_accels[i].accelerator = accels[i].accelerator;
        gs_accels[i].flags = G_PASTE_GNOME_SHELL_KEYBINDING_MODE_ALL;
    }
    gs_accels[2].accelerator = NULL;
    guint signal_id = g_signal_connect (client, "accelerator-activated", G_CALLBACK (on_accelerator_activated), accels);

    g_print ("Now testing KeyGrabber\n");
    guint32 *actions = g_paste_gnome_shell_client_grab_accelerators (client, gs_accels, &error);
    for (guint i = 0; i < 2; ++i)
        accels[i].action = actions[i];
    g_free (actions);
    if (error)
    {
        g_error ("Couldn't grab accelerators: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }

    accels[2].action = g_paste_gnome_shell_client_grab_accelerator (client, gs_accel, &error);
    if (error)
    {
        g_error ("Couldn't grab accelerator: %s", error->message);
        g_error_free (error);
        return EXIT_FAILURE;
    }

    g_print ("Now should recognize <Ctrl><Alt>D, <Super>F and <Super><Alt>G for 10 secondes.\n");
    G_PASTE_CLEANUP_LOOP_UNREF GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    g_timeout_add_seconds (10, kill_loop, loop);
    g_main_loop_run (loop);
    for (guint i = 0; i < 2; ++i)
    {
        g_paste_gnome_shell_client_ungrab_accelerator (client, accels[i].action, &error);
        accels[i].action = 0;
        if (error)
        {
            g_error ("Couldn't ungrab accelerator: %s", error->message);
            g_error_free (error);
            return EXIT_FAILURE;
        }
    }

    g_print ("Now should no longer recognize keybindings for 3 secondes.\n");
    g_usleep (3000000);

    // TODO: grab accelerator

    g_signal_handler_disconnect (client, signal_id);

    return EXIT_SUCCESS;
}
