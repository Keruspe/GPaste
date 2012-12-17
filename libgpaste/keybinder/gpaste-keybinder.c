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
    Display *display;
    GSList  *keybindings;
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
g_paste_keybinder_key_pressed (GdkModifierType modifiers,
                               guint           keycode,
                               GSList         *keybinding)
{
    g_debug ("In Keypress");
    for (; keybinding; keybinding = g_slist_next (keybinding))
    {
        GPasteKeybinding *real_keybinding = keybinding->data;
        g_debug ("%u %u", keycode, g_paste_keybinding_get_keycode (real_keybinding));
        if (g_paste_keybinding_is_active (real_keybinding) &&
            keycode == g_paste_keybinding_get_keycode (real_keybinding) &&
            modifiers == g_paste_keybinding_get_modifiers (real_keybinding))
        {
            g_debug ("notify");
            g_paste_keybinding_notify (real_keybinding);
            break;
        }
    }
}

static GdkFilterReturn
g_paste_keybinder_filter (GdkXEvent *xevent,
                          GdkEvent  *event G_GNUC_UNUSED,
                          gpointer   data)
{
    GPasteKeybinderPrivate *priv = G_PASTE_KEYBINDER (data)->priv;
    Display *display = (Display *) priv->display;
    XEvent *ev = (XEvent *) xevent;

    XIUngrabDevice (display, 3, CurrentTime);
    XSync (display, FALSE);

    if (ev->type == KeyPress)
    {
        XKeyEvent key = ev->xkey;
        g_paste_keybinder_key_pressed (key.keycode, key.state, priv->keybindings);
    }
    else if (ev->type == GenericEvent && ev->xgeneric.evtype == XI_KeyPress)
    {
        XIDeviceEvent *xi_ev = (XIDeviceEvent*) ev;
        g_paste_keybinder_key_pressed (xi_ev->detail, xi_ev->mods.effective, priv->keybindings);
    }

    return GDK_FILTER_CONTINUE;
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

    priv->display = gdk_x11_get_default_xdisplay ();
    priv->keybindings = NULL;

    gdk_window_add_filter (gdk_get_default_root_window (),
                           g_paste_keybinder_filter,
                           self);

    gint major = 2, minor = 2;
    gboolean has_xi = FALSE;
    gint xinput_error_base;
    gint xinput_event_base;
    gint xinput_opcode;

    if (XQueryExtension (priv->display,
                         "XInputExtension",
                         &xinput_opcode,
                         &xinput_error_base,
                         &xinput_event_base))
    {
        if (XIQueryVersion (priv->display, &major, &minor) == Success)
        {
            if (((major * 10) + minor) >= 22)
                has_xi = TRUE;
        }
    }

    if (!has_xi)
        g_warning ("XInput 2 not found, keybinder won't work");
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
