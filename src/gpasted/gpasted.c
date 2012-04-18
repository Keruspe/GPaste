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

#include "gpaste-daemon.h"
#include "gpaste-clipboard-internal.h"

#include <glib/gi18n-lib.h>
#include <xcb/xtest.h>
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
rebind (GPasteSettings   *settings,
        GPasteKeybindings keybinding,
        gpointer          user_data)
{
    GPasteKeybinding **keybindings = (GPasteKeybinding **) user_data;
    const gchar *binding;

    switch (keybinding)
    {
    case G_PASTE_KEYBINDINGS_SHOW_HISTORY:
        binding = g_paste_settings_get_show_history (settings);
        break;
    case G_PASTE_KEYBINDINGS_PASTE_AND_POP:
        binding = g_paste_settings_get_paste_and_pop (settings);
        break;
    default:
        binding = NULL;
        break;
    }

    if (binding)
        g_paste_keybinding_rebind (keybindings[keybinding], binding);
}

static void
reexec (GPasteDaemon *g_paste_daemon G_GNUC_UNUSED,
        gpointer      user_data G_GNUC_UNUSED)
{
    g_main_loop_quit (main_loop);
    execl (PKGLIBEXECDIR "/gpasted", "gpasted", NULL);
}

typedef struct {
    GPasteXcbWrapper *xcb_wrapper;
    GPasteHistory    *history;
} PasteAndPopData;

static void
fake_keyboard (GPasteXcbWrapper *xcb_wrapper,
               xcb_connection_t *connection,
               xcb_screen_t     *screen,
               xcb_keysym_t      keysym,
               guint8            delay,
               guint8            event)
{
    xcb_keycode_t *keycode = (xcb_keycode_t *) xcb_key_symbols_get_keycode ((xcb_key_symbols_t *) g_paste_xcb_wrapper_get_keysyms (xcb_wrapper), keysym);

    if (!keycode)
        return;

    xcb_test_fake_input (connection,
                         event,
                         *keycode,
                         delay,
                         screen->root,
                         0, 0, 0);

    xcb_flush (connection);
}

static void
paste_and_pop_get_clipboard_data (GtkClipboard     *clipboard,
                                  GtkSelectionData *selection_data,
                                  guint             info,
                                  gpointer          user_data_or_owner)
{
    PasteAndPopData *data = (PasteAndPopData *) user_data_or_owner;
    GPasteHistory *history = data->history;

    g_paste_clipboard_get_clipboard_data (clipboard,
                                          selection_data,
                                          info,
                                          G_OBJECT (g_paste_history_get (history, 0)));
    g_paste_history_remove (history, 0);
}

static void
paste_and_pop_clear_clipboard_data (GtkClipboard *clipboard G_GNUC_UNUSED,
                                    gpointer      user_data_or_owner G_GNUC_UNUSED)
{
}

static void
paste_and_pop (PasteAndPopData *data)
{
    GPasteXcbWrapper *xcb_wrapper = data->xcb_wrapper;
    xcb_connection_t *connection = (xcb_connection_t *) g_paste_xcb_wrapper_get_connection (xcb_wrapper);
    xcb_screen_t *screen = (xcb_screen_t *) g_paste_xcb_wrapper_get_screen (xcb_wrapper);

    g_return_if_fail (screen); /* This should never happen */

    GtkTargetList *target_list = gtk_target_list_new (NULL, 0);

    gtk_target_list_add_text_targets (target_list, 0);
    gtk_target_list_add_image_targets (target_list, 0, TRUE);
    gtk_target_list_add_uri_targets (target_list, 0);
    gtk_target_list_add (target_list, g_paste_clipboard_copy_files_target, 0, 0);

    gint n_targets;
    GtkTargetEntry *targets = gtk_target_table_new_from_list (target_list, &n_targets);
    GtkClipboard *clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);

    gtk_clipboard_set_with_data (clipboard,
                                 targets,
                                 n_targets,
                                 paste_and_pop_get_clipboard_data,
                                 paste_and_pop_clear_clipboard_data,
                                 data);

    fake_keyboard (xcb_wrapper, connection, screen, GDK_KEY_Shift_L, 0, XCB_KEY_PRESS);
    fake_keyboard (xcb_wrapper, connection, screen, GDK_KEY_Insert, 200, XCB_KEY_PRESS);
    fake_keyboard (xcb_wrapper, connection, screen, GDK_KEY_Shift_L, 0, XCB_KEY_RELEASE);
    fake_keyboard (xcb_wrapper, connection, screen, GDK_KEY_Insert, 0, XCB_KEY_RELEASE);

    gtk_target_table_free (targets, n_targets);
    gtk_target_list_unref (target_list);
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
    GPasteHistory *history = g_paste_history_new (settings);
    GPasteClipboardsManager *clipboards_manager = g_paste_clipboards_manager_new (history, settings);
    GPasteXcbWrapper *xcb_wrapper = g_paste_xcb_wrapper_new ();
    GPasteKeybinder *keybinder = g_paste_keybinder_new (xcb_wrapper);
    GPasteDaemon *g_paste_daemon = g_paste_daemon_new (history, settings, clipboards_manager, keybinder);
    GPasteClipboard *clipboard = g_paste_clipboard_new (GDK_SELECTION_CLIPBOARD, settings);
    GPasteClipboard *primary = g_paste_clipboard_new (GDK_SELECTION_PRIMARY, settings);

    PasteAndPopData data = {
        .xcb_wrapper = xcb_wrapper,
        .history = history
    };

    GPasteKeybinding **keybindings = alloca (G_PASTE_KEYBINDINGS_LAST_KEYBINDING * sizeof (GPasteKeybinding *));
    keybindings[G_PASTE_KEYBINDINGS_SHOW_HISTORY] = g_paste_keybinding_new (xcb_wrapper,
                                                                            g_paste_settings_get_show_history (settings),
                                                                            (GPasteKeybindingFunc) g_paste_daemon_show_history,
                                                                            g_paste_daemon);
    keybindings[G_PASTE_KEYBINDINGS_PASTE_AND_POP] = g_paste_keybinding_new (xcb_wrapper,
                                                                             g_paste_settings_get_paste_and_pop (settings),
                                                                             (GPasteKeybindingFunc) paste_and_pop,
                                                                             &data);

    g_signal_connect (G_OBJECT (settings),
                      "rebind",
                      G_CALLBACK (rebind),
                      keybindings);
    g_signal_connect (G_OBJECT (g_paste_daemon),
                      "reexecute-self",
                      G_CALLBACK (reexec),
                      NULL); /* user_data */

    for (GPasteKeybindings k = 0; k < G_PASTE_KEYBINDINGS_LAST_KEYBINDING; ++k)
        g_paste_keybinder_add_keybinding (keybinder, keybindings[k]);

    g_paste_history_load (history);
    g_paste_keybinder_activate_all (keybinder);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, clipboard);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, primary);
    g_paste_clipboards_manager_activate (clipboards_manager);

    g_object_unref (settings);
    g_object_unref (history);
    g_object_unref (clipboards_manager);
    g_object_unref (xcb_wrapper);
    g_object_unref (keybinder);
    g_object_unref (clipboard);
    g_object_unref (primary);

    for (GPasteKeybindings k = 0; k < G_PASTE_KEYBINDINGS_LAST_KEYBINDING; ++k)
        g_object_unref (keybindings[k]);

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

    g_object_unref (g_paste_daemon);
    g_bus_unown_name (owner_id);
    g_main_loop_unref (main_loop);

    return EXIT_SUCCESS;
}
