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

#include "gpaste-settings-ui-widget.h"

#include <glib/gi18n.h>

int
main (int argc, char *argv[])
{
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    gtk_init (&argc, &argv);
    g_object_set (gtk_settings_get_default (), "gtk-application-prefer-dark-theme", TRUE, NULL);
    
    GtkApplication *app = gtk_application_new ("org.gnome.GPaste.Settings", G_APPLICATION_FLAGS_NONE);
    GApplication *gapp = G_APPLICATION (app);
    GError *error = NULL;

    G_APPLICATION_GET_CLASS (gapp)->activate = NULL;
    g_application_register (gapp, NULL, &error);

    if (error)
    {
        fprintf (stderr, "%s: %s\n", _("Failed to register the gtk application"), error->message);
        g_error_free (error);
        return 1;
    }

    GPasteSettingsUiStack *stack = g_paste_settings_ui_stack_new ();
    g_paste_settings_ui_stack_fill (stack);

    GtkWidget *bar = gtk_header_bar_new ();
    GtkHeaderBar *header_bar = GTK_HEADER_BAR (bar);
    gtk_header_bar_pack_start (header_bar, gtk_widget_new (GTK_TYPE_STACK_SWITCHER,
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
