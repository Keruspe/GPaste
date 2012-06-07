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

#include <gtk/gtk.h>

#define G_PASTE_KEYBINDING_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_KEYBINDING, GPasteKeybindingPrivate))

G_DEFINE_TYPE (GPasteKeybinding, g_paste_keybinding, G_TYPE_OBJECT)

struct _GPasteKeybindingPrivate
{
    GPasteXcbWrapper      *xcb_wrapper;
    gchar                 *binding;
    xcb_keycode_t         *keycodes;
    guint16                modifiers;
    GPasteSettings        *settings;
    GPasteKeybindingGetter getter;
    GPasteKeybindingFunc   callback;
    gpointer               user_data;
    gboolean               active;
};

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

    GPasteXcbWrapper *xcb_wrapper = priv->xcb_wrapper;
    xcb_connection_t *connection = (xcb_connection_t *) g_paste_xcb_wrapper_get_connection (xcb_wrapper);
    xcb_screen_t *screen = (xcb_screen_t *) g_paste_xcb_wrapper_get_screen (xcb_wrapper);
    guint keysym;

    g_return_if_fail (screen); /* This should never happen */

    gtk_accelerator_parse (priv->binding, &keysym, (GdkModifierType *) &priv->modifiers);
    priv->keycodes = xcb_key_symbols_get_keycode ((xcb_key_symbols_t *) g_paste_xcb_wrapper_get_keysyms (xcb_wrapper), keysym);

    gdk_error_trap_push ();
    for (xcb_keycode_t *keycode = priv->keycodes; *keycode; ++keycode)
    {
        xcb_grab_key (connection,
                      FALSE,
                      screen->root,
                      priv->modifiers,
                      *keycode,
                      XCB_GRAB_MODE_ASYNC,
                      XCB_GRAB_MODE_ASYNC);
    }
    xcb_flush (connection);
    gdk_error_trap_pop_ignored ();

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

    GPasteXcbWrapper *xcb_wrapper = priv->xcb_wrapper;

    for (xcb_keycode_t *keycode = priv->keycodes; *keycode; ++keycode)
    {
        xcb_screen_t *screen = (xcb_screen_t *) g_paste_xcb_wrapper_get_screen (xcb_wrapper);

        xcb_ungrab_key ((xcb_connection_t *) g_paste_xcb_wrapper_get_connection (xcb_wrapper),
                        *keycode,
                        screen->root,
                        priv->modifiers);
    }

    g_free (priv->keycodes);

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
 * g_paste_keybinding_get_keycodes:
 * @self: a #GPasteKeybinding instance
 *
 * Get the keycodes corresponding to the binding
 *
 * Returns: (array zero-terminated=1): an array of keycodes or NULL
 */
G_PASTE_VISIBLE const GPasteKeycode *
g_paste_keybinding_get_keycodes (GPasteKeybinding *self)
{
    g_return_val_if_fail (G_PASTE_IS_KEYBINDING (self), NULL);

    GPasteKeybindingPrivate *priv = self->priv;

    return (priv->active) ? (GPasteKeycode *) priv->keycodes : NULL;
}

/**
 * g_paste_keybinding_get_modifirs:
 * @self: a #GPasteKeybinding instance
 *
 * Get the modifiers required by the binding
 *
 * Returns: the modifiers required
 */
G_PASTE_VISIBLE guint16
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
 * g_paste_keybinding_run_callback:
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
    g_object_unref (priv->xcb_wrapper);
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

    priv->active = FALSE;
}

/**
 * g_paste_keybinding_new:
 * @xcb_wrapper: a #GPasteXcbWrapper instance
 * @settings: a #GPasteSettings instance
 * @dconf_key: the dconf key to watch
 * @getter: (closure settings) (scope notified): the getter to use to get the binding
 * @callback: (closure user_data) (scope notified): the callback to call when activated
 * @user_data: (closure): the data to pass to @callback
 *
 * Create a new instance of #GPasteKeybinding
 *
 * Returns: a newly allocated #GPasteKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_keybinding_new (GPasteXcbWrapper      *xcb_wrapper,
                        GPasteSettings        *settings,
                        const gchar           *dconf_key,
                        GPasteKeybindingGetter getter,
                        GPasteKeybindingFunc   callback,
                        gpointer               user_data)
{
    g_return_val_if_fail (G_PASTE_IS_XCB_WRAPPER (xcb_wrapper), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (dconf_key != NULL, NULL);
    g_return_val_if_fail (getter != NULL, NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    GPasteKeybinding *self = g_object_new (G_PASTE_TYPE_KEYBINDING, NULL);
    GPasteKeybindingPrivate *priv = self->priv;

    priv->xcb_wrapper = g_object_ref (xcb_wrapper);
    priv->settings = g_object_ref (settings);
    priv->binding = g_strdup (getter (settings));
    priv->getter = getter;
    priv->callback = callback;
    priv->user_data = user_data;

    gchar *detailed_signal = g_strdup_printf ("rebind::%s", dconf_key);

    g_signal_connect_swapped (G_OBJECT (settings),
                              detailed_signal,
                              G_CALLBACK (g_paste_keybinding_rebind),
                              self);

    g_free (detailed_signal);

    return self;
}
