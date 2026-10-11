// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-settings.h>

#include <adwaita.h>

G_BEGIN_DECLS

/* The window's accelerators, spelled once for the window that registers them
 * and the dialog that lists them (window_shortcuts[] in
 * gpaste-ui-shortcuts-window.c), as the history's context-menu keys are
 * (G_PASTE_UI_HISTORY_CONTEXT_MENU_ACCELERATORS). */
#define G_PASTE_UI_SEARCH_ACCELERATOR       "<primary>f"
#define G_PASTE_UI_NEW_ITEM_ACCELERATOR     "<primary>n"
#define G_PASTE_UI_NEW_PASSWORD_ACCELERATOR "<primary><shift>n"
#define G_PASTE_UI_PREFERENCES_ACCELERATOR  "<primary>comma"
#define G_PASTE_UI_SHORTCUTS_ACCELERATOR    "<primary>question"
#define G_PASTE_UI_CLOSE_ACCELERATOR        "<primary>w"

GtkWidget *g_paste_ui_shortcuts_window_new (GPasteSettings *settings);

G_END_DECLS

