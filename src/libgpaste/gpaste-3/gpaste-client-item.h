// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-3/gpaste-item-enums.h>
#include <gpaste-3/gpaste-macros.h>

G_BEGIN_DECLS

/* How an item travels: uuid, value, kind, favourite, notes. Declared in
 * data/dbus/org.gnome.GPaste3.xml, which is the contract; these are the same
 * thing spelled for the C that builds and reads it, so that the daemon's
 * builder and the client's parser cannot come to disagree.
 *
 * The notes are a reservation, not a feature: the array is always empty, and
 * nothing in the tree writes to it or reads anything out of it. It is here
 * because a note is something a row draws, which is what puts it in the struct
 * rather than behind a getter of its own -- and every client would have to be
 * rebuilt for the day it arrives. Spending that break once, on a release that
 * is breaking the interface anyway, costs less than spending it twice. */
#define G_PASTE_ITEM_VARIANT_STRING  "(ssubas)"
#define G_PASTE_ITEMS_VARIANT_STRING "a" G_PASTE_ITEM_VARIANT_STRING

/* The same shape as a g_variant_new() / g_variant_get() format string. "^as"
 * is the "as" above plus the convenience conversion to and from a GStrv, which
 * makes it no longer a *type* string: G_VARIANT_TYPE cannot be given this one,
 * and g_variant_new/g_variant_get cannot be given the other without handing
 * them a builder and an iterator instead. Hence the pair, kept adjacent so that
 * changing the shape means changing both. */
#define G_PASTE_ITEM_VARIANT_FORMAT "(ssub^as)"

#define G_PASTE_ITEM_VARIANT_TYPE  G_VARIANT_TYPE (G_PASTE_ITEM_VARIANT_STRING)
#define G_PASTE_ITEMS_VARIANT_TYPE G_VARIANT_TYPE (G_PASTE_ITEMS_VARIANT_STRING)

#define G_PASTE_TYPE_CLIENT_ITEM (g_paste_client_item_get_type ())

G_PASTE_FINAL_TYPE (ClientItem, client_item, CLIENT_ITEM, GObject)

const gchar   *g_paste_client_item_get_uuid           (GPasteClientItem *self);
const gchar   *g_paste_client_item_get_value          (GPasteClientItem *self);
const gchar   *g_paste_client_item_get_display_string (GPasteClientItem *self);
GPasteItemKind g_paste_client_item_get_kind           (GPasteClientItem *self);
gboolean       g_paste_client_item_is_favourite       (GPasteClientItem *self);

const gchar * const *g_paste_client_item_get_notes (GPasteClientItem *self);

GPasteClientItem *g_paste_client_item_new (const gchar         *uuid,
                                           const gchar         *value,
                                           GPasteItemKind       kind,
                                           gboolean             favourite,
                                           const gchar * const *notes);

G_END_DECLS
