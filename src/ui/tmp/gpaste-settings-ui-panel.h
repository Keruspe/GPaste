/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_SETTINGS_UI_PANEL_H__
#define __G_PASTE_SETTINGS_UI_PANEL_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_SETTINGS_UI_PANEL (g_paste_settings_ui_panel_get_type ())

G_PASTE_FINAL_TYPE (SettingsUiPanel, settings_ui_panel, SETTINGS_UI_PANEL, GtkGrid)

typedef void (*GPasteBooleanCallback)     (gboolean data,
                                           gpointer user_data);
typedef void (*GPasteRangeCallback)       (gdouble  data,
                                           gpointer user_data);
typedef void (*GPasteTextCallback)        (const gchar *data,
                                           gpointer     user_data);
typedef void (*GPasteResetCallback)       (gpointer user_data);

GtkSwitch *g_paste_settings_ui_panel_add_boolean_setting (GPasteSettingsUiPanel *self,
                                                          const gchar           *label,
                                                          gboolean               value,
                                                          GPasteBooleanCallback  on_value_changed,
                                                          GPasteResetCallback    on_reset,
                                                          gpointer               user_data);
void g_paste_settings_ui_panel_add_separator (GPasteSettingsUiPanel *self);
GtkSpinButton *g_paste_settings_ui_panel_add_range_setting (GPasteSettingsUiPanel *self,
                                                            const gchar           *label,
                                                            gdouble                value,
                                                            gdouble                min,
                                                            gdouble                max,
                                                            gdouble                step,
                                                            GPasteRangeCallback    on_value_changed,
                                                            GPasteResetCallback    on_reset,
                                                            gpointer               user_data);
GtkEntry *g_paste_settings_ui_panel_add_text_setting (GPasteSettingsUiPanel *self,
                                                      const gchar           *label,
                                                      const gchar           *value,
                                                      GPasteTextCallback     on_value_changed,
                                                      GPasteResetCallback    on_reset,
                                                      gpointer               user_data);

GPasteSettingsUiPanel *g_paste_settings_ui_panel_new (void);

G_END_DECLS

#endif /*__G_PASTE_SETTINGS_UI_PANEL_H__*/
