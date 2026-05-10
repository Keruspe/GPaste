/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-item.h>

#include <string.h>

#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>

typedef struct
{
    gchar  *uuid;
    gchar  *value;
    GSList *special_values;
    gchar  *display_string;
    guint64 size;
} GPasteItemPrivate;

G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (Item, item, G_TYPE_OBJECT)

/**
 * g_paste_item_get_uuid:
 * @self: a #GPasteItem instance
 *
 * Get the uuid of the given item
 *
 * Returns: read-only string containing the uuid
 */
G_PASTE_VISIBLE const gchar *
g_paste_item_get_uuid (const GPasteItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_ITEM (self), NULL);

    const GPasteItemPrivate *priv = _g_paste_item_get_instance_private (self);

    return priv->uuid;
}

/**
 * g_paste_item_get_value:
 * @self: a #GPasteItem instance
 *
 * Get the value of the given item (text, uris or path to the image)
 *
 * Returns: read-only string containing the value
 */
G_PASTE_VISIBLE const gchar *
g_paste_item_get_value (const GPasteItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_ITEM (self), NULL);

    return _G_PASTE_ITEM_GET_CLASS (self)->get_value (self);
}

/**
 * g_paste_item_get_real_value:
 * @self: a #GPasteItem instance
 *
 * Get the real value of the given item (text, uris or path to the image)
 * This is different from get_value only for #GPastePasswordItem
 *
 * Returns: read-only string containing the real value
 */
G_PASTE_VISIBLE const gchar *
g_paste_item_get_real_value (const GPasteItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_ITEM (self), NULL);

    const GPasteItemPrivate *priv = _g_paste_item_get_instance_private (self);

    return priv->value;
}

/**
 * g_paste_item_get_special_values:
 * @self: a #GPasteItem instance
 *
 * Get the special values (special mime types) for an item
 *
 * Returns: (element-type GPasteSpecialValue): read-only list containing the special values
 */
G_PASTE_VISIBLE const GSList *
g_paste_item_get_special_values (const GPasteItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_ITEM (self), NULL);

    const GPasteItemPrivate *priv = _g_paste_item_get_instance_private (self);

    return priv->special_values;
}

/**
 * g_paste_item_get_special_value:
 * @self: a #GPasteItem instance
 * @atom: the value we want to get
 *
 * Get the special value (special mime type) for an item
 *
 * Returns: read-only special value
 */
G_PASTE_VISIBLE const gchar *
g_paste_item_get_special_value  (const GPasteItem *self,
                                 GPasteSpecialAtom atom)
{
    g_return_val_if_fail (_G_PASTE_IS_ITEM (self), NULL);

    const GPasteItemPrivate *priv = _g_paste_item_get_instance_private (self);

    for (GSList *sv = priv->special_values; sv; sv = sv->next)
    {
        GPasteSpecialValue *v = sv->data;
        if (v->mime == atom)
            return v->data;
    }

    return NULL;
}

/**
 * g_paste_item_get_display_string:
 * @self: a #GPasteItem instance
 *
 * Get the string we should use to display the #GPasteItem
 *
 * Returns: read-only display string
 */
G_PASTE_VISIBLE const gchar *
g_paste_item_get_display_string (const GPasteItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_ITEM (self), NULL);

    const GPasteItemPrivate *priv = _g_paste_item_get_instance_private (self);
    const gchar *display_string = priv->display_string;

    return (display_string) ? display_string : priv->value;
}

/**
 * g_paste_item_equals:
 * @self: a #GPasteItem instance
 * @other: another #GPasteItem instance
 *
 * Compare the two instances
 *
 * Returns: true if equals, false otherwise
 */
G_PASTE_VISIBLE gboolean
g_paste_item_equals (const GPasteItem *self,
                     const GPasteItem *other)
{
    g_return_val_if_fail (_G_PASTE_IS_ITEM (self), FALSE);
    g_return_val_if_fail (_G_PASTE_IS_ITEM (other), FALSE);

    if (self == other)
        return TRUE;

    return _G_PASTE_ITEM_GET_CLASS (self)->equals (self, other) && _G_PASTE_ITEM_GET_CLASS (other)->equals (other, self);
}

