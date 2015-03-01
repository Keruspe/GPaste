/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
typedef void (*GPasteMultiActionCallback) (const gchar *action,
                                           const gchar *target,
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
GtkEntry *g_paste_settings_ui_panel_add_text_confirm_setting (GPasteSettingsUiPanel *self,
                                                              const gchar           *label,
                                                              const gchar           *value,
                                                              GPasteTextCallback     on_value_changed,
                                                              gpointer               user_data1,
                                                              const gchar           *confirm_label,
                                                              GPasteTextCallback     confirm_action,
                                                              gpointer               user_data);
GtkComboBoxText *g_paste_settings_ui_panel_add_multi_action_setting (GPasteSettingsUiPanel    *self,
                                                                     gchar ** const           *action_labels,
                                                                     const gchar              *confirm_label,
                                                                     GPasteMultiActionCallback confirm_action,
                                                                     gpointer                  user_data);

GPasteSettingsUiPanel *g_paste_settings_ui_panel_new (void);

G_END_DECLS

#endif /*__G_PASTE_SETTINGS_UI_PANEL_H__*/
