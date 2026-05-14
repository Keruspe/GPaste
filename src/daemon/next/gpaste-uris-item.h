/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste-text-item.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_URIS_ITEM (g_paste_uris_item_get_type ())

G_PASTE_FINAL_TYPE (UrisItem, uris_item, URIS_ITEM, GPasteTextItem)

GdkFileList *g_paste_uris_item_get_file_list (const GPasteUrisItem *self);

GPasteItem  *g_paste_uris_item_new          (GdkFileList *file_list);
GPasteItem  *g_paste_uris_item_new_from_str (const gchar *str);

G_END_DECLS
