// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste/gpaste-macros.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_SETTINGS (g_paste_ui_settings_get_type ())

G_PASTE_FINAL_TYPE (UiSettings, ui_settings, UI_SETTINGS, GtkButton)

GtkWidget *g_paste_ui_settings_new (void);

G_END_DECLS

