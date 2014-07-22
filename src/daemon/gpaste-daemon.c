/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste.h>
#include <gpaste-daemon.h>
#include <gpaste-make-password-keybinding.h>
#include <gpaste-pop-keybinding.h>
#include <gpaste-show-history-keybinding.h>
#include <gpaste-sync-clipboard-to-primary-keybinding.h>
#include <gpaste-sync-primary-to-clipboard-keybinding.h>

#include <glib/gi18n-lib.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static GApplication *_app;

enum
{
    C_NAME_LOST,
    C_REEXECUTE_SELF,

    C_LAST_SIGNAL
};

static void
signal_handler (int signum)
{
    g_print (_("Signal %d received, exiting\n"), signum);
    g_application_quit (_app);
}

G_PASTE_NORETURN static void
on_name_lost (GPasteDaemon *g_paste_daemon G_GNUC_UNUSED,
              gpointer      user_data)
{
    GApplication *app = user_data;

    fprintf (stderr, "%s\n", _("Could not acquire DBus name."));
    g_application_quit (app);
    exit (EXIT_FAILURE);
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
    G_PASTE_INIT_APPLICATION ("Daemon");
    /* Keep the gapplication around */
    gtk_widget_hide (gtk_application_window_new (app));
    _app = gapp;

    G_PASTE_CLEANUP_UNREF GPasteSettings *settings = g_paste_settings_new ();
    G_PASTE_CLEANUP_UNREF GPasteHistory *history = g_paste_history_new (settings);
    G_PASTE_CLEANUP_UNREF GPasteClipboardsManager *clipboards_manager = g_paste_clipboards_manager_new (history, settings);
    G_PASTE_CLEANUP_UNREF GPasteGnomeShellClient *shell_client = g_paste_gnome_shell_client_new_sync (NULL);
    G_PASTE_CLEANUP_UNREF GPasteKeybinder *keybinder = g_paste_keybinder_new (settings, shell_client);
    G_PASTE_CLEANUP_UNREF GPasteDaemon *g_paste_daemon = g_paste_daemon_new (history, settings, clipboards_manager, keybinder);
    G_PASTE_CLEANUP_UNREF GPasteClipboard *clipboard = g_paste_clipboard_new (GDK_SELECTION_CLIPBOARD, settings);
    G_PASTE_CLEANUP_UNREF GPasteClipboard *primary = g_paste_clipboard_new (GDK_SELECTION_PRIMARY, settings);

    GPasteKeybinding *keybindings[] = {
        g_paste_make_password_keybinding_new (history),
        g_paste_pop_keybinding_new (history),
        g_paste_show_history_keybinding_new (g_paste_daemon),
        g_paste_sync_clipboard_to_primary_keybinding_new (clipboards_manager),
        g_paste_sync_primary_to_clipboard_keybinding_new (clipboards_manager)
    };

    gulong c_signals[C_LAST_SIGNAL] = {
        [C_NAME_LOST] = g_signal_connect (g_paste_daemon,
                                          "name-lost",
                                          G_CALLBACK (on_name_lost),
                                          gapp),
        [C_REEXECUTE_SELF] = g_signal_connect (g_paste_daemon,
                                               "reexecute-self",
                                               G_CALLBACK (reexec),
                                               gapp)
    };

    for (guint k = 0; k < G_N_ELEMENTS (keybindings); ++k)
        g_paste_keybinder_add_keybinding (keybinder, keybindings[k]);

    g_paste_history_load (history);
    g_paste_keybinder_activate_all (keybinder);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, clipboard);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, primary);
    g_paste_clipboards_manager_activate (clipboards_manager);

    signal (SIGTERM, &signal_handler);
    signal (SIGINT, &signal_handler);

    gint exit_status;
    if (g_paste_daemon_own_bus_name (g_paste_daemon, &error))
        exit_status = g_application_run (gapp, argc, argv);
    else
    {
        g_critical ("%s: %s\n", _("Could not register DBus service."), error->message);
        exit_status = EXIT_FAILURE;
    }

    g_signal_handler_disconnect (g_paste_daemon, c_signals[C_NAME_LOST]);
    g_signal_handler_disconnect (g_paste_daemon, c_signals[C_REEXECUTE_SELF]);

    return exit_status;
}
