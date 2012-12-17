/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-keybinding-private.h"

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#define G_PASTE_KEYBINDING_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_KEYBINDING, GPasteKeybindingPrivate))

G_DEFINE_TYPE (GPasteKeybinding, g_paste_keybinding, G_TYPE_OBJECT)

struct _GPasteKeybindingPrivate
{
    gchar                 *binding;
    GPasteSettings        *settings;
    GPasteKeybindingGetter getter;
    GPasteKeybindingFunc   callback;
    gpointer               user_data;
    gboolean               active;
    Window                 window;
    GdkModifierType        modifiers;
    guint                  keycode;
};

static void
g_paste_keybinding_change_grab (GPasteKeybinding *self,
                                gboolean          grab)
{
    GPasteKeybindingPrivate *priv = self->priv;

    guchar mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

    XISetMask (mask.mask, XI_KeyPress);

    gdk_error_trap_push ();

    guint mod_masks [] = {
        0, /* modifier only */
        GDK_MOD2_MASK,
        GDK_LOCK_MASK,
        GDK_MOD2_MASK | GDK_LOCK_MASK,
    };

    for (guint i = 0; i < G_N_ELEMENTS (mod_masks); i++) {
        XIGrabModifiers mods = { mod_masks[i] | priv->modifiers, 0 };

        if (grab)
        {
            XIGrabKeycode (self->display,
                           3,
                           priv->keycode,
                           priv->window,
                           XIGrabModeSync,
                           XIGrabModeAsync,
                           False,
                           &mask,
                           1,
                           &mods);
        }
        else
        {
            XIUngrabKeycode (self->display,
                             3,
                             priv->keycode,
                             priv->window,
                             1,
                             &mods);
        }
    }

    gdk_flush ();
    gdk_error_trap_pop_ignored ();
}

/**
 * g_paste_keybinding_activate:
 * @self: a #GPasteKeybinding instance
 *
 * Activate the keybinding
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_keybinding_activate (GPasteKeybinding  *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));

    GPasteKeybindingPrivate *priv = self->priv;

    g_return_if_fail (!priv->active);

    guint keysym;
    gtk_accelerator_parse (priv->binding, &keysym, &priv->modifiers);

    priv->keycode = XKeysymToKeycode (self->display, keysym);

    if (priv->keycode)
        g_paste_keybinding_change_grab (self, TRUE);

    priv->active = TRUE;
}

/**
 * g_paste_keybinding_unbind:
 * @self: a #GPasteKeybinding instance
 *
 * Deactivate the keybinding
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_keybinding_deactivate (GPasteKeybinding  *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));

    GPasteKeybindingPrivate *priv = self->priv;

    g_return_if_fail (priv->active);

    if (priv->keycode)
        g_paste_keybinding_change_grab (self, FALSE);

    priv->active = FALSE;
}

static void
g_paste_keybinding_rebind (GPasteKeybinding  *self,
                           GPasteSettings    *settings G_GNUC_UNUSED)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));

    GPasteKeybindingPrivate *priv = self->priv;

    g_free (priv->binding);
    priv->binding = g_strdup (priv->getter (priv->settings));

    if (priv->active)
    {
        g_paste_keybinding_deactivate (self);
        g_paste_keybinding_activate (self);
    }
}

/**
 * g_paste_keybinding_get_keycode:
 * @self: a #GPasteKeybinding instance
 *
 * Get the keycode corresponding to the binding
 *
 * Returns: a keycode or 0
 */
G_PASTE_VISIBLE guint
g_paste_keybinding_get_keycode (GPasteKeybinding *self)
{
    g_return_val_if_fail (G_PASTE_IS_KEYBINDING (self), 0);

    GPasteKeybindingPrivate *priv = self->priv;

    return (priv->active) ? priv->keycode : 0;
}

/**
 * g_paste_keybinding_get_modifirs:
 * @self: a #GPasteKeybinding instance
 *
 * Get the modifiers required by the binding
 *
 * Returns: the modifiers required
 */
G_PASTE_VISIBLE GdkModifierType
g_paste_keybinding_get_modifiers (GPasteKeybinding *self)
{
    g_return_val_if_fail (G_PASTE_IS_KEYBINDING (self), 0);

    GPasteKeybindingPrivate *priv = self->priv;

    return (priv->active) ? priv->modifiers : 0;
}

