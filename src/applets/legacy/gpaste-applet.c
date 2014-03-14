/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-applet.h>

#include <glib/gi18n.h>

#include <stdlib.h>

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
about_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data G_GNUC_UNUSED)
{
    const gchar *_authors[] = {
        "Marc-Antoine Perennou <Marc-Antoine@Perennou.com>",
        NULL
    };
    G_PASTE_CLEANUP_B_STRV_FREE GStrv authors = g_boxed_copy (G_TYPE_STRV, _authors);
    gtk_show_about_dialog (NULL,
                           "program-name",   PACKAGE_NAME,
                           "version",        PACKAGE_VERSION,
                           "logo-icon-name", "gtk-paste",
                           "license",        LICENSE,
                           "authors",        authors,
                           "copyright",      "Copyright © 2010-2013 Marc-Antoine Perennou",
                           "comments",       "Clipboard management system",
                           "website",        "http://www.imagination-land.org/tags/GPaste.html",
                           "website-label",  "Follow GPaste actuality",
                           "wrap-license",   TRUE,
                           NULL);
}

static void
quit_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data G_GNUC_UNUSED)
{
    g_application_quit (user_data);
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_GETTEXT ();

    gtk_init (&argc, &argv);
    g_object_set (gtk_settings_get_default (), "gtk-application-prefer-dark-theme", TRUE, NULL);

    GtkApplication *app = gtk_application_new ("org.gnome.GPaste.Applet", G_APPLICATION_FLAGS_NONE);
    GApplication *gapp = G_APPLICATION (app);
    G_PASTE_CLEANUP_ERROR_FREE GError *error = NULL;

    G_APPLICATION_GET_CLASS (gapp)->activate = NULL;
    g_application_register (gapp, NULL, &error);

    if (g_application_get_is_remote (gapp))
        return EXIT_SUCCESS;

    if (error)
    {
        fprintf (stderr, "%s: %s\n", _("Failed to register the gtk application"), error->message);

        return EXIT_FAILURE;
    }

    GActionEntry app_entries[] = {
        { "about", about_activated, NULL, NULL, NULL, { 0 } },
        { "quit",  quit_activated,  NULL, NULL, NULL, { 0 } }
    };
    g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app);

    GMenu *menu = g_menu_new ();
    g_menu_append (menu, "About GPaste", "app.about");
    g_menu_append (menu, "Quit", "app.quit");
    gtk_application_set_app_menu (app, G_MENU_MODEL (menu));

    G_PASTE_CLEANUP_UNREF GPasteApplet *applet = g_paste_applet_new_status_icon (app);

    return g_application_run (gapp, argc, argv);
}
