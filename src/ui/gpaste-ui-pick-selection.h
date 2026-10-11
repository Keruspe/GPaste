// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-macros.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_PICK_SELECTION (g_paste_ui_pick_selection_get_type ())

G_PASTE_FINAL_TYPE (UiPickSelection, ui_pick_selection, UI_PICK_SELECTION, GObject)

void g_paste_ui_pick_selection_set_picking (GPasteUiPickSelection *self,
                                            gboolean               picking);

GPasteUiPickSelection *g_paste_ui_pick_selection_new (GListModel *model);

G_END_DECLS
