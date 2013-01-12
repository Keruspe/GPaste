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

#ifndef __G_PASTE_SETTINGS_UI_WINDOW_H__
#define __G_PASTE_SETTINGS_UI_WINDOW_H__

#ifdef G_PASTE_COMPILATION
#include "config.h"
#endif

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SETTINGS_UI_WINDOW            (g_paste_settings_ui_window_get_type ())
#define G_PASTE_SETTINGS_UI_WINDOW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_SETTINGS_UI_WINDOW, GPasteSettingsUiWindow))
#define G_PASTE_IS_SETTINGS_UI_WINDOW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_SETTINGS_UI_WINDOW))
#define G_PASTE_SETTINGS_UI_WINDOW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_SETTINGS_UI_WINDOW, GPasteSettingsUiWindowClass))
#define G_PASTE_IS_SETTINGS_UI_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_SETTINGS_UI_WINDOW))
#define G_PASTE_SETTINGS_UI_WINDOW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_SETTINGS_UI_WINDOW, GPasteSettingsUiWindowClass))

typedef struct _GPasteSettingsUiWindow GPasteSettingsUiWindow;
typedef struct _GPasteSettingsUiWindowClass GPasteSettingsUiWindowClass;

#ifdef G_PASTE_COMPILATION
G_PASTE_VISIBLE
#endif
GType g_paste_settings_ui_window_get_type (void);

GPasteSettingsUiWindow *g_paste_settings_ui_window_new (GtkApplication *application);

G_END_DECLS

#endif /*__G_PASTE_SETTINGS_UI_WINDOW_H__*/
