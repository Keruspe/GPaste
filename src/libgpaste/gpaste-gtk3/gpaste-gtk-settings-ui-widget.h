/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK3_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk3.h> can be included directly."
#endif

#pragma once

#include <gpaste-gtk3/gpaste-gtk-settings-ui-stack.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_GTK_SETTINGS_UI_WIDGET (g_paste_gtk_settings_ui_widget_get_type ())

G_PASTE_GTK_FINAL_TYPE (SettingsUiWidget, settings_ui_widget, SETTINGS_UI_WIDGET, GtkGrid)

GPasteGtkSettingsUiStack *g_paste_gtk_settings_ui_widget_get_stack (GPasteGtkSettingsUiWidget *self);

GtkWidget *g_paste_gtk_settings_ui_widget_new (void);

G_END_DECLS
