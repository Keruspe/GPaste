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

#include <gtk/gtk.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#define G_PASTE_KEYBINDER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_KEYBINDER, GPasteKeybinderPrivate))

G_DEFINE_TYPE (GPasteKeybinder, g_paste_keybinder, G_TYPE_OBJECT)

struct _GPasteKeybinderPrivate
{
    gchar             *binding;
    xcb_connection_t  *connection;
    xcb_screen_t      *screen;
    xcb_key_symbols_t *keysyms;
    xcb_keycode_t     *keycodes;
    guint16            modifiers;
};

enum
{
    TOGGLE,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static gboolean
g_paste_keybinder_source (gpointer data)
{
    GPasteKeybinder *self = G_PASTE_KEYBINDER (data);
    GPasteKeybinderPrivate *priv = self->priv;
    xcb_generic_event_t *event;

    if ((event = xcb_poll_for_event (priv->connection)) &&
        (event->response_type & ~0x80) == XCB_KEY_PRESS)
    {
        xcb_ungrab_keyboard (priv->connection, GDK_CURRENT_TIME);
        xcb_flush (priv->connection);
        g_signal_emit (self,
                       signals[TOGGLE],
                       0); /* detail */
    }

    g_free (event);

    return TRUE;
}

static void
g_paste_keybinder_activate (GPasteKeybinder *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));

    GPasteKeybinderPrivate *priv = self->priv;
    guint keysym;

    gtk_accelerator_parse (priv->binding, &keysym, (GdkModifierType *) &priv->modifiers);
    g_free (priv->keycodes);
    priv->keycodes = xcb_key_symbols_get_keycode (priv->keysyms, keysym);

    gdk_error_trap_push ();
    for (xcb_keycode_t *keycode = priv->keycodes; *keycode; ++keycode)
        xcb_grab_key (priv->connection, FALSE, priv->screen->root, priv->modifiers, *keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    xcb_flush (priv->connection);
    gdk_error_trap_pop_ignored ();
}

/**
 * g_paste_keybinder_unbind:
 * @self: a #GPasteKeybinder instance
 *
 * Unbind the keybinding
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_keybinder_unbind (GPasteKeybinder *self)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));

    GPasteKeybinderPrivate *priv = self->priv;

    for (xcb_keycode_t *keycode = priv->keycodes; *keycode; ++keycode)
        xcb_ungrab_key (priv->connection, *keycode, priv->screen->root, priv->modifiers);
}

/**
 * g_paste_keybinder_rebind:
 * @self: a #GPasteKeybinder instance
 * @binding: the new keybinding
 *
 * Rebind to a new keybinding
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_keybinder_rebind (GPasteKeybinder *self,
                          const gchar     *binding)
{
    g_return_if_fail (G_PASTE_IS_KEYBINDER (self));
    g_return_if_fail (binding != NULL);

    GPasteKeybinderPrivate *priv = self->priv;

    g_free (priv->binding);
    priv->binding = g_strdup (binding);
    g_paste_keybinder_unbind (self);
    g_paste_keybinder_activate (self);
}

static void
g_paste_keybinder_dispose (GObject *object)
{
    GPasteKeybinder *self = G_PASTE_KEYBINDER (object);

    g_paste_keybinder_unbind (self);
    xcb_disconnect (self->priv->connection);

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->dispose (object);
}

static void
g_paste_keybinder_finalize (GObject *object)
{
    GPasteKeybinderPrivate *priv = G_PASTE_KEYBINDER (object)->priv;

    xcb_key_symbols_free (priv->keysyms);
    g_free (priv->keycodes);
    g_free (priv->binding);

    G_OBJECT_CLASS (g_paste_keybinder_parent_class)->finalize (object);
}

static void
g_paste_keybinder_class_init (GPasteKeybinderClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteKeybinderPrivate));

    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_keybinder_dispose;
    object_class->finalize = g_paste_keybinder_finalize;

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
 * Create a new instance of #GPasteKeybinder
 *
 * Returns: a newly allocated #GPasteKeybinder
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinder *
g_paste_keybinder_new (const gchar *binding)
{
    g_return_val_if_fail (binding != NULL, NULL);

    GPasteKeybinder *self = g_object_new (G_PASTE_TYPE_KEYBINDER, NULL);
    GPasteKeybinderPrivate *priv = self->priv;
    int connection_screen;
    const xcb_setup_t *setup;

    priv->connection = xcb_connect (NULL, &connection_screen);

    if ((setup = xcb_get_setup (priv->connection))) {
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator (setup);
        for (; iter.rem; --connection_screen, xcb_screen_next (&iter))
            if (0 == connection_screen)
                priv->screen = iter.data;
    }

    if (!priv->screen)
    {
        g_object_unref (self);
        return NULL;
    }

    priv->keysyms = xcb_key_symbols_alloc (priv->connection);
    priv->binding = g_strdup (binding);
    g_paste_keybinder_activate (self);
    g_timeout_add (100, g_paste_keybinder_source, self);

    return self;
}
