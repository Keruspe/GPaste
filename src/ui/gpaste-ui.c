/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-ui-window.h>
#include <gpaste-util.h>

#define LICENSE                                                            \
    "GPaste is free software: you can redistribute it and/or modify"       \
    "it under the terms of the GNU General Public License as published by" \
    "the Free Software Foundation, either version 3 of the License, or"    \
    "(at your option) any later version.\n\n"                              \
    "GPaste is distributed in the hope that it will be useful,"            \
    "but WITHOUT ANY WARRANTY; without even the implied warranty of"       \
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the"        \
    "GNU General Public License for more details.\n\n"                     \
    "You should have received a copy of the GNU General Public License"    \
    "along with GPaste.  If not, see <http://www.gnu.org/licenses/>."

static void
prefs_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data)
{
    g_paste_ui_window_show_prefs (G_PASTE_UI_WINDOW (gtk_application_get_windows (GTK_APPLICATION (user_data))->data));
}

static void
about_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data)
{
    GtkWindow *parent = GTK_WINDOW (gtk_application_get_windows (GTK_APPLICATION (user_data))->data);
    const gchar *authors[] = {
        "Marc-Antoine Perennou <Marc-Antoine@Perennou.com>",
        NULL
    };

    gtk_show_about_dialog (parent,
                           "program-name",   PACKAGE_NAME,
                           "version",        PACKAGE_VERSION,
                           "logo-icon-name", "gtk-paste",
                           "license",        LICENSE,
                           "authors",        authors,
                           "copyright",      "Copyright © 2010-2015 Marc-Antoine Perennou",
                           "comments",       "Clipboard management system",
                           "website",        "http://www.imagination-land.org/tags/GPaste.html",
                           "website-label",  "Follow GPaste news",
                           "wrap-license",   TRUE,
                           NULL);
}

static void
quit_activated (GSimpleAction *action    G_GNUC_UNUSED,
                GVariant      *parameter G_GNUC_UNUSED,
                gpointer       user_data)
{
    g_application_quit (G_APPLICATION (user_data));
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_APPLICATION_FULL ("Ui", g_paste_util_show_win);

    GActionEntry app_entries[] = {
        { "about", about_activated, NULL, NULL, NULL, { 0 } },
        { "prefs", prefs_activated, NULL, NULL, NULL, { 0 } },
        { "quit",  quit_activated,  NULL, NULL, NULL, { 0 } }
    };

    g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app);
    GMenu *menu = g_menu_new ();
    g_menu_append (menu, "GPaste Settings", "app.prefs");
    g_menu_append (menu, "About GPaste", "app.about");
    g_menu_append (menu, "Quit", "app.quit");
    gtk_application_set_app_menu (app, G_MENU_MODEL (menu));

    G_GNUC_UNUSED GtkWidget *window = g_paste_ui_window_new (app);

    return g_application_run (gapp, argc, argv);
}
