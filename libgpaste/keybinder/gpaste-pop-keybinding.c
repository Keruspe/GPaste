/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-pop-keybinding-private.h"

#include <gpaste-gsettings-keys.h>

struct _GPastePopKeybindingPrivate
{
    GPasteHistory *history;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPastePopKeybinding, g_paste_pop_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_pop_keybinding_dispose (GObject *object)
{
    GPastePopKeybindingPrivate *priv = g_paste_pop_keybinding_get_instance_private (G_PASTE_POP_KEYBINDING (object));

    g_clear_object (&priv->history);

    G_OBJECT_CLASS (g_paste_pop_keybinding_parent_class)->dispose (object);
}

static void
g_paste_pop_keybinding_class_init (GPastePopKeybindingClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_pop_keybinding_dispose;
}

static void
g_paste_pop_keybinding_init (GPastePopKeybinding *self G_GNUC_UNUSED)
{
}

static void
pop (GPasteKeybinding *self,
     gpointer          data G_GNUC_UNUSED)
{
    GPastePopKeybindingPrivate *priv = g_paste_pop_keybinding_get_instance_private (G_PASTE_POP_KEYBINDING (self));

    g_paste_history_remove (priv->history, 0);
}

/**
 * g_paste_pop_keybinding_new:
 * @history: a #GPasteHistory instance
 *
 * Create a new instance of #GPastePopKeybinding
 *
 * Returns: a newly allocated #GPastePopKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_pop_keybinding_new (GPasteHistory *history)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);

    GPasteKeybinding *self = _g_paste_keybinding_new (G_PASTE_TYPE_POP_KEYBINDING,
                                                      G_PASTE_POP_SETTING,
                                                      g_paste_settings_get_pop,
                                                      pop,
                                                      NULL);
    GPastePopKeybindingPrivate *priv = g_paste_pop_keybinding_get_instance_private (G_PASTE_POP_KEYBINDING (self));

    priv->history = g_object_ref (history);

    return self;
}
