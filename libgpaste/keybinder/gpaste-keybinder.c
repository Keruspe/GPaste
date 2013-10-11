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

#ifdef GDK_WINDOWING_X11
#  include <X11/extensions/XIproto.h>
#endif

struct _GPasteKeybinderPrivate
{
    GSList     *keybindings;

    /* TODO: share with keybindings */
    GdkDisplay *display;
    GdkWindow  *window;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteKeybinder, g_paste_keybinder, G_TYPE_OBJECT)

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

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    priv->keybindings = g_slist_prepend (priv->keybindings,
                                         g_object_ref (binding));
}

static void
g_paste_keybinder_activate_keybinding_func (gpointer data,
                                            gpointer user_data G_GNUC_UNUSED)
{
    GPasteKeybinding *keybinding = data;

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

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    g_slist_foreach (priv->keybindings,
                     g_paste_keybinder_activate_keybinding_func,
                     NULL);
}

static void
g_paste_keybinder_deactivate_keybinding_func (gpointer data,
                                              gpointer user_data G_GNUC_UNUSED)
{
    GPasteKeybinding *keybinding = data;

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

    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (self);

    g_slist_foreach (priv->keybindings,
                     g_paste_keybinder_deactivate_keybinding_func,
                     NULL);
}

#ifdef GDK_WINDOWING_WAYLAND
static void
g_paste_keybinder_unlock_wayland (void)
{
    g_error ("Wayland is currently not supported.");
}
#endif

#ifdef GDK_WINDOWING_X11
static void
g_paste_keybinder_unlock_x11 (Display *display)
{
    XIUngrabDevice (display, XI_DeviceButtonPress, CurrentTime);
    XSync (display, FALSE);
}
#endif

static void
g_paste_keybinder_unlock (GdkDisplay *display)
{
#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY (display))
        g_paste_keybinder_unlock_wayland ();
    else
#endif
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY (display))
        g_paste_keybinder_unlock_x11 (GDK_DISPLAY_XDISPLAY (display));
    else
#endif
        g_error ("Unsupported GDK backend.");
}

static GdkFilterReturn
g_paste_keybinder_filter (GdkXEvent *xevent,
                          GdkEvent  *event G_GNUC_UNUSED,
                          gpointer   data)
{
    GPasteKeybinderPrivate *priv = data;

    g_paste_keybinder_unlock (priv->display);

    for (GSList *keybinding = priv->keybindings; keybinding; keybinding = g_slist_next (keybinding))
    {
        GPasteKeybinding *real_keybinding = keybinding->data;
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

    if (priv->keybindings)
    {
        g_paste_keybinder_deactivate_all (self);
        g_slist_foreach (priv->keybindings, (GFunc) g_object_unref, NULL);
        priv->keybindings = NULL;
    }

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->dispose (object);
}

static void
g_paste_keybinder_finalize (GObject *object)
{
    GPasteKeybinderPrivate *priv = g_paste_keybinder_get_instance_private (G_PASTE_KEYBINDER (object));

    gdk_window_remove_filter (priv->window,
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
    GdkWindow *window = priv->window = gdk_get_default_root_window ();

    priv->display = gdk_display_get_default ();
    priv->keybindings = NULL;

    gdk_window_add_filter (window,
                           g_paste_keybinder_filter,
                           priv);
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
