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

#include "gpaste-settings-ui-notebook-private.h"

G_DEFINE_TYPE (GPasteSettingsUiNotebook, g_paste_settings_ui_notebook, GTK_TYPE_NOTEBOOK)

/**
 * g_paste_settings_ui_notebook_add_panel:
 * @self: a #GPasteSettingsUiNotebook instance
 * @label: the label to display
 * @panel: (transfer none): the #GPasteSettingsUiPanel to add
 *
 * Add a new panel to the #GPasteSettingsUiNotebook
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_ui_notebook_add_panel (GPasteSettingsUiNotebook *self,
                                        const gchar              *label,
                                        GPasteSettingsUiPanel    *panel)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS_UI_NOTEBOOK (self));

    gtk_notebook_append_page (GTK_NOTEBOOK (self),
                              GTK_WIDGET (panel),
                              gtk_label_new (label));
}

static void
g_paste_settings_ui_notebook_class_init (GPasteSettingsUiNotebookClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_settings_ui_notebook_init (GPasteSettingsUiNotebook *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_settings_ui_notebook_new:
 *
 * Create a new instance of #GPasteSettingsUiNotebook
 *
 * Returns: a newly allocated #GPasteSettingsUiNotebook
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteSettingsUiNotebook *
g_paste_settings_ui_notebook_new (void)
{
    return G_PASTE_SETTINGS_UI_NOTEBOOK (gtk_widget_new (G_PASTE_TYPE_SETTINGS_UI_NOTEBOOK, "margin", 12, NULL));
}
