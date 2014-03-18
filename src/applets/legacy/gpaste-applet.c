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

    G_PASTE_CLEANUP_UNREF GPasteApplet *applet = g_paste_applet_new_status_icon (app);

    return g_application_run (gapp, argc, argv);
}
