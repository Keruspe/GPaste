/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gnome-shell-client.h>

#define EXIT_TEST_SKIP 77

typedef struct
{
    const gchar *accelerator;
    guint32      action;
} Accelerator;

static void
on_accelerator_activated (GPasteGnomeShellClient *client G_GNUC_UNUSED,
                          guint64                 action,
                          gpointer                user_data)
{
    Accelerator *accels = user_data;

    g_print ("Recieved action %lu, was ", action);
    for (guint64 i = 0; i < 3; ++i)
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
    if (argc != 2 || !g_paste_str_equal (argv[1], "--dont-skip"))
        return EXIT_TEST_SKIP;

    g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteGnomeShellClient) client = g_paste_gnome_shell_client_new_sync (&error);
    if (!client)
    {
        g_error ("Couldn't connect to gnome-shell: %s", error->message);
        return EXIT_FAILURE;
    } 

    Accelerator accels[3] = {
        { "<Ctrl><Alt>D",  0 },
        { "<Super>F", 0 },
        { "<Super><Alt>G", 0 }
    };
    GPasteGnomeShellAccelerator gs_accels[3];
    GPasteGnomeShellAccelerator gs_accel = G_PASTE_GNOME_SHELL_ACCELERATOR (accels[2].accelerator);

    for (guint64 i = 0; i < 2; ++i)
        gs_accels[i] = G_PASTE_GNOME_SHELL_ACCELERATOR (accels[i].accelerator);
    gs_accels[2].accelerator = NULL;
    guint64 signal_id = g_signal_connect (client, "accelerator-activated", G_CALLBACK (on_accelerator_activated), accels);

    g_print ("Now testing KeyGrabber\n");
    guint32 *actions = g_paste_gnome_shell_client_grab_accelerators_sync (client, gs_accels, &error);
    for (guint64 i = 0; i < 2; ++i)
        accels[i].action = actions[i];
    g_free (actions);
    if (error)
    {
        g_error ("Couldn't grab accelerators: %s", error->message);
        return EXIT_FAILURE;
    }

    accels[2].action = g_paste_gnome_shell_client_grab_accelerator_sync (client, gs_accel, &error);
    if (error)
    {
        g_error ("Couldn't grab accelerator: %s", error->message);
        return EXIT_FAILURE;
    }

    g_print ("Now should recognize <Ctrl><Alt>D, <Super>F and <Super><Alt>G for 10 secondes.\n");
    g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
    g_source_set_name_by_id (g_timeout_add_seconds (10, kill_loop, loop), "[GPaste] test loop");
    g_main_loop_run (loop);
    for (guint64 i = 0; i < 2; ++i)
    {
        g_paste_gnome_shell_client_ungrab_accelerator_sync (client, accels[i].action, &error);
        accels[i].action = 0;
        if (error)
        {
            g_error ("Couldn't ungrab accelerator: %s", error->message);
            return EXIT_FAILURE;
        }
    }

    g_print ("Now should no longer recognize keybindings for 3 secondes.\n");
    g_usleep (3000000);

    g_signal_handler_disconnect (client, signal_id);

    return EXIT_SUCCESS;
}
