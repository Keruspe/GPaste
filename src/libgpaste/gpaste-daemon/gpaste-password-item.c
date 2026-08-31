// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-password-item.h>

#include <string.h>

struct _GPastePasswordItem
{
    GPasteItem parent_instance;

    gchar *name;
    guint  timeout;
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
        name = G_PASTE_PASSWORD_ITEM_NO_NAME;

    GPasteItem *item = G_PASTE_ITEM (self);

    if (self->name)
        g_paste_item_remove_size (item, strlen (self->name));
    g_paste_item_add_size (item, strlen (name));
    g_set_str (&self->name, name);

    /* The name alone, never the password: it is what get_value () masks as
     * G_PASTE_PASSWORD_ITEM_NO_NAME and what get_real_value () holds. The
     * "[Password]" a user reads in front of it is the drawing client's to add. */
    g_paste_item_set_display_string (item, g_strdup (name));
}

/**
 * g_paste_password_item_get_timeout:
 * @self: a #GPastePasswordItem instance
 *
 * Get how long the password may stay on the clipboard once it is the active
 * item, in seconds
 *
 * Returns: the timeout in seconds, 0 when the password has none
 */
G_PASTE_VISIBLE guint
g_paste_password_item_get_timeout (GPastePasswordItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_PASSWORD_ITEM (self), 0);

    return self->timeout;
}

/**
 * g_paste_password_item_set_timeout:
 * @self: a #GPastePasswordItem instance
 * @timeout: the new timeout, in seconds, or 0 for none
 *
 * Set how long the password may stay on the clipboard once it is the active item
 *
 * Capped at %G_PASTE_PASSWORD_TIMEOUT_MAX, here rather than in each of the
 * callers -- a D-Bus method, a keyboard shortcut and both storage backends
 * reading one back -- since it is the item the range is a fact about.
 *
 * The countdown itself belongs to #GPasteClipboardsManager, which arms it every
 * time the item is selected; this only says how long it runs for.
 */
G_PASTE_VISIBLE void
g_paste_password_item_set_timeout (GPastePasswordItem *self,
                                   guint               timeout)
{
    g_return_if_fail (G_PASTE_IS_PASSWORD_ITEM (self));

    self->timeout = MIN (timeout, G_PASTE_PASSWORD_TIMEOUT_MAX);
}

static const gchar *
g_paste_password_item_get_value (GPasteItem *self G_GNUC_UNUSED)
{
    return G_PASTE_PASSWORD_ITEM_NO_NAME;
}

static GPasteItemKind
g_paste_password_item_get_kind (GPasteItem *self G_GNUC_UNUSED)
{
    return G_PASTE_ITEM_KIND_PASSWORD;
}

/* A named password is the user's own record of a secret, and two records are two
 * items whatever they hold: naming one is what says it is worth keeping apart,
 * so those never match. Nameless ones are a secret read off a selection, and the
 * same secret read twice -- a password manager copied from again -- is one
 * exposure the history has already got: telling those apart fills it with
 * placeholders nothing distinguishes, since the value they would be told apart
 * by is the one nothing may display.
 *
 * Symmetric, as g_paste_item_equals() dispatches on @self alone: a password and
 * anything else differ in kind either way round. The two being the same object
 * is not answered here either, g_paste_item_equals() having answered it before
 * it dispatched. */
static gboolean
g_paste_password_item_equals (GPasteItem *self,
                              GPasteItem *other)
{
    g_return_val_if_fail (G_PASTE_IS_PASSWORD_ITEM (self), FALSE);
    g_return_val_if_fail (G_PASTE_IS_ITEM (other), FALSE);

    if (!G_PASTE_IS_PASSWORD_ITEM (other))
        return FALSE;

    if (!g_paste_str_equal (G_PASTE_PASSWORD_ITEM (self)->name, G_PASTE_PASSWORD_ITEM_NO_NAME) ||
        !g_paste_str_equal (G_PASTE_PASSWORD_ITEM (other)->name, G_PASTE_PASSWORD_ITEM_NO_NAME))
        return FALSE;

    return g_paste_str_equal (g_paste_item_get_real_value (self), g_paste_item_get_real_value (other));
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
 * @timeout: how long it may stay on the clipboard, in seconds, or 0 for as long
 *           as anything else
 *
 * Create a new instance of #GPastePasswordItem
 *
 * Returns: a newly allocated #GPastePasswordItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_password_item_new (const gchar *name,
                           const gchar *password,
                           guint        timeout)
{
    g_return_val_if_fail (password, NULL);
    g_return_val_if_fail (g_utf8_validate (password, -1, NULL), NULL);
    g_return_val_if_fail (!name || g_utf8_validate (name, -1, NULL), NULL);

    GPasteItem *self = g_paste_item_new (G_PASTE_TYPE_PASSWORD_ITEM, password);

    /* A password weighs nothing against the caps: its length is the secret. */
    g_paste_item_set_size (self, 0);
    g_paste_password_item_set_name (G_PASTE_PASSWORD_ITEM (self), name);
    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (self), timeout);

    return self;
}
