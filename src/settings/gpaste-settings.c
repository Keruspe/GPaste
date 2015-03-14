/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include <gpaste-settings-ui-stack.h>

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_APPLICATION_WITH_WIN ("Settings");

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
                                     "window-position", GTK_WIN_POS_CENTER_ALWAYS,
                                     "resizable",       FALSE,
                                     NULL);
    gtk_window_set_titlebar(GTK_WINDOW (win), bar);
    gtk_container_add (GTK_CONTAINER (win), GTK_WIDGET (stack));
    gtk_widget_show_all (win);

    return g_application_run (gapp, argc, argv);
}
