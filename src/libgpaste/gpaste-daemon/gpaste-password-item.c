// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-password-item.h>

#include <string.h>

struct _GPastePasswordItem
{
    GPasteItem parent_instance;

    gchar *name;
};

G_PASTE_DEFINE_TYPE (PasswordItem, password_item, G_PASTE_TYPE_ITEM)

/**
 * g_paste_password_item_get_name:
 * @self: a #GPastePasswordItem instance
 *
 * Get the name of the given item
 *
 * Returns: read-only string containing the name
 */
G_PASTE_VISIBLE const gchar *
g_paste_password_item_get_name (GPastePasswordItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_PASSWORD_ITEM (self), NULL);

    return self->name;
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
    g_return_if_fail (G_PASTE_IS_PASSWORD_ITEM (self));
    g_return_if_fail (!name || g_utf8_validate (name, -1, NULL));

    if (!name)
        name = "******";

    GPasteItem *item = G_PASTE_ITEM (self);

    if (self->name)
        g_paste_item_remove_size (item, strlen (self->name));
    g_paste_item_add_size (item, strlen (name));
    g_set_str (&self->name, name);

    // This is the prefix displayed in history to identify a password
    g_autofree gchar *full_display_string = g_strdup_printf ("[%s] %s", _("Password"), name);
    g_paste_item_set_display_string (item, g_steal_pointer (&full_display_string));
}

static const gchar *
g_paste_password_item_get_value (GPasteItem *self G_GNUC_UNUSED)
{
    return "******";
}

static GPasteItemKind
g_paste_password_item_get_kind (GPasteItem *self G_GNUC_UNUSED)
{
    return G_PASTE_ITEM_KIND_PASSWORD;
}

static gboolean
g_paste_password_item_equals (GPasteItem *self,
                              GPasteItem *other)
{
    g_return_val_if_fail (G_PASTE_IS_PASSWORD_ITEM (self), FALSE);
    g_return_val_if_fail (G_PASTE_IS_ITEM (other), FALSE);

    /* Passwords are never considered equals, except when it's the exact same object */
    return FALSE;
}

static gboolean
g_paste_password_item_secure (GPasteItem *self G_GNUC_UNUSED)
{
    return TRUE;
}

static void
g_paste_password_item_finalize (GObject *object)
{
    GPastePasswordItem *self = G_PASTE_PASSWORD_ITEM (object);

    g_free (self->name);

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
