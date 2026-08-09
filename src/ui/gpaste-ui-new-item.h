// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-client.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_NEW_ITEM (g_paste_ui_new_item_get_type ())

G_PASTE_FINAL_TYPE (UiNewItem, ui_new_item, UI_NEW_ITEM, GtkButton)

GtkWidget *g_paste_ui_new_item_new (GtkWindow    *rootwin,
                                    GPasteClient *client);

G_END_DECLS