/**
 * g_paste_item_get_kind:
 * @self: a #GPasteItem instance
 *
 * Get the kind of #GPasteItem as string (for serialization)
 *
 * Returns: read-only string containing the kind of GPasteItem
 *          can be "Text", "Uris" or "Image"
 */
G_PASTE_VISIBLE const gchar *
g_paste_item_get_kind (const GPasteItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_ITEM (self), NULL);

    const GPasteItemClass *klass = _G_PASTE_ITEM_GET_CLASS (self);

    g_return_val_if_fail (klass->get_kind, NULL);

    return klass->get_kind (self);
}

/**
 * g_paste_item_get_size:
 * @self: a #GPasteItem instance
 *
 * Get the size of the #GPasteItem
 *
 * Returns: The size of its contents
 */
G_PASTE_VISIBLE guint64
g_paste_item_get_size (const GPasteItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_ITEM (self), 0);

    const GPasteItemPrivate *priv = _g_paste_item_get_instance_private (self);

    return priv->size;
}

/**
 * g_paste_item_set_size:
 * @self: a #GPasteItem instance
 * @size: the new size
 *
 * Set the size of the item
 */
G_PASTE_VISIBLE void
g_paste_item_set_size (GPasteItem *self,
                       guint64     size)
{
    g_return_if_fail (_G_PASTE_IS_ITEM (self));

    GPasteItemPrivate *priv = g_paste_item_get_instance_private (self);

    priv->size = size;
}

/**
 * g_paste_item_add_size:
 * @self: a #GPasteItem instance
 * @size: the size to add
 *
 * Add some size of the item
 */
G_PASTE_VISIBLE void
g_paste_item_add_size (GPasteItem *self,
                       guint64     size)
{
    g_return_if_fail (_G_PASTE_IS_ITEM (self));

    GPasteItemPrivate *priv = g_paste_item_get_instance_private (self);

    priv->size += size;
}

/**
 * g_paste_item_remove_size:
 * @self: a #GPasteItem instance
 * @size: the size to remove
 *
 * Remove some size from the item
 */
G_PASTE_VISIBLE void
g_paste_item_remove_size (GPasteItem *self,
                          guint64     size)
{
    g_return_if_fail (_G_PASTE_IS_ITEM (self));

    GPasteItemPrivate *priv = g_paste_item_get_instance_private (self);

    g_return_if_fail (priv->size >= size);

    priv->size -= size;
}

/**
 * g_paste_item_set_display_string:
 * @self: a #GPasteItem instance
 * @display_string: the new display string
 *
 * Set the string to display
 */
G_PASTE_VISIBLE void
g_paste_item_set_display_string (GPasteItem  *self,
                                 const gchar *display_string)
{
    g_return_if_fail (_G_PASTE_IS_ITEM (self));

    GPasteItemPrivate *priv = g_paste_item_get_instance_private (self);

    if (priv->display_string)
    {
        priv->size -= (strlen (priv->display_string) + 1);
        g_free (priv->display_string);
    }

    if (display_string)
    {
        priv->display_string = g_strdup (display_string);
        priv->size += strlen (display_string) + 1;
    }
    else
        priv->display_string = NULL;
}

/**
 * g_paste_item_add_special_value:
 * @self: a #GPasteItem instance
 * @special_value: the special value
 *
 * Add the special values (special mime types) for an item
 */
G_PASTE_VISIBLE void
g_paste_item_add_special_value (GPasteItem               *self,
                                const GPasteSpecialValue *special_value)
{
    g_return_if_fail (_G_PASTE_IS_ITEM (self));

    GPasteItemPrivate *priv = g_paste_item_get_instance_private (self);
    GPasteSpecialValue *gsv = g_new (GPasteSpecialValue, 1);

    gsv->mime = special_value->mime;
    gsv->data = g_strdup (special_value->data);

    priv->special_values = g_slist_prepend (priv->special_values, gsv);
    priv->size += strlen (gsv->data);
}

