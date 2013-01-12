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

#include "gpaste-settings-ui-window-private.h"

#include "gpaste-settings-ui-notebook.h"

#include <glib/gi18n.h>

G_DEFINE_TYPE (GPasteSettingsUiWindow, g_paste_settings_ui_window, GTK_TYPE_WINDOW)

static void
g_paste_settings_ui_window_class_init (GPasteSettingsUiWindowClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_settings_ui_window_init (GPasteSettingsUiWindow *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_settings_ui_window_new:
 *
 * Create a new instance of #GPasteSettingsUiWindow
 *
 * Returns: a newly allocated #GPasteSettingsUiWindow
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteSettingsUiWindow *
g_paste_settings_ui_window_new (GtkApplication *application)
{
    GPasteSettingsUiWindow *self = G_PASTE_SETTINGS_UI_WINDOW (gtk_widget_new (G_PASTE_TYPE_SETTINGS_UI_WINDOW,
                                                                               "type", GTK_WINDOW_TOPLEVEL,
                                                                               "title", _("GPaste daemon settings"),
                                                                               "application", application,
                                                                               "window-position", GTK_WIN_POS_CENTER,
                                                                               "resizable", FALSE,
                                                                               NULL));
    GPasteSettingsUiNotebook *notebook = g_paste_settings_ui_notebook_new ();

    g_paste_settings_ui_notebook_fill (notebook);
    gtk_container_add (GTK_CONTAINER (self), GTK_WIDGET (notebook));

    return self;
}
