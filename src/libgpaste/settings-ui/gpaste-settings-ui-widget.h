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

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_SETTINGS_UI_WIDGET_H__
#define __G_PASTE_SETTINGS_UI_WIDGET_H__

#include <gpaste-settings-ui-stack.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SETTINGS_UI_WIDGET            (g_paste_settings_ui_widget_get_type ())
#define G_PASTE_SETTINGS_UI_WIDGET(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_SETTINGS_UI_WIDGET, GPasteSettingsUiWidget))
#define G_PASTE_IS_SETTINGS_UI_WIDGET(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_SETTINGS_UI_WIDGET))
#define G_PASTE_SETTINGS_UI_WIDGET_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_SETTINGS_UI_WIDGET, GPasteSettingsUiWidgetClass))
#define G_PASTE_IS_SETTINGS_UI_WIDGET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_SETTINGS_UI_WIDGET))
#define G_PASTE_SETTINGS_UI_WIDGET_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_SETTINGS_UI_WIDGET, GPasteSettingsUiWidgetClass))

typedef struct _GPasteSettingsUiWidget GPasteSettingsUiWidget;
typedef struct _GPasteSettingsUiWidgetClass GPasteSettingsUiWidgetClass;

G_PASTE_VISIBLE
GType g_paste_settings_ui_widget_get_type (void);

GPasteSettingsUiStack *g_paste_settings_ui_widget_get_stack (GPasteSettingsUiWidget *self);

GtkWidget *g_paste_settings_ui_widget_new (void);

G_END_DECLS

#endif /*__G_PASTE_SETTINGS_UI_WIDGET_H__*/
