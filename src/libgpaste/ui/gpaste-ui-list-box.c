/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-ui-list-box-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteUiListBoxPrivate
{
    gulong activated_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiListBox, g_paste_ui_list_box, GTK_TYPE_LIST_BOX)

static void
on_row_activated (GtkListBox    *list_box G_GNUC_UNUSED,
                  GtkListBoxRow *row G_GNUC_UNUSED,
                  gpointer       user_data G_GNUC_UNUSED)
{
}

static void
g_paste_ui_list_box_dispose (GObject *object)
{
    GPasteUiListBoxPrivate *priv = g_paste_ui_list_box_get_instance_private (G_PASTE_UI_LIST_BOX (object));

    if (priv->activated_id)
    {
        g_signal_handler_disconnect (object, priv->activated_id);
        priv->activated_id = 0;
    }

    G_OBJECT_CLASS (g_paste_ui_list_box_parent_class)->dispose (object);
}

static void
g_paste_ui_list_box_class_init (GPasteUiListBoxClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_list_box_dispose;
}

static void
g_paste_ui_list_box_init (GPasteUiListBox *self)
{
    GPasteUiListBoxPrivate *priv = g_paste_ui_list_box_get_instance_private (self);

    priv->activated_id = g_signal_connect (G_OBJECT (self),
                                           "row-activated",
                                           G_CALLBACK (on_row_activated),
                                           self);
}

/**
 * g_paste_ui_list_box_new:
 *
 * Create a new instance of #GPasteUiListBox
 *
 * Returns: a newly allocated #GPasteUiListBox
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_list_box_new (void)
{
    return gtk_widget_new (G_PASTE_TYPE_UI_LIST_BOX, NULL);
}
