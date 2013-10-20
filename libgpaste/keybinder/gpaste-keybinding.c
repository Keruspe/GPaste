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

#include "gpaste-keybinding-private.h"

#ifdef GDK_WINDOWING_WAYLAND
#  include <gdk/gdkwayland.h>
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
#  include <gdk/gdkx.h>
#  include <X11/extensions/XInput2.h>
#endif

struct _GPasteKeybindingPrivate
{
    GPasteKeybindingGetter getter;
    gchar                 *dconf_key;
    GPasteKeybindingFunc   callback;
    gpointer               user_data;
    gboolean               active;
    GdkModifierType        modifiers;
    guint                 *keycodes;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteKeybinding, g_paste_keybinding, G_TYPE_OBJECT)

/**
 * g_paste_keybinding_get_modifiers:
 * @self: a #GPasteKeybinding instance
 *
 * Get the modifiers for this keybinding
 *
 * Returns: the modifiers
 */
G_PASTE_VISIBLE GdkModifierType
g_paste_keybinding_get_modifiers (const GPasteKeybinding *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private ((GPasteKeybinding *) self);

    return priv->modifiers;
}

/**
 * g_paste_keybinding_get_keycodes:
 * @self: a #GPasteKeybinding instance
 *
 * Get the keycodes for this keybinding
 *
 * Returns: the keycodes
 */
G_PASTE_VISIBLE const guint *
g_paste_keybinding_get_keycodes (const GPasteKeybinding *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private ((GPasteKeybinding *) self);

    return priv->keycodes;
}

/**
 * g_paste_keybinding_get_dconf_key:
 * @self: a #GPasteKeybinding instance
 *
 * Get the dconf key for this keybinding
 *
 * Returns: the dconf key
 */
G_PASTE_VISIBLE const gchar *
g_paste_keybinding_get_dconf_key (const GPasteKeybinding *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private ((GPasteKeybinding *) self);

    return priv->dconf_key;
}

/**
 * g_paste_keybinding_activate:
 * @self: a #GPasteKeybinding instance
 * @settings: a #GPasteSettings instance
 *
 * Activate the keybinding
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_keybinding_activate (GPasteKeybinding *self,
                             GPasteSettings   *settings)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    g_return_if_fail (!priv->active);

    const gchar *binding = priv->getter (settings);

    if (binding)
    {
        gtk_accelerator_parse_with_keycode (binding, NULL, &priv->keycodes, &priv->modifiers);

        priv->active = priv->keycodes != NULL;
    }
}

/**
 * g_paste_keybinding_deactivate:
 * @self: a #GPasteKeybinding instance
 *
 * Deactivate the keybinding
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_keybinding_deactivate (GPasteKeybinding *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    g_return_if_fail (priv->active);

    priv->active = FALSE;
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

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    return priv->active;
}

#ifdef GDK_WINDOWING_WAYLAND
static void
g_paste_keybinding_parse_event_wayland (void)
{
    g_error ("Wayland is currently not supported outside of gnome-shell.");
}
#endif

#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
static gint
g_paste_keybinding_get_xinput_opcode (Display *display)
{
    static gint xinput_opcode = 0;

    if (!xinput_opcode)
    {
        gint major = 2, minor = 3;
        gint xinput_error_base;
        gint xinput_event_base;

        if (XQueryExtension (display,
                             "XInputExtension",
                             &xinput_opcode,
                             &xinput_error_base,
                             &xinput_event_base))
        {
            if (XIQueryVersion (display, &major, &minor) != Success)
                g_warning ("XInput 2 not found, keybinder won't work");
        }
    }

    return xinput_opcode;
}

static void
g_paste_keybinding_parse_event_x11 (XEvent                  *event,
                                    GdkModifierType         *modifiers,
                                    guint                   *keycode)
{
    XGenericEventCookie cookie = event->xcookie;

    if (cookie.extension == g_paste_keybinding_get_xinput_opcode (NULL))
    {
        XIDeviceEvent *xi_ev = (XIDeviceEvent *) cookie.data;

        if (xi_ev->evtype == XI_KeyPress)
        {
            *modifiers = xi_ev->mods.effective;
            *keycode = xi_ev->detail;
        }
    }
}
#endif

static gboolean
g_paste_keybinding_private_match (GPasteKeybindingPrivate *priv,
                                  GdkModifierType          modifiers,
                                  guint                    keycode)
{
    if (priv->keycodes && priv->modifiers == (priv->modifiers & modifiers))
    {
        for (guint *_keycode = priv->keycodes; *_keycode; ++_keycode)
        {
            if (keycode == *_keycode)
                return TRUE;
        }
    }

    return FALSE;
}

/**
 * g_paste_keybinding_notify:
 * @self: a #GPasteKeybinding instance
 * @xevent: The current X event
 *
 * Runs the callback associated to the keybinding if needed
 *
 * Returns: The return value of the callback
 */
