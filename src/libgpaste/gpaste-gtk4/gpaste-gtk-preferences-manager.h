/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK4_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk4.h> can be included directly."
#endif

#pragma once

#include <gpaste-gtk4/gpaste-gtk-preferences-page.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_GTK_PREFERENCES_MANAGER (g_paste_gtk_preferences_manager_get_type ())

G_PASTE_GTK_FINAL_TYPE (PreferencesManager, preferences_manager, PREFERENCES_MANAGER, GObject)

GPasteSettings *g_paste_gtk_preferences_manager_get_settings (GPasteGtkPreferencesManager *self);

void g_paste_gtk_preferences_manager_register (GPasteGtkPreferencesManager *self,
                                               GPasteGtkPreferencesPage    *page);

void g_paste_gtk_preferences_manager_deregister (GPasteGtkPreferencesManager *self,
                                                 GPasteGtkPreferencesPage    *page);

GPasteGtkPreferencesManager *g_paste_gtk_preferences_manager_new (void);

G_END_DECLS
