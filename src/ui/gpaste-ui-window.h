// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste/gpaste-macros.h>

#include <adwaita.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_WINDOW (g_paste_ui_window_get_type ())

G_PASTE_FINAL_TYPE (UiWindow, ui_window, UI_WINDOW, AdwApplicationWindow)

void g_paste_ui_window_empty_history (GPasteUiWindow *self,
                                      const gchar    *history);
void g_paste_ui_window_search        (GPasteUiWindow *self,
                                      const gchar    *search);
void g_paste_ui_window_show_prefs    (GPasteUiWindow *self);

GtkWidget *g_paste_ui_window_new (GtkApplication *app);

G_END_DECLS

