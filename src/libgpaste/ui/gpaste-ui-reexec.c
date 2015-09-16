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

#include <gpaste-ui-reexec.h>
#include <gpaste-util.h>

struct _GPasteUiReexec
{
    GtkButton parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GtkWindow    *topwin;
} GPasteUiReexecPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiReexec, g_paste_ui_reexec, GTK_TYPE_BUTTON)

static void
g_paste_ui_reexec_clicked (GtkButton *button)
{
    GPasteUiReexecPrivate *priv = g_paste_ui_reexec_get_instance_private (G_PASTE_UI_REEXEC (button));

    if (g_paste_util_confirm_dialog (priv->topwin, _("Restart"), _("Do you really want to restart the daemon?")))
        g_paste_client_reexecute (priv->client, NULL, NULL);
}

static void
g_paste_ui_reexec_dispose (GObject *object)
{
    GPasteUiReexecPrivate *priv = g_paste_ui_reexec_get_instance_private (G_PASTE_UI_REEXEC (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_reexec_parent_class)->dispose (object);
}

static void
g_paste_ui_reexec_class_init (GPasteUiReexecClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_reexec_dispose;
    GTK_BUTTON_CLASS (klass)->clicked = g_paste_ui_reexec_clicked;
}

static void
g_paste_ui_reexec_init (GPasteUiReexec *self)
{
    gtk_widget_set_tooltip_text (GTK_WIDGET (self), _("Restart the daemon"));
}

/**
 * g_paste_ui_reexec_new:
 * @topwin: the main #GtkWindow
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiReexec
 *
 * Returns: a newly allocated #GPasteUiReexec
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_reexec_new (GtkWindow    *topwin,
                       GPasteClient *client)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_REEXEC,
                                      "image", gtk_image_new_from_icon_name ("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON),
                                      NULL);
    GPasteUiReexecPrivate *priv = g_paste_ui_reexec_get_instance_private (G_PASTE_UI_REEXEC (self));

    priv->topwin = topwin;
    priv->client = g_object_ref (client);

    return self;
}
