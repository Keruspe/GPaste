/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_URIS_ITEM_H__
#define __G_PASTE_URIS_ITEM_H__

#include <gpaste-text-item.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_URIS_ITEM (g_paste_uris_item_get_type ())

G_PASTE_FINAL_TYPE (UrisItem, uris_item, URIS_ITEM, GPasteTextItem)

const gchar * const *g_paste_uris_item_get_uris (const GPasteUrisItem *self);

GPasteItem *g_paste_uris_item_new (const gchar *uris);

G_END_DECLS

#endif /*__G_PASTE_URIS_ITEM_H__*/
