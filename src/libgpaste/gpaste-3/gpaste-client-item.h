// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-3/gpaste-item-enums.h>
#include <gpaste-3/gpaste-macros.h>

G_BEGIN_DECLS

/* How an item travels: uuid, value, kind, favourite. Declared in
 * data/dbus/org.gnome.GPaste3.xml, which is the contract; these are the same
 * thing spelled for the C that builds and reads it, so that the daemon's
 * builder and the client's parser cannot come to disagree. */
#define G_PASTE_ITEM_VARIANT_STRING  "(ssub)"
#define G_PASTE_ITEMS_VARIANT_STRING "a" G_PASTE_ITEM_VARIANT_STRING

#define G_PASTE_ITEM_VARIANT_TYPE  G_VARIANT_TYPE (G_PASTE_ITEM_VARIANT_STRING)
#define G_PASTE_ITEMS_VARIANT_TYPE G_VARIANT_TYPE (G_PASTE_ITEMS_VARIANT_STRING)

#define G_PASTE_TYPE_CLIENT_ITEM (g_paste_client_item_get_type ())

G_PASTE_FINAL_TYPE (ClientItem, client_item, CLIENT_ITEM, GObject)

const gchar   *g_paste_client_item_get_uuid     (GPasteClientItem *self);
const gchar   *g_paste_client_item_get_value    (GPasteClientItem *self);
GPasteItemKind g_paste_client_item_get_kind     (GPasteClientItem *self);
gboolean       g_paste_client_item_is_favourite (GPasteClientItem *self);

GPasteClientItem *g_paste_client_item_new (const gchar   *uuid,
                                           const gchar   *value,
                                           GPasteItemKind kind,
                                           gboolean       favourite);

G_END_DECLS
