// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-item.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_COLOR_ITEM (g_paste_color_item_get_type ())

G_PASTE_FINAL_TYPE (ColorItem, color_item, COLOR_ITEM, GPasteItem)

const GdkRGBA *g_paste_color_item_get_rgba (const GPasteColorItem *self);

GPasteItem *g_paste_color_item_new          (const GdkRGBA *rgba);
GPasteItem *g_paste_color_item_new_from_str (const gchar   *str);

G_END_DECLS
