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

#include <gpaste-ui-empty.h>
#include <gpaste-util.h>

struct _GPasteUiEmpty
{
    GtkButton parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GtkWindow    *topwin;
} GPasteUiEmptyPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiEmpty, g_paste_ui_empty, GTK_TYPE_BUTTON)

static void
g_paste_ui_empty_clicked (GtkButton *button)
{
    GPasteUiEmptyPrivate *priv = g_paste_ui_empty_get_instance_private (G_PASTE_UI_EMPTY (button));

    /* Translators: this is the translation for emptying the history */
    if (g_paste_util_confirm_dialog (priv->topwin, _("Empty"), _("Do you really want to empty the history?")))
        g_paste_client_empty (priv->client, NULL, NULL);
}

static void
g_paste_ui_empty_dispose (GObject *object)
{
    GPasteUiEmptyPrivate *priv = g_paste_ui_empty_get_instance_private (G_PASTE_UI_EMPTY (object));

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
g_paste_ui_empty_init (GPasteUiEmpty *self)
{
    gtk_widget_set_tooltip_text (GTK_WIDGET (self), _("Empty history"));
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
    GPasteUiEmptyPrivate *priv = g_paste_ui_empty_get_instance_private (G_PASTE_UI_EMPTY (self));

    priv->topwin = topwin;
    priv->client = g_object_ref (client);

    return self;
}
