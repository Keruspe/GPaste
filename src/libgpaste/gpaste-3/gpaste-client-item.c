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

static void
g_paste_client_item_finalize (GObject *object)
{
    GPasteClientItem *self = G_PASTE_CLIENT_ITEM (object);

    g_free (self->uuid);
    g_free (self->value);
    g_free (self->display_string);

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
 *
 * Create a new instance of #GPasteClientItem
 *
 * Returns: (transfer full): a newly allocated #GPasteClientItem
 *                           free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClientItem *
g_paste_client_item_new (const gchar   *uuid,
                         const gchar   *value,
                         GPasteItemKind kind,
                         gboolean       favourite)
{
    g_return_val_if_fail (g_uuid_string_is_valid (uuid), NULL);
    g_return_val_if_fail (g_utf8_validate (value, -1, NULL), NULL);

    GPasteClientItem *self = g_object_new (G_PASTE_TYPE_CLIENT_ITEM, NULL);

    self->uuid = g_strdup (uuid);
    self->value = g_strdup (value);
    self->kind = kind;
    self->favourite = favourite;

    return self;
}
