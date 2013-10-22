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
#include "gpaste-settings-keys.h"

#define G_PASTE_POP_KEYBINDING_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_POP_KEYBINDING, GPastePopKeybindingPrivate))

G_DEFINE_TYPE (GPastePopKeybinding, g_paste_pop_keybinding, G_PASTE_TYPE_KEYBINDING)

struct _GPastePopKeybindingPrivate
{
    GPasteHistory *history;
};

static void
g_paste_pop_keybinding_dispose (GObject *object)
{
    GPastePopKeybindingPrivate *priv = G_PASTE_POP_KEYBINDING (object)->priv;

    g_clear_object (&priv->history);

    G_OBJECT_CLASS (g_paste_pop_keybinding_parent_class)->dispose (object);
}

static void
g_paste_pop_keybinding_class_init (GPastePopKeybindingClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPastePopKeybindingPrivate));

    G_OBJECT_CLASS (klass)->dispose = g_paste_pop_keybinding_dispose;
}

static void
g_paste_pop_keybinding_init (GPastePopKeybinding *self)
{
    self->priv = G_PASTE_POP_KEYBINDING_GET_PRIVATE (self);
}

static void
pop (gpointer user_data)
{
    GPasteHistory *history = G_PASTE_POP_KEYBINDING (user_data)->priv->history;

    g_paste_history_remove (history, 0);
}

/**
 * g_paste_paste_and_pop_keybinding_new:
 * @settings: a #GPasteSettings instance
 * @history: a #GPasteHistory instance
 *
 * Create a new instance of #GPastePasteAndPopKeybinding
 *
 * Returns: a newly allocated #GPastePasteAndPopKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPastePopKeybinding *
g_paste_pop_keybinding_new (GPasteSettings *settings,
                            GPasteHistory  *history)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);

    GPastePopKeybinding *self = G_PASTE_POP_KEYBINDING (_g_paste_keybinding_new (G_PASTE_TYPE_POP_KEYBINDING,
                                                                                 settings,
                                                                                 POP_KEY,
                                                                                 g_paste_settings_get_pop,
                                                                                 pop,
                                                                                 NULL));
    self->priv->history = g_object_ref (history);

    return self;
}
