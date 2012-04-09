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

#include "gpaste-xcb-wrapper-private.h"

#define G_PASTE_XCB_WRAPPER_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_XCB_WRAPPER, GPasteXcbWrapperPrivate))

G_DEFINE_TYPE (GPasteXcbWrapper, g_paste_xcb_wrapper, G_TYPE_OBJECT)

struct _GPasteXcbWrapperPrivate
{
    xcb_connection_t  *connection;
    xcb_screen_t      *screen;
    xcb_key_symbols_t *keysyms;
};

/**
 * g_paste_xcb_wrapper_get_connection:
 *
 * Get the #GPasteConnection managed by the #GPasteXcbWrapper
 *
 * Returns: (transfer none): the #GPasteConnection (xcb_connection_t)
 */
G_PASTE_VISIBLE GPasteConnection
g_paste_xcb_wrapper_get_connection (GPasteXcbWrapper *self)
{
    g_return_val_if_fail (G_PASTE_IS_XCB_WRAPPER (self), NULL);

    return (GPasteConnection) self->priv->connection;
}

/**
 * g_paste_xcb_wrapper_get_screen:
 *
 * Get the #GPasteScreen managed by the #GPasteXcbWrapper
 *
 * Returns: (transfer none): the #GPasteScreen (xcb_screen_t)
 */
G_PASTE_VISIBLE GPasteScreen
g_paste_xcb_wrapper_get_screen (GPasteXcbWrapper *self)
{
    g_return_val_if_fail (G_PASTE_IS_XCB_WRAPPER (self), NULL);

    return (GPasteScreen) self->priv->screen;
}

/**
 * g_paste_xcb_wrapper_get_keysyms:
 *
 * Get the #GPasteKeySymbols managed by the #GPasteXcbWrapper
 *
 * Returns: (transfer none) (array zero-terminated=1): the #GPasteKeySymbols (xcb_connection_t)
 */
G_PASTE_VISIBLE GPasteKeySymbols
g_paste_xcb_wrapper_get_keysyms (GPasteXcbWrapper *self)
{
    g_return_val_if_fail (G_PASTE_IS_XCB_WRAPPER (self), NULL);

    return (GPasteKeySymbols) self->priv->keysyms;
}

static void
g_paste_xcb_wrapper_finalize (GObject *object)
{
    GPasteXcbWrapperPrivate *priv = G_PASTE_XCB_WRAPPER (object)->priv;

    xcb_disconnect (priv->connection);
    xcb_key_symbols_free (priv->keysyms);

    G_OBJECT_CLASS (g_paste_xcb_wrapper_parent_class)->finalize (object);
}

static void
g_paste_xcb_wrapper_class_init (GPasteXcbWrapperClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteXcbWrapperPrivate));

    G_OBJECT_CLASS (klass)->finalize = g_paste_xcb_wrapper_finalize;
}

static void
g_paste_xcb_wrapper_init (GPasteXcbWrapper *self)
{
    GPasteXcbWrapperPrivate *priv = self->priv = G_PASTE_XCB_WRAPPER_GET_PRIVATE (self);
    int connection_screen;
    const xcb_setup_t *setup;

    priv->connection = xcb_connect (NULL, &connection_screen);
    priv->keysyms = xcb_key_symbols_alloc (priv->connection);

    if ((setup = xcb_get_setup (priv->connection))) {
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator (setup);
        for (; iter.rem; --connection_screen, xcb_screen_next (&iter))
            if (0 == connection_screen)
                priv->screen = iter.data;
    }
}

/**
 * g_paste_xcb_wrapper_new:
 *
 * Create a new instance of #GPasteXcbWrapper
 *
 * Returns: a newly allocated #GPasteXcbWrapper
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteXcbWrapper *
g_paste_xcb_wrapper_new (void)
{
    GPasteXcbWrapper *self = g_object_new (G_PASTE_TYPE_XCB_WRAPPER, NULL);

    if (!self->priv->screen)
    {
        g_object_unref (self);
        return NULL;
    }

    return self;
}
