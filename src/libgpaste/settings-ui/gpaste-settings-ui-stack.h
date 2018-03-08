/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_SETTINGS_UI_STACK_H__
#define __G_PASTE_SETTINGS_UI_STACK_H__

#include <gpaste-settings-ui-panel.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SETTINGS_UI_STACK (g_paste_settings_ui_stack_get_type ())

G_PASTE_FINAL_TYPE (SettingsUiStack, settings_ui_stack, SETTINGS_UI_STACK, GtkStack)

void g_paste_settings_ui_stack_add_panel (GPasteSettingsUiStack *self,
                                          const gchar              *name,
                                          const gchar              *label,
                                          GPasteSettingsUiPanel    *panel);
void g_paste_settings_ui_stack_fill      (GPasteSettingsUiStack *self);

GPasteSettingsUiStack *g_paste_settings_ui_stack_new (void);

G_END_DECLS

#endif /*__G_PASTE_SETTINGS_UI_STACK_H__*/