/**
 * g_paste_keybinding_is_active:
 * @self: a #GPasteKeybinding instance
 *
 * Check whether the keybinding is active or not
 *
 * Returns: true if the keybinding is active
 */
G_PASTE_VISIBLE gboolean
g_paste_keybinding_is_active (GPasteKeybinding *self)
{
    g_return_val_if_fail (G_PASTE_IS_KEYBINDING (self), FALSE);

    return self->priv->active;
}

/**
 * g_paste_keybinding_notify:
 * @self: a #GPasteKeybinding instance
 *
 * Runs the callback associated to the keybinding
 *
 * Returns: The return value of the callback
 */
G_PASTE_VISIBLE void
g_paste_keybinding_notify (GPasteKeybinding *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));

    GPasteKeybindingPrivate *priv = self->priv;

    priv->callback (priv->user_data);
}

static void
g_paste_keybinding_dispose (GObject *object)
{
    GPasteKeybinding *self = G_PASTE_KEYBINDING (object);
    GPasteKeybindingPrivate *priv = self->priv;

    if (priv->active)
        g_paste_keybinding_deactivate (self);
    g_object_unref (priv->settings);

    G_OBJECT_CLASS (g_paste_keybinding_parent_class)->dispose (object);
}

static void
g_paste_keybinding_finalize (GObject *object)
{
    GPasteKeybindingPrivate *priv = G_PASTE_KEYBINDING (object)->priv;

    g_free (priv->binding);

    G_OBJECT_CLASS (g_paste_keybinding_parent_class)->finalize (object);
}

static void
g_paste_keybinding_class_init (GPasteKeybindingClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteKeybindingPrivate));

    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_keybinding_dispose;
    object_class->finalize = g_paste_keybinding_finalize;
}

static void
g_paste_keybinding_init (GPasteKeybinding *self)
{
    GPasteKeybindingPrivate *priv = self->priv = G_PASTE_KEYBINDING_GET_PRIVATE (self);

    self->display = gdk_x11_get_default_xdisplay ();

    priv->window = gdk_x11_window_get_xid (gdk_get_default_root_window ());
    priv->active = FALSE;
}

/**
 * _g_paste_keybinding_new: (skip)
 */
GPasteKeybinding *
_g_paste_keybinding_new (GType                  type,
                         GPasteSettings        *settings,
                         const gchar           *dconf_key,
                         GPasteKeybindingGetter getter,
                         GPasteKeybindingFunc   callback,
                         gpointer               user_data)
{
    GPasteKeybinding *self = g_object_new (type, NULL);
    GPasteKeybindingPrivate *priv = self->priv;

    priv->settings = g_object_ref (settings);
    priv->binding = g_strdup (getter (settings));
    priv->getter = getter;
    priv->callback = callback;
    priv->user_data = (user_data) ? user_data : self;

    gchar *detailed_signal = g_strdup_printf ("rebind::%s", dconf_key);

    g_signal_connect_swapped (G_OBJECT (settings),
                              detailed_signal,
                              G_CALLBACK (g_paste_keybinding_rebind),
                              self);

    g_free (detailed_signal);

    return self;
}

/**
 * g_paste_keybinding_new:
 * @settings: a #GPasteSettings instance
 * @dconf_key: the dconf key to watch
 * @getter: (closure settings) (scope notified): the getter to use to get the binding
 * @callback: (closure user_data) (scope notified): the callback to call when activated
 * @user_data: (closure): the data to pass to @callback, defaults to self/this
 *
 * Create a new instance of #GPasteKeybinding
 *
 * Returns: a newly allocated #GPasteKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_keybinding_new (GPasteSettings        *settings,
                        const gchar           *dconf_key,
                        GPasteKeybindingGetter getter,
                        GPasteKeybindingFunc   callback,
                        gpointer               user_data)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (dconf_key != NULL, NULL);
    g_return_val_if_fail (getter != NULL, NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    return _g_paste_keybinding_new (G_PASTE_TYPE_KEYBINDING,
                                    settings,
                                    dconf_key,
                                    getter,
                                    callback,
                                    user_data);
}
