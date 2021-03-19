/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_SETTINGS_UI_WIDGET_H__
#define __G_PASTE_SETTINGS_UI_WIDGET_H__

#include <gpaste-settings-ui-stack.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SETTINGS_UI_WIDGET (g_paste_settings_ui_widget_get_type ())

G_PASTE_FINAL_TYPE (SettingsUiWidget, settings_ui_widget, SETTINGS_UI_WIDGET, GtkGrid)

GPasteSettingsUiStack *g_paste_settings_ui_widget_get_stack (GPasteSettingsUiWidget *self);

GtkWidget *g_paste_settings_ui_widget_new (void);

G_END_DECLS

#endif /*__G_PASTE_SETTINGS_UI_WIDGET_H__*/
