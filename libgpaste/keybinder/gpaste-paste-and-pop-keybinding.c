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

#include "gpaste-paste-and-pop-keybinding-private.h"

#include "gpaste-clipboard-common.h"

#include <X11/extensions/XTest.h>

#define PASTE_AND_POP_WATCH_CLIPBOARD(clipboard)                     \
    gtk_clipboard_set_with_data (gtk_clipboard_get (clipboard),      \
                                 targets,                            \
                                 n_targets,                          \
                                 paste_and_pop_get_clipboard_data,   \
                                 paste_and_pop_clear_clipboard_data, \
                                 _data);

struct _GPastePasteAndPopKeybindingPrivate
{
    GPasteHistory           *history;

    gboolean                 delete;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPastePasteAndPopKeybinding, g_paste_paste_and_pop_keybinding, G_PASTE_TYPE_KEYBINDING)

static gboolean
do_pop (gpointer data)
{
    gpointer *_data = data;
    GPastePasteAndPopKeybindingPrivate *priv = g_paste_paste_and_pop_keybinding_get_instance_private (G_PASTE_PASTE_AND_POP_KEYBINDING (_data[0]));
    GPasteClipboardsManager *clipboards_manager = _data[1];
    g_free (_data);

    g_paste_history_remove (priv->history, 0);
    g_paste_clipboards_manager_unlock (clipboards_manager);
    return FALSE;
}

static void
paste_and_pop_get_clipboard_data (GtkClipboard     *clipboard,
                                  GtkSelectionData *selection_data,
                                  guint             info,
                                  gpointer          user_data_or_owner)
{
    gpointer *data = user_data_or_owner;
    GPastePasteAndPopKeybindingPrivate *priv = g_paste_paste_and_pop_keybinding_get_instance_private (G_PASTE_PASTE_AND_POP_KEYBINDING (data[0]));
    GPasteHistory *history = priv->history;
    gboolean delete = priv->delete;

    priv->delete = FALSE;

    GObject *item = G_OBJECT (g_paste_history_dup (history, 0));
    g_paste_clipboard_get_clipboard_data (clipboard,
                                          selection_data,
                                          info,
                                          item);

    if (delete)
        g_idle_add (do_pop, user_data_or_owner);
}

static void
paste_and_pop_clear_clipboard_data (GtkClipboard *clipboard          G_GNUC_UNUSED,
                                    gpointer      user_data_or_owner G_GNUC_UNUSED)
{
}

static void
paste_and_pop (GPasteKeybinding *self,
               gpointer          data)
{
    GtkTargetList *target_list = gtk_target_list_new (NULL, 0);

    gtk_target_list_add_text_targets (target_list, 0);

    gint n_targets;
    GtkTargetEntry *targets = gtk_target_table_new_from_list (target_list, &n_targets);

    GPastePasteAndPopKeybindingPrivate *priv = g_paste_paste_and_pop_keybinding_get_instance_private (G_PASTE_PASTE_AND_POP_KEYBINDING (self));
    GPasteClipboardsManager *clipboards_manager = data;

    priv->delete = TRUE;
    g_paste_clipboards_manager_lock (clipboards_manager);

    gpointer *_data = g_new (gpointer, 2);
    _data[0] = self;
    _data[1] = clipboards_manager;
    PASTE_AND_POP_WATCH_CLIPBOARD (GDK_SELECTION_CLIPBOARD)
    PASTE_AND_POP_WATCH_CLIPBOARD (GDK_SELECTION_PRIMARY)

    /* FIXME: split x11 stuff */
    Display *display = GDK_DISPLAY_XDISPLAY (self->display);
    XTestFakeKeyEvent (display, XKeysymToKeycode (display, GDK_KEY_Shift_L),  TRUE, CurrentTime);
    XFlush (display);
    XTestFakeKeyEvent (display, XKeysymToKeycode (display, GDK_KEY_Insert),   TRUE, CurrentTime);
    XFlush (display);
    XTestFakeKeyEvent (display, XKeysymToKeycode (display, GDK_KEY_Shift_L), FALSE, CurrentTime);
    XFlush (display);
    XTestFakeKeyEvent (display, XKeysymToKeycode (display, GDK_KEY_Insert),  FALSE, CurrentTime);
    XFlush (display);

    gtk_target_table_free (targets, n_targets);
    gtk_target_list_unref (target_list);
}

static void
g_paste_paste_and_pop_keybinding_dispose (GObject *object)
{
    GPastePasteAndPopKeybindingPrivate *priv = g_paste_paste_and_pop_keybinding_get_instance_private (G_PASTE_PASTE_AND_POP_KEYBINDING (object));

    if (priv->history)
        g_clear_object (&priv->history);

    G_OBJECT_CLASS (g_paste_paste_and_pop_keybinding_parent_class)->dispose (object);
}

static void
g_paste_paste_and_pop_keybinding_class_init (GPastePasteAndPopKeybindingClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_paste_and_pop_keybinding_dispose;
}

static void
g_paste_paste_and_pop_keybinding_init (GPastePasteAndPopKeybinding *self)
{
    GPastePasteAndPopKeybindingPrivate *priv = g_paste_paste_and_pop_keybinding_get_instance_private (self);

    priv->delete = FALSE;
}

/**
 * g_paste_paste_and_pop_keybinding_new:
 * @settings: a #GPasteSettings instance
 * @history: a #GPasteHistory instance
 *
 * Create a new instance of #GPastePasteAndPopKeybinding
 *
 * Returns: a newly allocated #GPastePasteAndPopKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_paste_and_pop_keybinding_new (GPasteSettings          *settings,
                                      GPasteHistory           *history,
                                      GPasteClipboardsManager *clipboards_manager)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (clipboards_manager), NULL);

    GPasteKeybinding *self = _g_paste_keybinding_new (G_PASTE_TYPE_PASTE_AND_POP_KEYBINDING,
                                                      settings,
                                                      G_PASTE_PASTE_AND_POP_SETTING,
                                                      g_paste_settings_get_paste_and_pop,
                                                      paste_and_pop,
                                                      clipboards_manager);

    g_return_val_if_fail (self, NULL);

    GPastePasteAndPopKeybindingPrivate *priv = g_paste_paste_and_pop_keybinding_get_instance_private (G_PASTE_PASTE_AND_POP_KEYBINDING (self));

    priv->history = g_object_ref (history);

    return self;
}
