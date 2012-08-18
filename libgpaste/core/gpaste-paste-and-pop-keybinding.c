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

#include "gpaste-paste-and-pop-keybinding-private.h"
#include "gpaste-clipboard-common.h"
#include "gpaste-settings-keys.h"

#include <xcb/xtest.h>

#define G_PASTE_PASTE_AND_POP_KEYBINDING_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_PASTE_AND_POP_KEYBINDING, GPastePasteAndPopKeybindingPrivate))

G_DEFINE_TYPE (GPastePasteAndPopKeybinding, g_paste_paste_and_pop_keybinding, G_PASTE_TYPE_KEYBINDING)

struct _GPastePasteAndPopKeybindingPrivate
{
    GPasteHistory *history;
};

static void
g_paste_paste_and_pop_keybinding_dispose (GObject *object)
{
    g_object_unref (G_PASTE_PASTE_AND_POP_KEYBINDING (object)->priv->history);

    G_OBJECT_CLASS (g_paste_paste_and_pop_keybinding_parent_class)->dispose (object);
}

static void
g_paste_paste_and_pop_keybinding_class_init (GPastePasteAndPopKeybindingClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPastePasteAndPopKeybindingPrivate));

    G_OBJECT_CLASS (klass)->dispose = g_paste_paste_and_pop_keybinding_dispose;
}

static void
g_paste_paste_and_pop_keybinding_init (GPastePasteAndPopKeybinding *self G_GNUC_UNUSED)
{
}

static void
fake_keyboard (GPasteXcbWrapper *xcb_wrapper,
               xcb_connection_t *connection,
               xcb_screen_t     *screen,
               xcb_keysym_t      keysym,
               guint8            delay,
               guint8            event)
{
    xcb_keycode_t *keycode = (xcb_keycode_t *) xcb_key_symbols_get_keycode ((xcb_key_symbols_t *) g_paste_xcb_wrapper_get_keysyms (xcb_wrapper), keysym);

    if (!keycode)
        return;

    xcb_test_fake_input (connection,
                         event,
                         *keycode,
                         delay,
                         screen->root,
                         0, 0, 0);

    xcb_flush (connection);
}

static void
paste_and_pop_get_clipboard_data (GtkClipboard     *clipboard,
                                  GtkSelectionData *selection_data,
                                  guint             info,
                                  gpointer          user_data_or_owner)
{
    GPastePasteAndPopKeybinding *self = G_PASTE_PASTE_AND_POP_KEYBINDING (user_data_or_owner);
    GPasteHistory *history = self->priv->history;

    g_paste_clipboard_get_clipboard_data (clipboard,
                                          selection_data,
                                          info,
                                          G_OBJECT (g_paste_history_get (history, 0)));
    g_paste_history_remove (history, 0);
}

static void
paste_and_pop_clear_clipboard_data (GtkClipboard *clipboard G_GNUC_UNUSED,
                                    gpointer      user_data_or_owner G_GNUC_UNUSED)
{
}

static void
paste_and_pop (GPasteKeybinding *data)
{
    GPasteXcbWrapper *xcb_wrapper = g_paste_keybinding_get_xcb_wrapper (data);
    xcb_connection_t *connection = (xcb_connection_t *) g_paste_xcb_wrapper_get_connection (xcb_wrapper);
    xcb_screen_t *screen = (xcb_screen_t *) g_paste_xcb_wrapper_get_screen (xcb_wrapper);

    g_return_if_fail (screen); /* This should never happen */

    GtkTargetList *target_list = gtk_target_list_new (NULL, 0);

    gtk_target_list_add_text_targets (target_list, 0);

    gint n_targets;
    GtkTargetEntry *targets = gtk_target_table_new_from_list (target_list, &n_targets);
    GtkClipboard *clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);

    gtk_clipboard_set_with_data (clipboard,
                                 targets,
                                 n_targets,
                                 paste_and_pop_get_clipboard_data,
                                 paste_and_pop_clear_clipboard_data,
                                 data);

    fake_keyboard (xcb_wrapper, connection, screen, GDK_KEY_Shift_L, 0, XCB_KEY_PRESS);
    fake_keyboard (xcb_wrapper, connection, screen, GDK_KEY_Insert, 200, XCB_KEY_PRESS);
    fake_keyboard (xcb_wrapper, connection, screen, GDK_KEY_Shift_L, 0, XCB_KEY_RELEASE);
    fake_keyboard (xcb_wrapper, connection, screen, GDK_KEY_Insert, 0, XCB_KEY_RELEASE);

    gtk_target_table_free (targets, n_targets);
    gtk_target_list_unref (target_list);
}

/**
 * g_paste_paste_and_pop_keybinding_new:
 * @xcb_wrapper: a #GPasteXcbWrapper instance
 * @settings: a #GPasteSettings instance
 * @history: a #GPasteHistory instance
 *
 * Create a new instance of #GPastePasteAndPopKeybinding
 *
 * Returns: a newly allocated #GPastePasteAndPopKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPastePasteAndPopKeybinding *
g_paste_paste_and_pop_keybinding_new (GPasteXcbWrapper *xcb_wrapper,
                                      GPasteSettings   *settings,
                                      GPasteHistory    *history)
{
    g_return_val_if_fail (G_PASTE_IS_XCB_WRAPPER (xcb_wrapper), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);

    GPastePasteAndPopKeybinding *self = G_PASTE_PASTE_AND_POP_KEYBINDING (_g_paste_keybinding_new (G_PASTE_TYPE_PASTE_AND_POP_KEYBINDING,
                                                                                                   xcb_wrapper,
                                                                                                   settings,
                                                                                                   PASTE_AND_POP_KEY,
                                                                                                   g_paste_settings_get_paste_and_pop,
                                                                                                   (GPasteKeybindingFunc) paste_and_pop,
                                                                                                   NULL));
    self->priv->history = g_object_ref (history);

    return self;
}
