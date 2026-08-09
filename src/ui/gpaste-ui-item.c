// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>
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

    /* Bumped on every (re)binding. GtkListView recycles row widgets, so a reply
     * for a previous binding must be dropped rather than overwrite the content
     * the widget has since been rebound to. */
    guint64         generation;
};

G_PASTE_DEFINE_TYPE (UiItem, ui_item, G_PASTE_TYPE_UI_ITEM_SKELETON)

/* Carried by every step of the fill chain. @self is owned: the widget's only
 * owner is the list item, so a row recycled or a window closed mid-flight would
 * otherwise finalize it before the reply lands. */
typedef struct
{
    GPasteUiItem *self;
    guint64       generation;
} AsyncCallbackData;

static void
async_callback_data_free (AsyncCallbackData *data)
{
    g_object_unref (data->self);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (AsyncCallbackData, async_callback_data_free)

/* Whether the reply @data was made for is still the one @self is waiting on.
 *
 * The ref @data holds keeps the widget allocated, not alive: dispose() may
 * already have cleared the client the call was made on, and calling its
 * _finish() would be calling it on %NULL. So this is asked before touching the
 * result at all, not after. */
static gboolean
g_paste_ui_item_still_wants (GPasteUiItem      *self,
                             AsyncCallbackData *data)
{
    return self->client && data->generation == self->generation;
}

/* Only ever called while @self's generation is the current one — every step of
 * the chain bails before continuing it — so the new data inherits it. */
static AsyncCallbackData *
async_callback_data_new (GPasteUiItem *self)
{
    AsyncCallbackData *data = g_new (AsyncCallbackData, 1);

    data->self = g_object_ref (self);
    data->generation = self->generation;

    return data;
}

/**
 * g_paste_ui_item_get_uuid:
 * @self: a #GPasteUiItem instance
 *
 * Get the uuid of the item currently displayed
 *
 * Returns: (nullable): the uuid, owned by the item
 */
const gchar *
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
gboolean
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
    g_autoptr (AsyncCallbackData) data = user_data;
    GPasteUiItem *self = data->self;

    if (!g_paste_ui_item_still_wants (self, data))
        return;

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
    g_autoptr (AsyncCallbackData) data = user_data;
    GPasteUiItem *self = data->self;

    if (!g_paste_ui_item_still_wants (self, data))
        return;

    g_autoptr (GError) error = NULL;
    GPasteItemKind kind = g_paste_client_get_element_kind_finish (self->client, res, &error);

    if (error)
        return;

    GPasteUiItemSkeleton *sk = G_PASTE_UI_ITEM_SKELETON (self);

    g_paste_ui_item_skeleton_set_editable (sk, kind == G_PASTE_ITEM_KIND_TEXT);
    g_paste_ui_item_skeleton_set_uploadable (sk, kind == G_PASTE_ITEM_KIND_TEXT);

    if (kind == G_PASTE_ITEM_KIND_IMAGE)
        g_paste_client_get_image (self->client, self->uuid, g_paste_ui_item_on_image_ready, async_callback_data_new (self));
    else
        g_paste_ui_item_skeleton_set_thumbnail (sk, NULL);
}

static void
_g_paste_ui_item_ready (GPasteUiItem *self,
                        const gchar  *txt)
{
    if (!txt)
        return;

    g_autofree gchar *oneline = g_paste_util_one_line (txt);

    g_paste_ui_item_skeleton_set_index_and_uuid (G_PASTE_UI_ITEM_SKELETON (self), self->index, self->uuid);
    g_paste_client_get_element_kind (self->client, self->uuid, g_paste_ui_item_on_kind_ready, async_callback_data_new (self));

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
    g_autoptr (AsyncCallbackData) data = user_data;
    GPasteUiItem *self = data->self;

    if (!g_paste_ui_item_still_wants (self, data))
        return;

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
    g_autoptr (AsyncCallbackData) data = user_data;
    GPasteUiItem *self = data->self;

    if (!g_paste_ui_item_still_wants (self, data))
        return;

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
        g_paste_client_get_element (self->client, self->uuid, g_paste_ui_item_on_text_ready, async_callback_data_new (self));
    else
        g_paste_client_get_element_at_index (self->client, self->index, g_paste_ui_item_on_item_ready, async_callback_data_new (self));
}

static void
_g_paste_ui_item_set_index (GPasteUiItem *self,
                            guint64       index,
                            gboolean      fake_index)
{
    ++self->generation;

    self->index = index;
    self->fake_index = fake_index;

    /* The list view shows exactly the rows the model holds, so an unbound one is
     * simply not on screen — there is nothing to hide. Drop its uuid, though: a
     * recycled row must not be activatable — or, in merge mode, pickable — as
     * the item it used to display. */
    if (index != (guint64) -1)
        g_paste_ui_item_reset_text (self);
    else
        g_clear_pointer (&self->uuid, g_free);
}

/**
 * g_paste_ui_item_set_index:
 * @self: a #GPasteUiItem instance
 * @index: the index of the corresponding item
 *
 * Track a new index
 */
void
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
void
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
GtkWidget *
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
