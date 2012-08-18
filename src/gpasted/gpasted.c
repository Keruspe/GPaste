/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-paste-and-pop-keybinding.h"
#include "gpaste-show-history-keybinding.h"

#include <gpaste.h>
#include <gpaste-daemon.h>
#include <glib/gi18n-lib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#define ELEMENTSOF(foo) sizeof(foo)/sizeof(foo[0])

static GMainLoop *main_loop;

enum
{
    C_NAME_LOST,
    C_REEXECUTE_SELF,

    C_LAST_SIGNAL
};

static gulong c_signals[C_LAST_SIGNAL] = { 0 };

static void
signal_handler (int signum)
{
    g_print (_("Signal %d received, exiting\n"), signum);
    g_main_loop_quit (main_loop);
}

static void
on_name_lost (GPasteDaemon *g_paste_daemon G_GNUC_UNUSED,
              gpointer      user_data G_GNUC_UNUSED)
{
    fprintf (stderr, "%s\n", _("Could not acquire DBus name."));
    g_main_loop_quit (main_loop);
    exit (EXIT_FAILURE);
}

static void
reexec (GPasteDaemon *g_paste_daemon G_GNUC_UNUSED,
        gpointer      user_data G_GNUC_UNUSED)
{
    g_main_loop_quit (main_loop);
    execl (PKGLIBEXECDIR "/gpasted", "gpasted", NULL);
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
    GPasteHistory *history = g_paste_history_new (settings);
    GPasteClipboardsManager *clipboards_manager = g_paste_clipboards_manager_new (history, settings);
    GPasteXcbWrapper *xcb_wrapper = g_paste_xcb_wrapper_new ();
    GPasteKeybinder *keybinder = g_paste_keybinder_new (xcb_wrapper);
    GPasteDaemon *g_paste_daemon = g_paste_daemon_new (history, settings, clipboards_manager, keybinder);
    GPasteClipboard *clipboard = g_paste_clipboard_new (GDK_SELECTION_CLIPBOARD, settings);
    GPasteClipboard *primary = g_paste_clipboard_new (GDK_SELECTION_PRIMARY, settings);

    GPasteKeybinding *keybindings[] = {
        G_PASTE_KEYBINDING (g_paste_paste_and_pop_keybinding_new (xcb_wrapper,
                                                                  settings,
                                                                  history)),
        G_PASTE_KEYBINDING (g_paste_show_history_keybinding_new (xcb_wrapper,
                                                                 settings,
                                                                 g_paste_daemon))
    };

    c_signals[C_NAME_LOST] = g_signal_connect (G_OBJECT (g_paste_daemon),
                                               "name-lost",
                                               G_CALLBACK (on_name_lost),
                                               NULL); /* user_data */
    c_signals[C_REEXECUTE_SELF] = g_signal_connect (G_OBJECT (g_paste_daemon),
                                                    "reexecute-self",
                                                    G_CALLBACK (reexec),
                                                    NULL); /* user_data */

    for (guint k = 0; k < ELEMENTSOF (keybindings); ++k)
        g_paste_keybinder_add_keybinding (keybinder, keybindings[k]);

    g_paste_history_load (history);
    g_paste_keybinder_activate_all (keybinder);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, clipboard);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, primary);
    g_paste_clipboards_manager_activate (clipboards_manager);

    g_object_unref (history);
    g_object_unref (clipboards_manager);
    g_object_unref (xcb_wrapper);
    g_object_unref (keybinder);
    g_object_unref (clipboard);
    g_object_unref (primary);

    for (guint k = 0; k < ELEMENTSOF (keybindings); ++k)
        g_object_unref (keybindings[k]);

    signal (SIGTERM, &signal_handler);
    signal (SIGINT, &signal_handler);

    main_loop = g_main_loop_new (NULL, FALSE);

    gint exit_status = EXIT_SUCCESS;
    GError *error = NULL;
    if (g_paste_daemon_own_bus_name (g_paste_daemon, &error))
    {
        g_main_loop_run (main_loop);
    }
    else
    {
        g_error_free (error);
        fprintf (stderr, "%s\n", _("Could not register DBus service."));
        exit_status = EXIT_FAILURE;
    }

    g_signal_handler_disconnect (g_paste_daemon, c_signals[C_NAME_LOST]);
    g_signal_handler_disconnect (g_paste_daemon, c_signals[C_REEXECUTE_SELF]);
    g_object_unref (settings);
    g_object_unref (g_paste_daemon);
    g_main_loop_unref (main_loop);

    return exit_status;
}
