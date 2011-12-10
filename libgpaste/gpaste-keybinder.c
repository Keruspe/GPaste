/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>

#define G_PASTE_KEYBINDER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_KEYBINDER, GPasteKeybinderPrivate))

G_DEFINE_TYPE (GPasteKeybinder, g_paste_keybinder, G_TYPE_OBJECT)

struct _GPasteKeybinderPrivate
{
    KeyCode keycode;
    GdkModifierType modifiers;
    Display *display;
    GdkWindow *root_window;
};

enum
{
    TOGGLE,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/**
 * g_paste_keybinder_unbind:
 * @self: a GPasteKeybinder instance
 *
 * Unbind the keybinding
 *
 * Returns:
 */
void
g_paste_keybinder_unbind (GPasteKeybinder *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));

    GPasteKeybinderPrivate *priv = self->priv;
    XID xid = gdk_x11_window_get_xid (priv->root_window);

    XUngrabKey (priv->display, priv->keycode, (guint) priv->modifiers, (Window) xid);
}

/**
 * g_paste_keybinder_rebind:
 * @self: a GPasteKeybinder instance
 * @binding: the new keybinding
 *
 * Rebind to a new keybinding
 *
 * Returns:
 */
void
g_paste_keybinder_rebind (GPasteKeybinder *self,
                          const gchar     *binding)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));

    g_paste_keybinder_unbind (self);
    g_paste_keybinder_activate (self, binding);
}

static GdkFilterReturn
g_paste_keybinder_event_filter (GdkXEvent *xevent,
                                GdkEvent  *event,
                                gpointer   user_data)
{
    /* silence warning */
    event = event;

    GPasteKeybinder *self = G_PASTE_KEYBINDER (user_data);
    GPasteKeybinderPrivate *priv = self->priv;
    XEvent *xev = (XEvent *) xevent;

    if (xev->type == KeyPress &&
        xev->xkey.keycode == priv->keycode &&
        xev->xkey.state == priv->modifiers)
    {
        Display *display = priv->display;

        XUngrabKeyboard (display, GDK_CURRENT_TIME);
        XSync (display, FALSE);
        g_signal_emit (self,
                       signals[TOGGLE],
                       0); /* detail */
    }

    return GDK_FILTER_CONTINUE; 
}

/**
 * g_paste_keybinder_activate:
 * @self: a GPasteKeybinder instance
 * @binding: the keybinding
 *
 * Bind to a keybinding
 *
 * Returns:
 */
void
g_paste_keybinder_activate (GPasteKeybinder *self,
                            const gchar     *binding)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));

    GPasteKeybinderPrivate *priv = self->priv;
    guint keysym;

    gtk_accelerator_parse (binding, &keysym, &priv->modifiers);
    priv->keycode = XKeysymToKeycode (priv->display, keysym);

    if (priv->keycode)
    {
        XID xid = gdk_x11_window_get_xid (priv->root_window);
        
        gdk_error_trap_push ();
        XGrabKey (priv->display, priv->keycode, priv->modifiers, xid, FALSE, GrabModeAsync, GrabModeAsync);
        gdk_flush ();
        gdk_error_trap_pop_ignored ();
    }
}

static void
g_paste_keybinder_class_init (GPasteKeybinderClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteKeybinderPrivate));
    
    signals[TOGGLE] = g_signal_new ("toggle",
                                    G_PASTE_TYPE_KEYBINDER,
                                    G_SIGNAL_RUN_LAST,
                                    0, /* class offset */
                                    NULL, /* accumulator */
                                    NULL, /* accumulator data */
                                    g_cclosure_marshal_VOID__VOID,
                                    G_TYPE_NONE,
                                    0); /* number of params */
}

static void
g_paste_keybinder_init (GPasteKeybinder *self)
{
    self->priv = G_PASTE_KEYBINDER_GET_PRIVATE (self);
}

/**
 * g_paste_keybinder_new:
 *
 * Create a new instance of GPasteKeybinder
 *
 * Returns: a newly allocated GPasteKeybinder
 *          free it with g_object_unref
 */
GPasteKeybinder *
g_paste_keybinder_new (const gchar *binding)
{
    GPasteKeybinder *self = g_object_new (G_PASTE_TYPE_KEYBINDER, NULL);
    GPasteKeybinderPrivate *priv = self->priv;
    GdkWindow *root_window;

    priv->display = gdk_x11_get_default_xdisplay ();
    priv->root_window = root_window = gdk_get_default_root_window ();
    if (root_window)
        gdk_window_add_filter (root_window, g_paste_keybinder_event_filter, self);
    g_paste_keybinder_activate (self, binding);

    return self;
}
