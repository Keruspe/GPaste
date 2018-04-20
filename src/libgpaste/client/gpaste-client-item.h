/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_CLIENT_ITEM_H__
#define __G_PASTE_CLIENT_ITEM_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIENT_ITEM (g_paste_client_item_get_type ())

G_PASTE_FINAL_TYPE (ClientItem, client_item, CLIENT_ITEM, GObject)

const gchar *g_paste_client_item_get_uuid  (const GPasteClientItem *self);
const gchar *g_paste_client_item_get_value (const GPasteClientItem *self);

GPasteClientItem *g_paste_client_item_new (const gchar *uuid,
                                           const gchar *value);

G_END_DECLS

#endif /*__G_PASTE_CLIENT_ITEM_H__*/
