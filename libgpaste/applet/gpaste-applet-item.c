/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-applet-item-private.h"

#include <glib/gi18n-lib.h>

struct _GPasteAppletItemPrivate
{
    GPasteClient *client;
    GtkLabel     *label;
    guint32       index;

    gulong        changed_id;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteAppletItem, g_paste_applet_item, GTK_TYPE_MENU_ITEM)

/* TODO: move me somewhere ( dupe from history ) */
static gchar *
g_paste_applet_item_replace (const gchar *text,
                             const gchar *pattern,
                             const gchar *substitution)
{
    G_PASTE_CLEANUP_FREE gchar *regex_string = g_regex_escape_string (pattern, -1);
    G_PASTE_CLEANUP_REGEX_UNREF GRegex *regex = g_regex_new (regex_string,
                                                             0, /* Compile options */
                                                             0, /* Match options */
                                                             NULL); /* Error */
    return g_regex_replace_literal (regex,
                                    text,
                                    (gssize) -1,
                                    0, /* Start position */
                                    substitution,
                                    0, /* Match options */
                                    NULL); /* Error */
}

static void
g_paste_applet_item_on_text_ready (GObject      *source_object G_GNUC_UNUSED,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
    GPasteAppletItemPrivate *priv = user_data;
    G_PASTE_CLEANUP_FREE gchar *nospace = g_paste_applet_item_replace (g_paste_client_get_element_finish (priv->client, res, NULL), "\n", "");
    G_PASTE_CLEANUP_FREE gchar *escaped = g_markup_escape_text (nospace, -1);
    G_PASTE_CLEANUP_FREE gchar *markup = (priv->index) ? NULL : g_strdup_printf ("<b>%s</b>", escaped);

    gtk_label_set_markup (priv->label, (markup) ? markup : escaped);
}

static void
g_paste_applet_item_reset_text (GPasteClient            *client,
                                GPasteAppletItemPrivate *priv)
{
    g_paste_client_get_element (client, priv->index, g_paste_applet_item_on_text_ready, priv);
}

static void
g_paste_applet_item_activate (GtkMenuItem *menu_item)
{
    GPasteAppletItemPrivate *priv = g_paste_applet_item_get_instance_private ((GPasteAppletItem *) menu_item);

    g_paste_client_select (priv->client, priv->index, NULL, NULL);

    GTK_MENU_ITEM_CLASS (g_paste_applet_item_parent_class)->activate (menu_item);
}

static void
g_paste_applet_item_dispose (GObject *object)
{
    GPasteAppletItemPrivate *priv = g_paste_applet_item_get_instance_private ((GPasteAppletItem *) object);

    if (priv->changed_id)
    {
        g_signal_handler_disconnect (priv->client, priv->changed_id);
        priv->changed_id = 0;
    }
    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_applet_item_parent_class)->dispose (object);
}

static void
g_paste_applet_item_class_init (GPasteAppletItemClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_applet_item_dispose;
    GTK_MENU_ITEM_CLASS (klass)->activate = g_paste_applet_item_activate;
}

static void
g_paste_applet_item_init (GPasteAppletItem *self)
{
    GPasteAppletItemPrivate *priv = g_paste_applet_item_get_instance_private (self);

    GtkWidget *label = gtk_label_new ("");
    priv->label = GTK_LABEL (label);

    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);

    gtk_container_add (GTK_CONTAINER (self), hbox);

    priv->changed_id = 0;
}

/**
 * g_paste_applet_item_new:
 * @client: a #GPasteClient
 * @index: the index of the corresponding item
 *
 * Create a new instance of #GPasteAppletItem
 *
 * Returns: a newly allocated #GPasteAppletItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_applet_item_new (GPasteClient *client,
                         guint32       index)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_APPLET_ITEM, NULL);
    GPasteAppletItemPrivate *priv = g_paste_applet_item_get_instance_private ((GPasteAppletItem *) self);

    priv->client = g_object_ref (client);
    priv->index = index;

    gtk_label_set_max_width_chars (priv->label, 80 /* FIXME */);
    gtk_label_set_ellipsize (priv->label, PANGO_ELLIPSIZE_END);
    gtk_box_pack_end (GTK_BOX (gtk_bin_get_child (GTK_BIN (self))), g_paste_applet_delete_new (client, index), FALSE, TRUE, 0);

    /* FIXME: watch for settings changes for element_size */
    priv->changed_id = g_signal_connect (G_OBJECT (client),
                                         "changed",
                                         G_CALLBACK (g_paste_applet_item_reset_text),
                                         priv);
    g_paste_applet_item_reset_text (client, priv);

    return self;
}
