/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK3_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk3.h> can be included directly."
#endif

#pragma once

#include <gpaste-gtk3/gpaste-gtk-settings-ui-panel.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_GTK_SETTINGS_UI_STACK (g_paste_gtk_settings_ui_stack_get_type ())

G_PASTE_GTK_FINAL_TYPE (SettingsUiStack, settings_ui_stack, SETTINGS_UI_STACK, GtkStack)

void g_paste_gtk_settings_ui_stack_add_panel (GPasteGtkSettingsUiStack *self,
                                              const gchar              *name,
                                              const gchar              *label,
                                              GPasteGtkSettingsUiPanel *panel);
void g_paste_gtk_settings_ui_stack_fill      (GPasteGtkSettingsUiStack *self);

GPasteGtkSettingsUiStack *g_paste_gtk_settings_ui_stack_new (void);

G_END_DECLS
