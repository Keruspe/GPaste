/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-password-item.h>

#include <string.h>

struct _GPastePasswordItem
{
    GPasteTextItem parent_instance;
};

typedef struct
{
    gchar *name;
} GPastePasswordItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (PasswordItem, password_item, G_PASTE_TYPE_TEXT_ITEM)

/**
 * g_paste_password_item_get_name:
 * @self: a #GPastePasswordItem instance
 *
 * Get the name of the given item
 *
 * Returns: read-only string containing the name
 */
G_PASTE_VISIBLE const gchar *
g_paste_password_item_get_name (const GPastePasswordItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_PASSWORD_ITEM (self), NULL);

    const GPastePasswordItemPrivate *priv = _g_paste_password_item_get_instance_private (self);

    return priv->name;
}

/**
 * g_paste_password_item_set_name:
 * @self: a #GPastePasswordItem instance
 * @name: (nullable): the new name
 *
 * Set the name of the given item
 */
G_PASTE_VISIBLE void
g_paste_password_item_set_name (GPastePasswordItem *self,
                                const gchar        *name)
{
    g_return_if_fail (_G_PASTE_IS_PASSWORD_ITEM (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    GPastePasswordItemPrivate *priv = g_paste_password_item_get_instance_private (self);

    if (!name)
        name = "******";

    GPasteItem *item = G_PASTE_ITEM (self);

    g_paste_item_add_size (item, strlen (name) - ((priv->name) ? strlen(priv->name) : 0));
    g_free (priv->name);
    priv->name = g_strdup (name);

    // This is the prefix displayed in history to identify a password
    g_autofree gchar *full_display_string = g_strdup_printf ("[%s] %s", _("Password"), name);
    g_paste_item_set_display_string (item, full_display_string);
}

static const gchar *
g_paste_password_item_get_value (const GPasteItem *self G_GNUC_UNUSED)
{
    return "******";
}

static const gchar *
g_paste_password_item_get_kind (const GPasteItem *self G_GNUC_UNUSED)
{
    return "Password";
}

static gboolean
g_paste_password_item_equals (const GPasteItem *self,
                              const GPasteItem *other)
{
    g_return_val_if_fail (_G_PASTE_IS_PASSWORD_ITEM (self), FALSE);
    g_return_val_if_fail (_G_PASTE_IS_ITEM (other), FALSE);

    /* Passwords are never considered equals, except when it's the exact same object */
    return FALSE;
}

static gboolean
g_paste_password_item_secure (const GPasteItem *self G_GNUC_UNUSED)
{
    return TRUE;
}

static void
g_paste_password_item_finalize (GObject *object)
{
    const GPastePasswordItemPrivate *priv = _g_paste_password_item_get_instance_private (G_PASTE_PASSWORD_ITEM (object));

    g_free (priv->name);

    G_OBJECT_CLASS (g_paste_password_item_parent_class)->finalize (object);
}


static void
g_paste_password_item_class_init (GPastePasswordItemClass *klass)
{
    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);

    item_class->get_kind = g_paste_password_item_get_kind;
    item_class->get_value = g_paste_password_item_get_value;
    item_class->equals = g_paste_password_item_equals;
    item_class->secure = g_paste_password_item_secure;

    G_OBJECT_CLASS (klass)->finalize = g_paste_password_item_finalize;
}

static void
g_paste_password_item_init (GPastePasswordItem *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_password_item_new:
 * @name: (nullable): the name used to identify the password
 * @password: the content of the desired #GPastePasswordItem
 *
 * Create a new instance of #GPastePasswordItem
 *
 * Returns: a newly allocated #GPastePasswordItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_password_item_new (const gchar *name,
                           const gchar *password)
{
    g_return_val_if_fail (password, NULL);
    g_return_val_if_fail (g_utf8_validate (password, -1, NULL), NULL);
    g_return_val_if_fail (!name || g_utf8_validate (name, -1, NULL), NULL);

    GPasteItem *self = g_paste_item_new (G_PASTE_TYPE_PASSWORD_ITEM, password);

    /* override password value length */
    g_paste_item_set_size (self, 0);
    g_paste_password_item_set_name (G_PASTE_PASSWORD_ITEM (self), name);

    return self;
}
