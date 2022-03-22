/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK4_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk4.h> can be included directly."
#endif

#pragma once

#include <gpaste-gtk4/gpaste-gtk-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_GTK_PREFERENCES_GROUP (g_paste_gtk_preferences_group_get_type ())

G_PASTE_GTK_FINAL_TYPE (PreferencesGroup, preferences_group, PREFERENCES_GROUP, AdwPreferencesGroup)

typedef void (*GPasteGtkBooleanCallback) (GPasteSettings *settings,
                                          gboolean        data);
typedef void (*GPasteGtkRangeCallback)   (GPasteSettings *settings,
                                          guint64         data);
typedef void (*GPasteGtkTextCallback)    (GPasteSettings *settings,
                                          const gchar    *data);
typedef void (*GPasteGtkResetCallback)   (GPasteSettings *settings);

GtkSwitch *g_paste_gtk_preferences_group_add_boolean_setting (GPasteGtkPreferencesGroup *self,
                                                              const gchar               *label,
                                                              gboolean                   value,
                                                              GPasteGtkBooleanCallback   on_value_changed,
                                                              GPasteGtkResetCallback     on_reset,
                                                              GPasteSettings            *settings);
GtkSpinButton *g_paste_gtk_preferences_group_add_range_setting (GPasteGtkPreferencesGroup *self,
                                                                const gchar               *label,
                                                                gdouble                    value,
                                                                gdouble                    min,
                                                                gdouble                    max,
                                                                gdouble                    step,
                                                                GPasteGtkRangeCallback     on_value_changed,
                                                                GPasteGtkResetCallback     on_reset,
                                                                GPasteSettings            *settings);
GtkEntryBuffer *g_paste_gtk_preferences_group_add_text_setting (GPasteGtkPreferencesGroup *self,
                                                                const gchar               *label,
                                                                const gchar               *value,
                                                                GPasteGtkTextCallback      on_value_changed,
                                                                GPasteGtkResetCallback     on_reset,
                                                                GPasteSettings            *settings);

GPasteGtkPreferencesGroup *g_paste_gtk_preferences_group_new (const gchar *title);

G_END_DECLS