/**
 * g_paste_item_set_state:
 * @self: a #GPasteItem instance
 * @state: a #GPasteItemState
 *
 * Set whether this item is Active or Idle
 */
G_PASTE_VISIBLE void
g_paste_item_set_state (GPasteItem     *self,
                        GPasteItemState state)
{
    g_return_if_fail (_G_PASTE_IS_ITEM (self));

    g_debug ("item: set state: %d", state);

    G_PASTE_ITEM_GET_CLASS (self)->set_state (self, state);
}

/**
 * g_paste_item_set_uuid:
 * @self: a #GPasteItem instance
 * @uuid: the new uuid
 *
 * Set the uuid of the item
 */
G_PASTE_VISIBLE void
g_paste_item_set_uuid (GPasteItem  *self,
                       const gchar *uuid)
{
    g_return_if_fail (_G_PASTE_IS_ITEM (self));
    g_return_if_fail (g_uuid_string_is_valid (uuid));

    GPasteItemPrivate *priv = g_paste_item_get_instance_private (self);

    g_free (priv->uuid);
    priv->uuid = g_strdup (uuid);
}

static void
g_paste_item_finalize (GObject *object)
{
    const GPasteItem *self = _G_PASTE_ITEM (object);
    const GPasteItemPrivate *priv = _g_paste_item_get_instance_private (self);

    g_free (priv->uuid);
    if (_G_PASTE_ITEM_GET_CLASS (self)->secure (self))
        gcr_secure_memory_strfree (priv->value);
    else
        g_free (priv->value);
    g_free (priv->display_string);

    for (GSList *sv = priv->special_values; sv; sv = sv->next)
    {
        GPasteSpecialValue *gsv = sv->data;
        g_free (gsv->data);
        g_free (gsv);
    }

    g_slist_free (priv->special_values);

    G_OBJECT_CLASS (g_paste_item_parent_class)->finalize (object);
}

static gboolean
g_paste_item_default_equals (const GPasteItem *self,
                             const GPasteItem *other)
{
    if (self == other)
        return TRUE;

    const GPasteItemPrivate *priv = _g_paste_item_get_instance_private (self);
    const GPasteItemPrivate *_priv = _g_paste_item_get_instance_private (other);

    return g_paste_str_equal (priv->value, _priv->value);
}

static void
g_paste_item_default_set_state (GPasteItem     *self  G_GNUC_UNUSED,
                                GPasteItemState state G_GNUC_UNUSED)
{
}

static gboolean
g_paste_item_default_secure (const GPasteItem *self G_GNUC_UNUSED)
{
    return FALSE;
}

static void
g_paste_item_class_init (GPasteItemClass *klass)
{
    klass->equals = g_paste_item_default_equals;
    klass->get_value = g_paste_item_get_real_value;
    klass->get_kind = NULL;
    klass->set_state = g_paste_item_default_set_state;
    klass->secure = g_paste_item_default_secure;

    G_OBJECT_CLASS (klass)->finalize = g_paste_item_finalize;
}

static void
g_paste_item_init (GPasteItem *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_item_new:
 * @type: the type of the subclass to instantiate
 * @value: the value of the item
 *
 * Create a new instance of #GPasteItem
 *
 * Returns: a newly allocated #GPasteItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_item_new (GType        type,
                  const gchar *value)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_ITEM), NULL);
    g_return_val_if_fail (value, NULL);

    GPasteItem *self = g_object_new (type, NULL);
    GPasteItemPrivate *priv = g_paste_item_get_instance_private (self);

    priv->uuid = g_uuid_string_random ();
    priv->value = (_G_PASTE_ITEM_GET_CLASS (self)->secure (self)) ? gcr_secure_memory_strdup (value) : g_strdup (value);
    priv->display_string = NULL;

    priv->size = strlen (priv->value) + 1;

    return self;
}
