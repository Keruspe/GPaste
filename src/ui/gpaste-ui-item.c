// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-item.h>

struct _GPasteUiItem
{
    GPasteUiItemSkeleton parent_instance;

    GPasteClient   *client;
    GPasteSettings *settings;

    GtkWindow      *rootwin;

    guint64         index;
    gboolean        fake_index;
    gchar          *uuid;
};

G_PASTE_DEFINE_TYPE (UiItem, ui_item, G_PASTE_TYPE_UI_ITEM_SKELETON)

/**
 * g_paste_ui_item_get_uuid:
 * @self: a #GPasteUiItem instance
 *
 * Get the uuid of the item currently displayed
 *
 * Returns: (nullable): the uuid, owned by the item
 */
G_PASTE_VISIBLE const gchar *
g_paste_ui_item_get_uuid (GPasteUiItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_UI_ITEM (self), NULL);

    return self->uuid;
}

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
    g_return_val_if_fail (G_PASTE_IS_UI_ITEM (self), FALSE);

    if (!self->uuid)
        return FALSE;

    g_paste_client_select (self->client, self->uuid, NULL, NULL);

    if (g_paste_settings_get_close_on_select (self->settings))
        gtk_window_close (self->rootwin); /* Exit the application */

    return TRUE;
}

static void
g_paste_ui_item_on_image_ready (GObject      *source_object G_GNUC_UNUSED,
                                GAsyncResult *res,
                                gpointer      user_data)
{
    g_autoptr (GPasteUiItem) self = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GdkTexture) texture = g_paste_gtk_util_get_image_finish (self->client, res, &error);

    if (!texture)
    {
        g_warning ("Failed to retrieve image: %s", error ? error->message : "no image returned");
        return;
    }

    g_paste_ui_item_skeleton_set_thumbnail (G_PASTE_UI_ITEM_SKELETON (self), texture);
}

static void
g_paste_ui_item_on_kind_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    g_autoptr (GPasteUiItem) self = user_data;
    g_autoptr (GError) error = NULL;
    GPasteItemKind kind = g_paste_client_get_element_kind_finish (self->client, res, &error);

    if (error)
        return;

    GPasteUiItemSkeleton *sk = G_PASTE_UI_ITEM_SKELETON (self);

    g_paste_ui_item_skeleton_set_editable (sk, kind == G_PASTE_ITEM_KIND_TEXT);
    g_paste_ui_item_skeleton_set_uploadable (sk, kind == G_PASTE_ITEM_KIND_TEXT);

    if (kind == G_PASTE_ITEM_KIND_IMAGE)
        g_paste_client_get_image (self->client, self->uuid, g_paste_ui_item_on_image_ready, g_object_ref (self));
    else
        g_paste_ui_item_skeleton_set_thumbnail (sk, NULL);
}

static void
_g_paste_ui_item_ready (GPasteUiItem *self,
                        const gchar  *txt)
{
    if (!txt)
        return;

    g_autofree gchar *oneline = g_strdelimit (g_strdup (txt), "\n\t", ' ');

    g_paste_ui_item_skeleton_set_index_and_uuid (G_PASTE_UI_ITEM_SKELETON (self), self->index, self->uuid);
    g_paste_client_get_element_kind (self->client, self->uuid, g_paste_ui_item_on_kind_ready, g_object_ref (self));

    if (!self->index)
        g_paste_ui_item_skeleton_set_text_bold (G_PASTE_UI_ITEM_SKELETON (self), oneline);
    else
        g_paste_ui_item_skeleton_set_text (G_PASTE_UI_ITEM_SKELETON (self), oneline);
}

static void
g_paste_ui_item_on_text_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    g_autoptr (GPasteUiItem) self = user_data;
    g_autoptr (GError) error = NULL;
    g_autofree gchar *txt = g_paste_client_get_element_finish (self->client, res, &error);

    if (!txt || error)
        return;

    _g_paste_ui_item_ready (self, txt);
}

static void
g_paste_ui_item_on_item_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    g_autoptr (GPasteUiItem) self = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClientItem) txt = g_paste_client_get_element_at_index_finish (self->client, res, &error);

    if (!txt || error)
        return;

    g_set_str (&self->uuid, g_paste_client_item_get_uuid (txt));

    _g_paste_ui_item_ready (self, g_paste_client_item_get_value (txt));
}

static void
g_paste_ui_item_reset_text (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

    if (self->fake_index)
        g_paste_client_get_element (self->client, self->uuid, g_paste_ui_item_on_text_ready, g_object_ref (self));
    else
        g_paste_client_get_element_at_index (self->client, self->index, g_paste_ui_item_on_item_ready, g_object_ref (self));
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
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

    g_paste_ui_item_reset_text (self);
}

static void
_g_paste_ui_item_set_index (GPasteUiItem *self,
                            guint64       index,
                            gboolean      fake_index)
{
    self->index = index;
    self->fake_index = fake_index;

    if (index != (guint64) -1)
    {
        g_paste_ui_item_reset_text (self);
        gtk_widget_set_visible (GTK_WIDGET (self), TRUE);
    }
    else if (self->uuid)
        gtk_widget_set_visible (GTK_WIDGET (self), FALSE);
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
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

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
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

    g_set_str (&self->uuid, uuid);

    _g_paste_ui_item_set_index (self, (guint64) -2, TRUE);
}

static void
g_paste_ui_item_dispose (GObject *object)
{
    GPasteUiItem *self = G_PASTE_UI_ITEM (object);

    g_clear_object (&self->client);
    g_clear_object (&self->settings);
    g_clear_pointer (&self->uuid, g_free);

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
    GPasteUiItem *priv = G_PASTE_UI_ITEM (self);

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
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);

    GtkWidget *self = g_paste_ui_item_skeleton_new (G_PASTE_TYPE_UI_ITEM, client, settings, rootwin);
    GPasteUiItem *priv = G_PASTE_UI_ITEM (self);

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->rootwin = rootwin;

    g_paste_ui_item_set_index (G_PASTE_UI_ITEM (self), index);

    return self;
}
