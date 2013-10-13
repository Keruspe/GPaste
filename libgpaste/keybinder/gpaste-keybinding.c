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

#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_WAYLAND
#  include <gdk/gdkwayland.h>
#endif
#ifdef GDK_WINDOWING_X11
#  include <gdk/gdkx.h>
#  include <X11/extensions/XInput2.h>
#endif

struct _GPasteKeybindingPrivate
{
    gchar                 *binding;
    GPasteSettings        *settings;
    GPasteKeybindingGetter getter;
    GPasteKeybindingFunc   callback;
    gpointer               user_data;
    gboolean               active;
    GdkWindow             *window;
    GdkModifierType        modifiers;
    guint                 *keycodes;

    gulong                 rebind_signal;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteKeybinding, g_paste_keybinding, G_TYPE_OBJECT)

#ifdef GDK_WINDOWING_WAYLAND
static void
g_paste_keybinding_change_grab_wayland (void)
{
    g_error ("Wayland is currently not supported.");
}
#endif

#ifdef GDK_WINDOWING_X11
static void
g_paste_keybinding_change_grab_x11 (GPasteKeybinding *self,
                                    gboolean          grab)
{
    guchar mask_bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
    XIEventMask mask = { XIAllMasterDevices, sizeof (mask_bits), mask_bits };

    XISetMask (mask.mask, XI_KeyPress);

    gdk_error_trap_push ();

    guint mod_masks [] = {
        0, /* modifier only */
        GDK_MOD2_MASK, /* NumLock */
        GDK_MOD5_MASK, /* ScrollLock */
        GDK_LOCK_MASK, /* CapsLock */
        GDK_MOD2_MASK | GDK_MOD5_MASK,
        GDK_MOD2_MASK | GDK_LOCK_MASK,
        GDK_MOD5_MASK | GDK_LOCK_MASK,
        GDK_MOD2_MASK | GDK_MOD5_MASK | GDK_LOCK_MASK,
    };

    Display *display = GDK_DISPLAY_XDISPLAY (self->display);
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);
    Window window = gdk_x11_window_get_xid (priv->window);

    for (guint i = 0; i < G_N_ELEMENTS (mod_masks); ++i) {
        XIGrabModifiers mods = { mod_masks[i] | priv->modifiers, 0 };
        for (guint *keycode = priv->keycodes; *keycode; ++keycode)
        {
            if (grab)
            {
                XIGrabKeycode (display,
                               XIAllMasterDevices,
                               *keycode,
                               window,
                               XIGrabModeSync,
                               XIGrabModeAsync,
                               False,
                               &mask,
                               1,
                               &mods);
            }
            else
            {
                XIUngrabKeycode (display,
                                 XIAllMasterDevices,
                                 *keycode,
                                 window,
                                 1,
                                 &mods);
            }
        }
    }

    gdk_flush ();
    gdk_error_trap_pop_ignored ();
}
#endif

static void
g_paste_keybinding_change_grab (GPasteKeybinding *self,
                                gboolean          grab)
{
    GdkDisplay *display = self->display;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_paste_keybinding_change_grab_wayland ();
    else
#endif
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY (display))
        g_paste_keybinding_change_grab_x11 (self, grab);
    else
#endif
        g_error ("Unsupported GDK backend.");
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

    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    g_return_if_fail (!priv->active);

    gtk_accelerator_parse_with_keycode (priv->binding, NULL, &priv->keycodes, &priv->modifiers);

    if (priv->keycodes)
        g_paste_keybinding_change_grab (self, TRUE);

    priv->active = TRUE;
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

    if (priv->keycodes)
        g_paste_keybinding_change_grab (self, FALSE);

    priv->active = FALSE;
}

static void
g_paste_keybinding_rebind (GPasteKeybinding *self,
                           GPasteSettings   *settings G_GNUC_UNUSED)
{
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    g_free (priv->binding);
    priv->binding = g_strdup (priv->getter (priv->settings));

    if (priv->active)
    {
        g_paste_keybinding_deactivate (self);
        g_paste_keybinding_activate (self);
    }
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
    g_error ("Wayland is currently not supported.");
}
#endif

#ifdef GDK_WINDOWING_X11
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
    GdkDisplay *display = self->display;
    GdkModifierType modifiers;
    guint keycode = 0;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_paste_keybinding_parse_event_wayland ();
    else
#endif
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY (display))
        g_paste_keybinding_parse_event_x11 ((XEvent *) xevent, &modifiers, &keycode);
    else
#endif
        g_error ("Unsupported GDK backend.");

    if (keycode && g_paste_keybinding_private_match (priv, modifiers, keycode))
        priv->callback (self, priv->user_data);
}

static void
g_paste_keybinding_dispose (GObject *object)
{
    GPasteKeybinding *self = G_PASTE_KEYBINDING (object);
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);
    GPasteSettings *settings = priv->settings;

    if (settings)
    {
        if (priv->active)
            g_paste_keybinding_deactivate (self);
        g_signal_handler_disconnect (priv->settings, priv->rebind_signal);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_keybinding_parent_class)->dispose (object);
}

static void
g_paste_keybinding_finalize (GObject *object)
{
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (G_PASTE_KEYBINDING (object));

    g_free (priv->binding);
    g_free (priv->keycodes);

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

    GdkDisplay *display = self->display = gdk_display_get_default ();

    priv->window = gdk_get_default_root_window ();
    priv->active = FALSE;

#ifdef GDK_WINDOWING_X11
    /* Initialize */
    g_paste_keybinding_get_xinput_opcode (GDK_DISPLAY_XDISPLAY (display));
#endif
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
    g_return_val_if_fail (g_type_is_a (type, G_PASTE_TYPE_KEYBINDING), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (dconf_key != NULL, NULL);
    g_return_val_if_fail (getter != NULL, NULL);
    g_return_val_if_fail (callback != NULL, NULL);

    GPasteKeybinding *self = g_object_new (type, NULL);
    GPasteKeybindingPrivate *priv = g_paste_keybinding_get_instance_private (self);

    priv->settings = g_object_ref (settings);
    priv->binding = g_strdup (getter (settings));
    priv->getter = getter;
    priv->callback = callback;
    priv->user_data = user_data;
    priv->keycodes = NULL;

    gchar *detailed_signal = g_strdup_printf ("rebind::%s", dconf_key);

    priv->rebind_signal = g_signal_connect_swapped (G_OBJECT (settings),
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
    return _g_paste_keybinding_new (G_PASTE_TYPE_KEYBINDING,
                                    settings,
                                    dconf_key,
                                    getter,
                                    callback,
                                    user_data);
}
