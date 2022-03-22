/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK3_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk3.h> can be included directly."
#endif

#pragma once

#include <gpaste-gtk3/gpaste-gtk-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_GTK_SETTINGS_UI_PANEL (g_paste_gtk_settings_ui_panel_get_type ())

G_PASTE_GTK_FINAL_TYPE (SettingsUiPanel, settings_ui_panel, SETTINGS_UI_PANEL, GtkGrid)

typedef void (*GPasteGtkBooleanCallback)     (gboolean data,
                                              gpointer user_data);
typedef void (*GPasteGtkRangeCallback)       (gdouble  data,
                                              gpointer user_data);
typedef void (*GPasteGtkTextCallback)        (const gchar *data,
                                              gpointer     user_data);
typedef void (*GPasteGtkResetCallback)       (gpointer user_data);

GtkSwitch *g_paste_gtk_settings_ui_panel_add_boolean_setting (GPasteGtkSettingsUiPanel *self,
                                                              const gchar              *label,
                                                              gboolean                  value,
                                                              GPasteGtkBooleanCallback  on_value_changed,
                                                              GPasteGtkResetCallback    on_reset,
                                                              gpointer                  user_data);
void g_paste_gtk_settings_ui_panel_add_separator (GPasteGtkSettingsUiPanel *self);
GtkSpinButton *g_paste_gtk_settings_ui_panel_add_range_setting (GPasteGtkSettingsUiPanel *self,
                                                                const gchar              *label,
                                                                gdouble                   value,
                                                                gdouble                   min,
                                                                gdouble                   max,
                                                                gdouble                   step,
                                                                GPasteGtkRangeCallback    on_value_changed,
                                                                GPasteGtkResetCallback    on_reset,
                                                                gpointer                  user_data);
GtkEntry *g_paste_gtk_settings_ui_panel_add_text_setting (GPasteGtkSettingsUiPanel *self,
                                                          const gchar              *label,
                                                          const gchar              *value,
                                                          GPasteGtkTextCallback     on_value_changed,
                                                          GPasteGtkResetCallback    on_reset,
                                                          gpointer                  user_data);

GPasteGtkSettingsUiPanel *g_paste_gtk_settings_ui_panel_new (void);

G_END_DECLS
