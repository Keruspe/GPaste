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

#include <gpaste-settings-ui-widget.h>
#include <gpaste-client.h>

#include <glib/gi18n.h>

#include <stdlib.h>

static void
client_ready (GObject      *source_object G_GNUC_UNUSED,
              GAsyncResult *res,
              gpointer      user_data     G_GNUC_UNUSED)
{
    G_PASTE_CLEANUP_ERROR_FREE GError *error = NULL;
    G_PASTE_CLEANUP_UNREF GPasteClient *client = g_paste_client_new_finish (res, &error);
    if (!error)
        g_paste_client_about (client, NULL, NULL);
}

static void
about_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data G_GNUC_UNUSED)
{
    g_paste_client_new (client_ready, NULL);
}

static void
quit_activated (GSimpleAction *action    G_GNUC_UNUSED,
                GVariant      *parameter G_GNUC_UNUSED,
                gpointer       user_data G_GNUC_UNUSED)
{
    g_application_quit (user_data);
}

static void
show_win (GApplication *application)
{
    for (GList *wins = gtk_application_get_windows (GTK_APPLICATION (application)); wins; wins = g_list_next (wins))
        gtk_window_present (wins->data);
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_APPLICATION_FULL ("Settings", show_win);

    GActionEntry app_entries[] = {
        { "about", about_activated, NULL, NULL, NULL, { 0 } },
        { "quit",  quit_activated,  NULL, NULL, NULL, { 0 } }
    };
    g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app);

    GMenu *menu = g_menu_new ();
    g_menu_append (menu, "About GPaste", "app.about");
    g_menu_append (menu, "Quit", "app.quit");
    gtk_application_set_app_menu (app, G_MENU_MODEL (menu));

    GPasteSettingsUiStack *stack = g_paste_settings_ui_stack_new ();

    if (!stack)
        exit (EXIT_FAILURE);

    g_paste_settings_ui_stack_fill (stack);

    GtkWidget *bar = gtk_header_bar_new ();
    GtkHeaderBar *header_bar = GTK_HEADER_BAR (bar);
    gtk_header_bar_set_custom_title (header_bar, gtk_widget_new (GTK_TYPE_STACK_SWITCHER,
                                                                 "stack",  GTK_STACK (stack),
                                                                 NULL));
    gtk_header_bar_set_show_close_button (header_bar, TRUE);

    GtkWidget *win = gtk_widget_new (GTK_TYPE_APPLICATION_WINDOW,
                                     "application",     app,
                                     "type",            GTK_WINDOW_TOPLEVEL,
                                     "window-position", GTK_WIN_POS_CENTER,
                                     "resizable",       FALSE,
                                     NULL);
    gtk_window_set_titlebar(GTK_WINDOW (win), bar);
    gtk_container_add (GTK_CONTAINER (win), GTK_WIDGET (stack));
    gtk_widget_show_all (win);

    return g_application_run (gapp, argc, argv);
}
