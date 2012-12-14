/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gpaste-keybinder-private.h"

#include <gdk/gdk.h>

#define G_PASTE_KEYBINDER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_KEYBINDER, GPasteKeybinderPrivate))

G_DEFINE_TYPE (GPasteKeybinder, g_paste_keybinder, G_TYPE_OBJECT)

struct _GPasteKeybinderPrivate
{
    GSList           *keybindings;
};

/**
 * g_paste_keybinder_add_keybinding:
 * @self: a #GPasteKeybinder instance
 * @binding; a #GPasteKeybinding instance
 *
 * Add a new keybinding
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_keybinder_add_keybinding (GPasteKeybinder  *self,
                                  GPasteKeybinding *binding)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));
    g_return_if_fail (G_PASTE_IS_KEYBINDING (binding));

    GPasteKeybinderPrivate *priv = self->priv;

    priv->keybindings = g_slist_prepend (priv->keybindings,
                                         g_object_ref (binding));
}

static void
g_paste_keybinder_activate_keybinding_func (gpointer data,
                                            gpointer user_data G_GNUC_UNUSED)
{
    GPasteKeybinding *keybinding = G_PASTE_KEYBINDING (data);

    if (!g_paste_keybinding_is_active (keybinding))
        g_paste_keybinding_activate (keybinding);
}

/**
 * g_paste_keybinder_activate_all:
 * @self: a #GPasteKeybinder instance
 *
 * Activate all the managed keybindings
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_keybinder_activate_all (GPasteKeybinder *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));

    g_slist_foreach (self->priv->keybindings,
                     g_paste_keybinder_activate_keybinding_func,
                     self);
}

static void
g_paste_keybinder_deactivate_keybinding_func (gpointer data,
                                              gpointer user_data G_GNUC_UNUSED)
{
    GPasteKeybinding *keybinding = G_PASTE_KEYBINDING (data);

    if (g_paste_keybinding_is_active (keybinding))
        g_paste_keybinding_deactivate (keybinding);
}

/**
 * g_paste_keybinder_deactivate_all:
 * @self: a #GPasteKeybinder instance
 *
 * Deactivate all the managed keybindings
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_keybinder_deactivate_all (GPasteKeybinder *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));

    g_slist_foreach (self->priv->keybindings,
                     g_paste_keybinder_deactivate_keybinding_func,
                     self);
}

static void
g_paste_keybinder_dispose (GObject *object)
{
    GPasteKeybinder *self = G_PASTE_KEYBINDER (object);

    g_paste_keybinder_deactivate_all (self);
    g_slist_foreach (self->priv->keybindings, (GFunc) g_object_unref, NULL);

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->dispose (object);
}

static void
g_paste_keybinder_class_init (GPasteKeybinderClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteKeybinderPrivate));

    G_OBJECT_CLASS (klass)->dispose = g_paste_keybinder_dispose;
}

static void
g_paste_keybinder_init (GPasteKeybinder *self)
{
    GPasteKeybinderPrivate *priv = self->priv = G_PASTE_KEYBINDER_GET_PRIVATE (self);

    priv->keybindings = NULL;
}

/**
 * g_paste_keybinder_new:
 *
 * Create a new instance of #GPasteKeybinder
 *
 * Returns: a newly allocated #GPasteKeybinder
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinder *
g_paste_keybinder_new (void)
{
    return g_object_new (G_PASTE_TYPE_KEYBINDER, NULL);
}
