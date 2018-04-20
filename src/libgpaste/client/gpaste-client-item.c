/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-client-item.h>

struct _GPasteClientItem
{
    GObject parent_instance;
};

typedef struct
{
    gchar *uuid;
    gchar *value;
} GPasteClientItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (ClientItem, client_item, G_TYPE_OBJECT)

/**
 * g_paste_client_item_get_uuid:
 * @self: a #GPasteClientItem instance
 *
 * Returns the uuid of the item
 */
G_PASTE_VISIBLE const gchar *
g_paste_client_item_get_uuid (const GPasteClientItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT_ITEM (self), NULL);

    const GPasteClientItemPrivate *priv = _g_paste_client_item_get_instance_private (self);

    return priv->uuid;;
}

/**
 * g_paste_client_item_get_value:
 * @self: a #GPasteClientItem instance
 *
 * Returns the value of the item
 */
G_PASTE_VISIBLE const gchar *
g_paste_client_item_get_value (const GPasteClientItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT_ITEM (self), NULL);

    const GPasteClientItemPrivate *priv = _g_paste_client_item_get_instance_private (self);

    return priv->value;
}

static void
g_paste_client_item_finalize (GObject *object)
{
    GPasteClientItemPrivate *priv = g_paste_client_item_get_instance_private (G_PASTE_CLIENT_ITEM (object));

    g_free (priv->uuid);
    g_free (priv->value);
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
 *
 * Create a new instance of #GPasteClientItem
 *
 * Returns: (transfer full): a newly allocated #GPasteClientItem
 *                           free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteClientItem *
g_paste_client_item_new (const gchar *uuid,
                         const gchar *value)
{
    g_return_val_if_fail (g_uuid_string_is_valid (uuid), NULL);
    g_return_val_if_fail (g_utf8_validate (value, -1, NULL), NULL);

    GPasteClientItem *self = g_object_new (G_PASTE_TYPE_CLIENT_ITEM, NULL);
    GPasteClientItemPrivate *priv = g_paste_client_item_get_instance_private (self);

    priv->uuid = g_strdup (uuid);
    priv->value = g_strdup (value);

    return self;
}
