// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-client-item.h>
#include <gpaste-3/gpaste-util.h>

struct _GPasteClientItem
{
    GObject parent_instance;

    gchar         *uuid;
    gchar         *value;
    GPasteItemKind kind;
    gboolean       favourite;
    /* Always empty: see G_PASTE_ITEM_VARIANT_STRING. Kept as a strv rather than
     * left %NULL so that a caller walking it needs no special case for the item
     * that has none, which is currently every item. */
    GStrv          notes;

    /* Composed on demand from @kind and @value, then kept: a row is redrawn far
     * more often than an item is built. */
    gchar         *display_string;
};

G_PASTE_DEFINE_TYPE (ClientItem, client_item, G_TYPE_OBJECT)

/**
 * g_paste_client_item_get_uuid:
 * @self: a #GPasteClientItem instance
 *
 * Returns the uuid of the item
 */
G_PASTE_VISIBLE const gchar *
g_paste_client_item_get_uuid (GPasteClientItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT_ITEM (self), NULL);

    return self->uuid;
}

/**
 * g_paste_client_item_get_value:
 * @self: a #GPasteClientItem instance
 *
 * Returns the value of the item
 */
G_PASTE_VISIBLE const gchar *
g_paste_client_item_get_value (GPasteClientItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT_ITEM (self), NULL);

    return self->value;
}

/**
 * g_paste_client_item_get_display_string:
 * @self: a #GPasteClientItem instance
 *
 * Get the string to draw for this item: its value, with the decoration its kind
 * calls for around it, as g_paste_util_display_string () composes it. Kept once
 * composed, since a row is redrawn far more often than an item is built.
 *
 * Returns: read-only display string, owned by the item
 */
G_PASTE_VISIBLE const gchar *
g_paste_client_item_get_display_string (GPasteClientItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT_ITEM (self), NULL);

    if (!self->display_string)
        self->display_string = g_paste_util_display_string (self->value, self->kind);

    return self->display_string;
}

/**
 * g_paste_client_item_get_kind:
 * @self: a #GPasteClientItem instance
 *
 * Returns the kind of the item
 */
G_PASTE_VISIBLE GPasteItemKind
g_paste_client_item_get_kind (GPasteClientItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT_ITEM (self), G_PASTE_ITEM_KIND_INVALID);

    return self->kind;
}

/**
 * g_paste_client_item_is_favourite:
 * @self: a #GPasteClientItem instance
 *
 * Returns whether the item is pinned, and so exempt from the history's
 * automatic eviction policies
 */
G_PASTE_VISIBLE gboolean
g_paste_client_item_is_favourite (GPasteClientItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT_ITEM (self), FALSE);

    return self->favourite;
}

/**
 * g_paste_client_item_get_notes:
 * @self: a #GPasteClientItem instance
 *
 * Get the notes attached to the item
 *
 * There are none: the notes are a slot reserved on the wire, and nothing
 * attaches any yet. The array is empty rather than %NULL, so a caller drawing
 * them walks it without a case of its own.
 *
 * Returns: (array zero-terminated=1) (transfer none): read-only, owned by the
 *          item
 */
G_PASTE_VISIBLE const gchar * const *
g_paste_client_item_get_notes (GPasteClientItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT_ITEM (self), NULL);

    return (const gchar * const *) self->notes;
}

static void
g_paste_client_item_finalize (GObject *object)
{
    GPasteClientItem *self = G_PASTE_CLIENT_ITEM (object);

    g_free (self->uuid);
    g_free (self->value);
    g_free (self->display_string);
    g_strfreev (self->notes);

    G_OBJECT_CLASS (g_paste_client_item_parent_class)->finalize (object);
}

static void
g_paste_client_item_class_init (GPasteClientItemClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = g_paste_client_item_finalize;
}

static void
g_paste_client_item_init (GPasteClientItem *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_client_item_new:
 * @uuid: the uuid of the item
 * @value: the value of the item
 * @kind: the kind of the item
 * @favourite: whether the item is pinned
 * @notes: (array zero-terminated=1) (nullable): the notes attached to the item,
 *         of which there are none yet, %NULL being the same as an empty array
 *
 * Create a new instance of #GPasteClientItem
 *
 * Returns: (transfer full): a newly allocated #GPasteClientItem
 *                           free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClientItem *
g_paste_client_item_new (const gchar         *uuid,
                         const gchar         *value,
                         GPasteItemKind       kind,
                         gboolean             favourite,
                         const gchar * const *notes)
{
    g_return_val_if_fail (g_uuid_string_is_valid (uuid), NULL);
    g_return_val_if_fail (g_utf8_validate (value, -1, NULL), NULL);

    GPasteClientItem *self = g_object_new (G_PASTE_TYPE_CLIENT_ITEM, NULL);

    self->uuid = g_strdup (uuid);
    self->value = g_strdup (value);
    self->kind = kind;
    self->favourite = favourite;
    /* g_strdupv () of nothing is nothing, and the getter promises an array. */
    self->notes = (notes) ? g_strdupv ((GStrv) notes) : g_new0 (gchar *, 1);

    return self;
}
