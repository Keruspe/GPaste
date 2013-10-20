/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifdef GDK_WINDOWING_WAYLAND
#  include <gdk/gdkwayland.h>
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
#  include <gdk/gdkx.h>
#  include <X11/extensions/XInput2.h>
#endif

struct _GPasteKeybinderPrivate
{
    GSList         *keybindings;
    GPasteSettings *settings;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteKeybinder, g_paste_keybinder, G_TYPE_OBJECT)

/*
 * Wrapper around GPasteKeybinding
 */

typedef struct
{
    GPasteKeybinding *binding;
    GPasteSettings   *settings;

    gulong            rebind_signal;
} _Keybinding;

static void
_keybinding_activate (_Keybinding *k)
{
    g_paste_keybinding_activate (k->binding,
                                 g_paste_keybinding_get_setting_getter (k->binding) (k->settings));
}

static void
_keybinding_rebind (_Keybinding    *k,
                    GPasteSettings *setting G_GNUC_UNUSED)
{
    g_paste_keybinding_deactivate (k->binding);
    _keybinding_activate (k);
}

static _Keybinding *
_keybinding_new (GPasteKeybinding *binding,
                 GPasteSettings   *settings)
{
    _Keybinding *k = g_new (_Keybinding, 1);

    k->binding = binding;
    k->settings = g_object_ref (settings);

    G_PASTE_CLEANUP_FREE gchar *detailed_signal = g_strdup_printf ("rebind::%s",
                                                                   g_paste_keybinding_get_dconf_key (binding));
    k->rebind_signal = g_signal_connect_swapped (G_OBJECT (settings),
                                                 detailed_signal,
                                                 G_CALLBACK (_keybinding_rebind),
                                                 k);
    return k;
}

static void
_keybinding_free (_Keybinding *k)
{
    g_signal_handler_disconnect (k->settings, k->rebind_signal);
    g_object_unref (k->binding);
    g_object_unref (k->settings);
    g_free (k);
}

#define GET_BINDING(k) ((_Keybinding *) k)->binding

/**
 * g_paste_keybinder_add_keybinding:
 * @self: a #GPasteKeybinder instance
 * @binding; (transfer full): a #GPasteKeybinding instance
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

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    priv->keybindings = g_slist_prepend (priv->keybindings,
                                         _keybinding_new (binding, priv->settings));
}

#ifdef GDK_WINDOWING_WAYLAND
static void
g_paste_keybinder_change_grab_wayland (void)
{
    g_error ("Wayland is currently not supported outside of gnome-shell.");
}
#endif

#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
static void
g_paste_keybinder_change_grab_x11 (GPasteKeybinding *binding,
                                   Display          *display,
                                   gboolean          grab)
{
    if (!g_paste_keybinding_is_active (binding))
        return;

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

    Window window = GDK_ROOT_WINDOW ();
    GdkModifierType modifiers = g_paste_keybinding_get_modifiers (binding);
    const guint *keycodes = g_paste_keybinding_get_keycodes (binding);

    for (guint i = 0; i < G_N_ELEMENTS (mod_masks); ++i) {
        XIGrabModifiers mods = { mod_masks[i] | modifiers, 0 };
        for (const guint *keycode = keycodes; *keycode; ++keycode)
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
g_paste_keybinder_change_grab_internal (GPasteKeybinding *binding,
                                        gboolean          grab)
{
    GdkDisplay *display = gdk_display_get_default ();;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_paste_keybinder_change_grab_wayland ();
    else
#endif
#if defined(ENABLE_X_KEYBINDER) && defined (GDK_WINDOWING_X11)
    if (GDK_IS_X11_DISPLAY (display))
        g_paste_keybinder_change_grab_x11 (binding, GDK_DISPLAY_XDISPLAY (display), grab);
    else
#endif
        g_warning ("Unsupported GDK backend, keybinder won't work.");
}

static void
g_paste_keybinder_activate_keybinding_func (gpointer data,
                                            gpointer user_data G_GNUC_UNUSED)
{
    _Keybinding *k = data;
    GPasteKeybinding *keybinding = k->binding;;

    if (!g_paste_keybinding_is_active (keybinding))
    {
        _keybinding_activate (k);
        g_paste_keybinder_change_grab_internal (keybinding, TRUE);
    }
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

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    g_slist_foreach (priv->keybindings,
                     g_paste_keybinder_activate_keybinding_func,
                     NULL);
}

static void
g_paste_keybinder_deactivate_keybinding_func (gpointer data,
                                              gpointer user_data G_GNUC_UNUSED)
{
    GPasteKeybinding *keybinding = GET_BINDING (data);

    if (g_paste_keybinding_is_active (keybinding))
    {
        g_paste_keybinder_change_grab_internal (keybinding, FALSE);
        g_paste_keybinding_deactivate (keybinding);
    }
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

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    g_slist_foreach (priv->keybindings,
                     g_paste_keybinder_deactivate_keybinding_func,
                     NULL);
}

static GdkFilterReturn
g_paste_keybinder_filter (GdkXEvent *xevent,
                          GdkEvent  *event G_GNUC_UNUSED,
                          gpointer   data)
{
    GPasteKeybinderPrivate *priv = data;

    for (GList *dev = gdk_device_manager_list_devices (gdk_display_get_device_manager (gdk_display_get_default ()),
                                                       GDK_DEVICE_TYPE_MASTER); dev; dev = g_list_next (dev))
    {
        GdkDevice *device = dev->data;

        if (gdk_device_get_source (device) == GDK_SOURCE_KEYBOARD)
            gdk_device_ungrab (device, GDK_CURRENT_TIME);
    }

    gdk_flush ();

    for (GSList *keybinding = priv->keybindings; keybinding; keybinding = g_slist_next (keybinding))
    {
        GPasteKeybinding *real_keybinding = GET_BINDING (keybinding->data);
        if (g_paste_keybinding_is_active (real_keybinding))
            g_paste_keybinding_notify (real_keybinding, xevent);
    }

    return GDK_FILTER_CONTINUE;
}

static void
g_paste_keybinder_dispose (GObject *object)
{
    GPasteKeybinder *self = G_PASTE_KEYBINDER (object);
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    if (priv->settings)
    {
        g_clear_object (&priv->settings);
        g_paste_keybinder_deactivate_all (self);
        g_slist_foreach (priv->keybindings, (GFunc) _keybinding_free, NULL);
        priv->keybindings = NULL;
    }

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->dispose (object);
}

static void
g_paste_keybinder_finalize (GObject *object)
{
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (G_PASTE_KEYBINDER (object));

    gdk_window_remove_filter (gdk_get_default_root_window (),
                              g_paste_keybinder_filter,
                              priv);

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->finalize (object);
}

static void
g_paste_keybinder_class_init (GPasteKeybinderClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_keybinder_dispose;
    object_class->finalize = g_paste_keybinder_finalize;
}

static void
g_paste_keybinder_init (GPasteKeybinder *self)
{
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    priv->keybindings = NULL;

    gdk_window_add_filter (gdk_get_default_root_window (),
                           g_paste_keybinder_filter,
                           priv);
}

/**
 * g_paste_keybinder_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new instance of #GPasteKeybinder
 *
 * Returns: a newly allocated #GPasteKeybinder
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinder *
g_paste_keybinder_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    GPasteKeybinder *self = G_PASTE_KEYBINDER (g_object_new (G_PASTE_TYPE_KEYBINDER, NULL));
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    priv->settings = g_object_ref (settings);

    return self;
}
