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
    GPasteXcbWrapper *xcb_wrapper;
    GSList           *keybindings;
    GThread          *thread;
    gboolean          keep_looping;
};

static gpointer
g_paste_keybinder_thread (gpointer data)
{
    GPasteKeybinderPrivate *priv = G_PASTE_KEYBINDER (data)->priv;
    xcb_connection_t *connection = (xcb_connection_t *) g_paste_xcb_wrapper_get_connection (priv->xcb_wrapper);
    xcb_generic_event_t *event;

    while (priv->keep_looping)
    {
        if ((event = xcb_wait_for_event (connection)))
        {
            if ((event->response_type & ~0x80) == XCB_KEY_PRESS)
            {
                xcb_ungrab_keyboard (connection, GDK_CURRENT_TIME);
                xcb_flush (connection);
                xcb_key_press_event_t *real_event = (xcb_key_press_event_t *) event;
                xcb_keycode_t keycode = real_event->detail;
                /* Ignore mouse modifiers */
                guint16 modifiers = real_event->state & 0xff;
                for (GSList *keybinding = priv->keybindings; keybinding; keybinding = g_slist_next (keybinding))
                {
                    GPasteKeybinding *real_keybinding = keybinding->data;
                    if (g_paste_keybinding_is_active (real_keybinding))
                    {
                        const xcb_keycode_t *keycodes = (const xcb_keycode_t *) g_paste_keybinding_get_keycodes (real_keybinding);
                        if (keycodes && g_paste_keybinding_get_modifiers (real_keybinding) == modifiers)
                        {
                            for (const xcb_keycode_t *k = keycodes; *k; ++k)
                            {
                                if (*k == keycode)
                                {
                                    g_paste_keybinding_notify (real_keybinding);
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            g_free (event);
        }
        else
            g_usleep (1000);
    }

    return NULL;
}

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
g_paste_keybinder_dispose (GObject *object)
{
    GPasteKeybinder *self = G_PASTE_KEYBINDER (object);
    GPasteKeybinderPrivate *priv = self->priv;

    priv->keep_looping = FALSE;
    g_thread_join (priv->thread);
    g_paste_keybinder_deactivate_all (self);
    g_object_unref (priv->xcb_wrapper);
    g_slist_foreach (priv->keybindings, (GFunc) g_object_unref, NULL);

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

    priv->keybindings = NULL;
    priv->keep_looping = TRUE;
}

/**
 * g_paste_keybinder_new:
 * @xcb_wrapper: a #GPasteXcbWrapper instance
 *
 * Create a new instance of #GPasteKeybinder
 *
 * Returns: a newly allocated #GPasteKeybinder
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinder *
g_paste_keybinder_new (GPasteXcbWrapper *xcb_wrapper)
{
    g_return_val_if_fail (G_PASTE_IS_XCB_WRAPPER (xcb_wrapper), NULL);

    GPasteKeybinder *self = g_object_new (G_PASTE_TYPE_KEYBINDER, NULL);
    GPasteKeybinderPrivate *priv = self->priv;

    priv->xcb_wrapper = g_object_ref (xcb_wrapper);
    priv->thread = g_thread_new ("gpaste-keybinder",
                                 g_paste_keybinder_thread,
                                 self);

    return self;
}