G_PASTE_VISIBLE void
g_paste_keybinding_notify (GPasteKeybinding *self,
                           GdkXEvent        *xevent)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDING (self));

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);
    GdkDisplay *display = gdk_display_get_default ();
    GdkModifierType modifiers;
    guint keycode = 0;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_paste_keybinding_parse_event_wayland ();
    else
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
    if (GDK_IS_X11_DISPLAY (display))
        g_paste_keybinding_parse_event_x11 ((XEvent *) xevent, &modifiers, &keycode);
    else
#endif
        g_warning ("Unsupported GDK backend, keybinder won't work.");

    if (keycode && g_paste_keybinding_private_match (priv, modifiers, keycode))
        priv->callback (self, priv->user_data);
}

static void
g_paste_keybinding_dispose (GObject *object)
{
    GPasteKeybinding *self = G_PASTE_KEYBINDING (object);
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    if (priv->active)
        g_paste_keybinding_deactivate (self);

    G_OBJECT_CLASS (g_paste_keybinding_parent_class)->dispose (object);
}

static void
g_paste_keybinding_finalize (GObject *object)
{
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (G_PASTE_KEYBINDING (object));

    g_free (priv->keycodes);
    g_free (priv->dconf_key);

    G_OBJECT_CLASS (g_paste_keybinding_parent_class)->finalize (object);
}

static void
g_paste_keybinding_class_init (GPasteKeybindingClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_keybinding_dispose;
    object_class->finalize = g_paste_keybinding_finalize;
}

static void
g_paste_keybinding_init (GPasteKeybinding *self)
{
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    GdkDisplay *display = gdk_display_get_default ();

    priv->active = FALSE;

#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
    /* Initialize */
    g_paste_keybinding_get_xinput_opcode (GDK_DISPLAY_XDISPLAY (display));
#endif
}

/**
 * _g_paste_keybinding_new: (skip)
 */
GPasteKeybinding *
_g_paste_keybinding_new (GType                  type,
                         const gchar           *dconf_key,
                         GPasteKeybindingGetter getter,
                         GPasteKeybindingFunc   callback,
                         gpointer               user_data)
{
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_KEYBINDING), NULL);
    g_return_val_if_fail (dconf_key, NULL);
    g_return_val_if_fail (getter, NULL);
    g_return_val_if_fail (callback, NULL);

    GPasteKeybinding *self = g_object_new (type, NULL);
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    priv->getter = getter;
    priv->dconf_key = g_strdup (dconf_key);
    priv->callback = callback;
    priv->user_data = user_data;
    priv->keycodes = NULL;

    return self;
}

/**
 * g_paste_keybinding_new:
 * @dconf_key: the dconf key to watch
 * @getter: (scope notified): the getter to use to get the binding
 * @callback: (closure user_data) (scope notified): the callback to call when activated
 * @user_data: (closure): the data to pass to @callback, defaults to self/this
 *
 * Create a new instance of #GPasteKeybinding
 *
 * Returns: a newly allocated #GPasteKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_keybinding_new (const gchar           *dconf_key,
                        GPasteKeybindingGetter getter,
                        GPasteKeybindingFunc   callback,
                        gpointer               user_data)
{
    return _g_paste_keybinding_new (G_PASTE_TYPE_KEYBINDING,
                                    dconf_key,
                                    getter,
                                    callback,
                                    user_data);
}
