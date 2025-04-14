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

#define G_PASTE_TYPE_GTK_PREFERENCES_DIALOG (g_paste_gtk_preferences_dialog_get_type ())

G_PASTE_GTK_FINAL_TYPE (PreferencesDialog, preferences_dialog, PREFERENCES_DIALOG, AdwPreferencesDialog)

AdwDialog *g_paste_gtk_preferences_dialog_new (GApplication *gapp);

G_END_DECLS
