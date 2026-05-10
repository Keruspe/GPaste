/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste-text-item.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_URIS_ITEM (g_paste_uris_item_get_type ())

G_PASTE_FINAL_TYPE (UrisItem, uris_item, URIS_ITEM, GPasteTextItem)

const gchar * const *g_paste_uris_item_get_uris (const GPasteUrisItem *self);

GPasteItem *g_paste_uris_item_new (const gchar *uris);

G_END_DECLS
