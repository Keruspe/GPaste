/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-item.h>
#include <gpaste-util.h>

struct _GPasteUiItem
{
    GPasteUiItemSkeleton parent_instance;
};

typedef struct
{
    GPasteClient   *client;
    GPasteSettings *settings;

    GtkWindow      *rootwin;

    guint64         index;
    gboolean        fake_index;
    gchar          *uuid;

    guint64         size_id;
} GPasteUiItemPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiItem, ui_item, G_PASTE_TYPE_UI_ITEM_SKELETON)

/**
 * g_paste_ui_item_activate:
 * @self: a #GPasteUiItem instance
 *
 * Activate/Select the item
 *
 * returns: whether there was anything to select or not
 */
G_PASTE_VISIBLE gboolean
g_paste_ui_item_activate (GPasteUiItem *self)
{
    g_return_val_if_fail (_G_PASTE_IS_UI_ITEM (self), FALSE);

    const GPasteUiItemPrivate *priv = _g_paste_ui_item_get_instance_private (self);

    if (!priv->uuid)
        return FALSE;

    g_paste_client_select (priv->client, priv->uuid, NULL, NULL);

    if (g_paste_settings_get_close_on_select (priv->settings))
        gtk_window_close (priv->rootwin); /* Exit the application */

    return TRUE;
}

static void
g_paste_ui_item_on_kind_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItem *self = user_data;
    const GPasteUiItemPrivate *priv = _g_paste_ui_item_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    GPasteItemKind kind = g_paste_client_get_element_kind_finish (priv->client, res, &error);

    if (error)
        return;

    GPasteUiItemSkeleton *sk = G_PASTE_UI_ITEM_SKELETON (self);

    g_paste_ui_item_skeleton_set_editable (sk, kind == G_PASTE_ITEM_KIND_TEXT);
    g_paste_ui_item_skeleton_set_uploadable (sk, kind == G_PASTE_ITEM_KIND_TEXT);
}

static void
_g_paste_ui_item_ready (GPasteUiItem *self,
                        const gchar  *txt)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autofree gchar *oneline = g_paste_util_replace (txt, "\n", " ");

    g_paste_client_get_element_kind (priv->client, priv->uuid, g_paste_ui_item_on_kind_ready, self);
    g_paste_ui_item_skeleton_set_index_and_uuid (G_PASTE_UI_ITEM_SKELETON (self), priv->index, priv->uuid);

    if (!priv->index)
    {
        g_autofree gchar *markup = g_markup_printf_escaped ("<b>%s</b>", oneline);
        g_paste_ui_item_skeleton_set_markup (G_PASTE_UI_ITEM_SKELETON (self), markup);
    }
    else
    {
        g_paste_ui_item_skeleton_set_text (G_PASTE_UI_ITEM_SKELETON (self), oneline);
    }
}

static void
g_paste_ui_item_on_text_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItem *self = user_data;
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    g_autofree gchar *txt = g_paste_client_get_element_finish (priv->client, res, &error);

    if (!txt || error)
        return;

    _g_paste_ui_item_ready (self, txt);
}

static void
g_paste_ui_item_on_item_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    GPasteUiItem *self = user_data;
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClientItem) txt = g_paste_client_get_element_at_index_finish (priv->client, res, &error);

    if (!txt || error)
        return;

    g_autofree gchar *uuid = priv->uuid;
    priv->uuid = g_strdup (g_paste_client_item_get_uuid (txt));

    _g_paste_ui_item_ready (self, g_paste_client_item_get_value (txt));
}

static void
g_paste_ui_item_reset_text (GPasteUiItem *self)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM (self));

    const GPasteUiItemPrivate *priv = _g_paste_ui_item_get_instance_private (self);

    if (priv->fake_index)
        g_paste_client_get_element (priv->client, priv->uuid, g_paste_ui_item_on_text_ready, self);
    else
        g_paste_client_get_element_at_index (priv->client, priv->index, g_paste_ui_item_on_item_ready, self);
}

/**
 * g_paste_ui_item_refresh:
 * @self: a #GPasteUiItem instance
 *
 * Refresh the item
 */
G_PASTE_VISIBLE void
g_paste_ui_item_refresh (GPasteUiItem *self)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM (self));

    g_paste_ui_item_reset_text (self);
}

static void
_g_paste_ui_item_set_index (GPasteUiItem *self,
                            guint64       index,
                            gboolean      fake_index)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);

    priv->index = index;
    priv->fake_index = fake_index;

    if (index != (guint64) -1)
    {
        g_paste_ui_item_reset_text (self);
        gtk_widget_show (GTK_WIDGET (self));
    }
    else if (priv->uuid)
    {
        gtk_widget_hide (GTK_WIDGET (self));
    }
}

/**
 * g_paste_ui_item_set_index:
 * @self: a #GPasteUiItem instance
 * @index: the index of the corresponding item
 *
 * Track a new index
 */
G_PASTE_VISIBLE void
g_paste_ui_item_set_index (GPasteUiItem *self,
                           guint64       index)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM (self));

    _g_paste_ui_item_set_index (self, index, FALSE);
}

/**
 * g_paste_ui_item_set_uuid:
 * @self: a #GPasteUiItem instance
 * @uuid: the uuid of the corresponding item
 *
 * Track a new uuid
 */
G_PASTE_VISIBLE void
g_paste_ui_item_set_uuid (GPasteUiItem *self,
                          const gchar  *uuid)
{
    g_return_if_fail (_G_PASTE_IS_UI_ITEM (self));

    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (self);
    g_autofree gchar *_uuid = priv->uuid;

    priv->uuid = g_strdup (uuid);

    _g_paste_ui_item_set_index (self, (guint64) -2, TRUE);
}

static void
g_paste_ui_item_dispose (GObject *object)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (object));

    g_clear_object (&priv->client);
    g_clear_object (&priv->settings);
    g_clear_pointer (&priv->uuid, g_free);

    G_OBJECT_CLASS (g_paste_ui_item_parent_class)->dispose (object);
}

static void
g_paste_ui_item_class_init (GPasteUiItemClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_item_dispose;
}

static void
g_paste_ui_item_init (GPasteUiItem *self)
{
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (self));

    priv->index = (guint64) -1;
}

/**
 * g_paste_ui_item_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @rootwin: the root #GtkWindow
 * @index: the index of the corresponding item
 *
 * Create a new instance of #GPasteUiItem
 *
 * Returns: a newly allocated #GPasteUiItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_item_new (GPasteClient   *client,
                     GPasteSettings *settings,
                     GtkWindow      *rootwin,
                     guint64         index)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_paste_ui_item_skeleton_new (G_PASTE_TYPE_UI_ITEM, client, settings, rootwin);
    GPasteUiItemPrivate *priv = g_paste_ui_item_get_instance_private (G_PASTE_UI_ITEM (self));

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->rootwin = rootwin;

    g_paste_ui_item_set_index (G_PASTE_UI_ITEM (self), index);

    return self;
}
