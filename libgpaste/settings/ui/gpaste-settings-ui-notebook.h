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

#ifndef __G_PASTE_SETTINGS_UI_NOTEBOOK_H__
#define __G_PASTE_SETTINGS_UI_NOTEBOOK_H__

#include "gpaste-settings-ui-panel.h"

G_BEGIN_DECLS

#define G_PASTE_TYPE_SETTINGS_UI_NOTEBOOK            (g_paste_settings_ui_notebook_get_type ())
#define G_PASTE_SETTINGS_UI_NOTEBOOK(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_SETTINGS_UI_NOTEBOOK, GPasteSettingsUiNotebook))
#define G_PASTE_IS_SETTINGS_UI_NOTEBOOK(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_SETTINGS_UI_NOTEBOOK))
#define G_PASTE_SETTINGS_UI_NOTEBOOK_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_SETTINGS_UI_NOTEBOOK, GPasteSettingsUiNotebookClass))
#define G_PASTE_IS_SETTINGS_UI_NOTEBOOK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_SETTINGS_UI_NOTEBOOK))
#define G_PASTE_SETTINGS_UI_NOTEBOOK_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_SETTINGS_UI_NOTEBOOK, GPasteSettingsUiNotebookClass))

typedef struct _GPasteSettingsUiNotebook GPasteSettingsUiNotebook;
typedef struct _GPasteSettingsUiNotebookClass GPasteSettingsUiNotebookClass;

#ifdef G_PASTE_COMPILATION
G_PASTE_VISIBLE
#endif
GType g_paste_settings_ui_notebook_get_type (void);

void g_paste_settings_ui_notebook_add_panel (GPasteSettingsUiNotebook *self,
                                             const gchar              *label,
                                             GPasteSettingsUiPanel    *panel);

GPasteSettingsUiNotebook *g_paste_settings_ui_notebook_new (void);

G_END_DECLS

#endif /*__G_PASTE_SETTINGS_UI_NOTEBOOK_H__*/
