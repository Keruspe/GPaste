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

#include "gpaste-ui-empty-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteUiEmptyPrivate
{
    GtkWindow    *topwin;
    GPasteClient *client;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiEmpty, g_paste_ui_empty, GTK_TYPE_BUTTON)

static void
g_paste_ui_empty_clicked (GtkButton *button)
{
    GPasteUiEmptyPrivate *priv = g_paste_ui_empty_get_instance_private ((GPasteUiEmpty *) button);
    GtkWidget *dialog = gtk_message_dialog_new (priv->topwin,
                                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                                GTK_MESSAGE_QUESTION,
                                                GTK_BUTTONS_OK_CANCEL,
                                                _("Do you really want to empty the history?"));

    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
        g_paste_client_empty (priv->client, NULL, NULL);
    gtk_widget_destroy (dialog);
}

static void
g_paste_ui_empty_dispose (GObject *object)
{
    GPasteUiEmptyPrivate *priv = g_paste_ui_empty_get_instance_private ((GPasteUiEmpty *) object);

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_empty_parent_class)->dispose (object);
}

static void
g_paste_ui_empty_class_init (GPasteUiEmptyClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_empty_dispose;
    GTK_BUTTON_CLASS (klass)->clicked = g_paste_ui_empty_clicked;
}

static void
g_paste_ui_empty_init (GPasteUiEmpty *self G_GNUC_UNUSED)
{
}

/**
 * g_paste_ui_empty_new:
 * @topwin: the main #GtkWindow
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiEmpty
 *
 * Returns: a newly allocated #GPasteUiEmpty
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_empty_new (GtkWindow    *topwin,
                      GPasteClient *client)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_EMPTY,
                                      "image", gtk_image_new_from_icon_name ("edit-clear-all-symbolic", GTK_ICON_SIZE_BUTTON),
                                      NULL);
    GPasteUiEmptyPrivate *priv = g_paste_ui_empty_get_instance_private ((GPasteUiEmpty *) self);

    priv->topwin = topwin;
    priv->client = g_object_ref (client);

    return self;
}
