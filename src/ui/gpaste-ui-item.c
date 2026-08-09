// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-3/gpaste-gsettings-keys.h>

#include <gpaste-ui-edit-item.h>
#include <gpaste-ui-item-action.h>
#include <gpaste-ui-item.h>

struct _GPasteUiItem
{
    GtkBox parent_instance;

    GSignalGroup   *settings_signals;

    GSList         *actions;
    GtkWidget      *edit;
    GtkWidget      *upload;

    GtkWidget      *hbox;

    GtkLabel       *index_label;
    GtkInscription *label;
    GtkPicture     *thumbnail;
    GtkWidget      *tooltip_preview; /* cached hover preview, rebuilt on thumbnail change */
    gboolean        editable;
    gboolean        uploadable;

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

G_PASTE_DEFINE_TYPE (UiItem, ui_item, GTK_TYPE_BOX)

static void
g_paste_ui_item_set_text_size (GPasteSettings *settings,
                               GParamSpec     *pspec G_GNUC_UNUSED,
                               gpointer        user_data)
{
    GPasteUiItem *self = user_data;
    guint64 size = g_paste_settings_get_element_size (settings);

    gtk_inscription_set_min_chars (self->label, size);
    gtk_inscription_set_nat_chars (self->label, size);
}

static void
g_paste_ui_item_on_images_preview_changed (GPasteSettings *settings,
                                           GParamSpec     *pspec G_GNUC_UNUSED,
                                           gpointer        user_data)
{
    GPasteUiItem *self = user_data;

    if (!self->thumbnail)
        return;

    gboolean has_image = gtk_picture_get_paintable (self->thumbnail) != NULL;

    if (has_image)
    {
        gint size = MAX ((gint) g_paste_settings_get_images_preview_size (settings), 10);
        gtk_widget_set_size_request (GTK_WIDGET (self->thumbnail), size, size);
    }

    gtk_widget_set_visible (GTK_WIDGET (self->thumbnail), has_image && g_paste_settings_get_images_preview (settings));
}

static void
g_paste_ui_item_set_editable (GPasteUiItem *self,
                              gboolean              editable)
{
    
    self->editable = editable;

    gtk_widget_set_sensitive (self->edit, editable);
}

static void
g_paste_ui_item_set_uploadable (GPasteUiItem *self,
                                gboolean              uploadable)
{
    
    self->uploadable = uploadable;

    gtk_widget_set_sensitive (self->upload, uploadable);
}

static void
g_paste_ui_item_set_text (GPasteUiItem *self,
                          const gchar          *text)
{
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    gtk_inscription_set_attributes (self->label, NULL);
    gtk_inscription_set_text (self->label, text);
}

static void
g_paste_ui_item_set_text_bold (GPasteUiItem *self,
                               const gchar          *text)
{
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    g_autoptr (PangoAttrList) attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    gtk_inscription_set_attributes (self->label, attrs);
    gtk_inscription_set_text (self->label, text);
}

static void
action_set_uuid (gpointer data,
                 gpointer user_data)
{
    GPasteUiItemAction *a = data;
    const gchar *uuid = user_data;

    g_paste_ui_item_action_set_uuid (a, uuid);
}

static void
g_paste_ui_item_apply_index_and_uuid (GPasteUiItem *self,
                                      guint64               index,
                                      const gchar          *uuid)
{
    if (index == (guint64) -1 || index == (guint64) -2)
        gtk_label_set_text (self->index_label, "");
    else
    {
        g_autofree gchar *_index = g_strdup_printf("%" G_GUINT64_FORMAT, index);

        gtk_label_set_text (self->index_label, _index);
    }

    g_slist_foreach (self->actions, action_set_uuid, (gpointer) uuid);
}

static void
g_paste_ui_item_set_thumbnail (GPasteUiItem *self,
                               GdkTexture           *texture)
{
    
    gtk_picture_set_paintable (self->thumbnail, texture ? GDK_PAINTABLE (texture) : NULL);
    /* The cached hover preview is tied to the old paintable; drop it. */
    g_clear_object (&self->tooltip_preview);
    g_paste_ui_item_on_images_preview_changed (self->settings, NULL, self);
}

/* Largest dimension, in pixels, of the hover preview. */
#define G_PASTE_UI_ITEM_PREVIEW_SIZE 400

/* Enlarge the small inline thumbnail to a detail preview on hover: the inline
 * picture is deliberately tiny (images-preview-size), so show the full image,
 * capped and aspect-preserved, in a custom tooltip.
 * gtk_widget_set_size_request () only sets a minimum, so it cannot bound the
 * tooltip's natural size: the paintable must be re-rendered with a capped
 * intrinsic size instead. */
static gboolean
g_paste_ui_item_on_thumbnail_query_tooltip (GtkWidget  *widget,
                                            gint        x        G_GNUC_UNUSED,
                                            gint        y        G_GNUC_UNUSED,
                                            gboolean    keyboard G_GNUC_UNUSED,
                                            GtkTooltip *tooltip,
                                            gpointer    user_data)
{
    GPasteUiItem *self = user_data;
    GdkPaintable *paintable = gtk_picture_get_paintable (GTK_PICTURE (widget));

    if (!paintable)
        return FALSE;

    /* query-tooltip fires repeatedly while hovering; build the scaled preview
     * once per thumbnail and reuse it (cleared in set_thumbnail/dispose). */
    if (!self->tooltip_preview)
    {
        gint width = gdk_paintable_get_intrinsic_width (paintable);
        gint height = gdk_paintable_get_intrinsic_height (paintable);
        g_autoptr (GdkPaintable) scaled = NULL;

        /* Re-render the paintable at the capped size: the result's intrinsic
         * size is the one the tooltip lays out with, which set_size_request
         * alone does not achieve here. */
        if (width > 0 && height > 0)
        {
            gdouble scale = MIN (1.0, MIN ((gdouble) G_PASTE_UI_ITEM_PREVIEW_SIZE / width,
                                           (gdouble) G_PASTE_UI_ITEM_PREVIEW_SIZE / height));
            g_autoptr (GtkSnapshot) snapshot = gtk_snapshot_new ();

            gdk_paintable_snapshot (paintable, GDK_SNAPSHOT (snapshot), width * scale, height * scale);
            scaled = gtk_snapshot_to_paintable (snapshot, NULL);
        }

        GtkWidget *preview = gtk_picture_new_for_paintable ((scaled) ? scaled : paintable);

        gtk_picture_set_content_fit (GTK_PICTURE (preview), GTK_CONTENT_FIT_CONTAIN);

        if (!scaled)
            gtk_widget_set_size_request (preview, G_PASTE_UI_ITEM_PREVIEW_SIZE, G_PASTE_UI_ITEM_PREVIEW_SIZE);

        self->tooltip_preview = g_object_ref_sink (preview);
    }

    gtk_tooltip_set_custom (tooltip, self->tooltip_preview);

    return TRUE;
}

static void
add_action (gpointer data,
            gpointer user_data)
{
    GtkWidget *w = data;
    GtkBox *b = user_data;

    gtk_widget_set_halign (w, GTK_ALIGN_START);
    gtk_box_append (b, w);
}

static void
delete_item_action (GPasteClient *client,
                    const gchar  *uuid)
{
    g_paste_client_delete (client, uuid, NULL, NULL);
}

static void
upload_item_action (GPasteClient *client,
                    const gchar  *uuid)
{
    g_paste_client_upload (client, uuid, NULL, NULL);
}


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

    g_paste_ui_item_set_thumbnail (self, texture);
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

    GPasteUiItem *sk = self;

    g_paste_ui_item_set_editable (sk, kind == G_PASTE_ITEM_KIND_TEXT);
    g_paste_ui_item_set_uploadable (sk, kind == G_PASTE_ITEM_KIND_TEXT);

    if (kind == G_PASTE_ITEM_KIND_IMAGE)
        g_paste_client_get_image (self->client, self->uuid, g_paste_ui_item_on_image_ready, async_callback_data_new (self));
    else
        g_paste_ui_item_set_thumbnail (sk, NULL);
}

static void
_g_paste_ui_item_ready (GPasteUiItem *self,
                        const gchar  *txt)
{
    if (!txt)
        return;

    g_autofree gchar *oneline = g_paste_util_one_line (txt);

    g_paste_ui_item_apply_index_and_uuid (self, self->index, self->uuid);
    g_paste_client_get_element_kind (self->client, self->uuid, g_paste_ui_item_on_kind_ready, async_callback_data_new (self));

    if (!self->index)
        g_paste_ui_item_set_text_bold (self, oneline);
    else
        g_paste_ui_item_set_text (self, oneline);
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
    g_clear_object (&self->settings_signals);
    g_clear_object (&self->tooltip_preview);
    g_clear_pointer (&self->actions, g_slist_free);

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
    self->index = (guint64) -1;

    GtkWidget *index_label = gtk_label_new ("");
    GtkWidget *label = gtk_inscription_new (NULL);

    self->index_label = GTK_LABEL (index_label);
    self->label = GTK_INSCRIPTION (label);
    self->editable = TRUE;

    gtk_widget_set_margin_start (index_label, 6);
    gtk_widget_set_margin_end (index_label, 6);
    gtk_widget_set_margin_top (index_label, 6);
    gtk_widget_set_margin_bottom (index_label, 6);
    gtk_widget_set_sensitive (index_label, FALSE);
    gtk_label_set_xalign (self->index_label, 1.0);
    gtk_label_set_width_chars (self->index_label, 3);
    gtk_label_set_max_width_chars (self->index_label, 3);
    gtk_label_set_selectable (self->index_label, FALSE);
    gtk_inscription_set_text_overflow (self->label, GTK_INSCRIPTION_OVERFLOW_ELLIPSIZE_END);
    gtk_inscription_set_xalign (self->label, 0.0);

    /* The skeleton is the row's own box: GtkListView wraps the factory widget in
     * its own GtkListItem, so there is nothing to nest inside here. */
    GtkWidget *hbox = GTK_WIDGET (self);
    self->hbox = hbox;
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_HORIZONTAL);
    gtk_box_set_spacing (GTK_BOX (self), 2);
    gtk_widget_set_margin_start (hbox, 6);
    gtk_widget_set_margin_end (hbox, 6);

    gtk_widget_set_halign (index_label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (hbox), index_label);

    gtk_widget_set_hexpand (label, TRUE);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    gtk_box_append (GTK_BOX (hbox), label);

    GtkWidget *thumbnail = gtk_picture_new ();
    self->thumbnail = GTK_PICTURE (thumbnail);
    gtk_picture_set_content_fit (self->thumbnail, GTK_CONTENT_FIT_CONTAIN);
    gtk_widget_set_visible (thumbnail, FALSE);
    gtk_widget_set_hexpand (thumbnail, TRUE);
    gtk_widget_set_halign (thumbnail, GTK_ALIGN_FILL);
    gtk_widget_set_has_tooltip (thumbnail, TRUE);
    g_signal_connect (thumbnail, "query-tooltip", G_CALLBACK (g_paste_ui_item_on_thumbnail_query_tooltip), self);

    GtkWidget *thumbnail_container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand (thumbnail_container, FALSE);
    gtk_widget_set_halign (thumbnail_container, GTK_ALIGN_CENTER);

    gtk_box_append (GTK_BOX (thumbnail_container), thumbnail);
    gtk_box_append (GTK_BOX (hbox), thumbnail_container);
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

    GtkWidget *widget = g_object_new (G_PASTE_TYPE_UI_ITEM, NULL);
    GPasteUiItem *self = G_PASTE_UI_ITEM (widget);

    self->client = g_object_ref (client);
    self->settings = g_object_ref (settings);
    self->rootwin = rootwin;

    GtkWidget *edit = g_paste_ui_edit_item_new (client, rootwin);
    GtkWidget *upload = g_paste_ui_item_action_new_simple (client, "document-send-symbolic", _("Upload"), upload_item_action);
    GtkWidget *delete = g_paste_ui_item_action_new_simple (client, "edit-delete-symbolic", _("Delete"), delete_item_action);

    self->edit = edit;
    self->upload = upload;

    self->actions = g_slist_prepend (self->actions, edit);
    self->actions = g_slist_prepend (self->actions, upload);
    self->actions = g_slist_prepend (self->actions, delete);

    /* Reverse so that pack_end order (edit|upload|delete) is preserved with append */
    g_autoptr (GSList) actions_reversed = g_slist_reverse (g_slist_copy (self->actions));
    g_slist_foreach (actions_reversed, add_action, GTK_BOX (self->hbox));

    GSignalGroup *settings_signals = self->settings_signals = g_signal_group_new (G_PASTE_TYPE_SETTINGS);
    g_signal_group_connect (settings_signals,
                            "notify::" G_PASTE_ELEMENT_SIZE_SETTING,
                            G_CALLBACK (g_paste_ui_item_set_text_size),
                            self);
    g_signal_group_connect (settings_signals,
                            "notify::" G_PASTE_IMAGES_PREVIEW_SETTING,
                            G_CALLBACK (g_paste_ui_item_on_images_preview_changed),
                            self);
    g_signal_group_connect (settings_signals,
                            "notify::" G_PASTE_IMAGES_PREVIEW_SIZE_SETTING,
                            G_CALLBACK (g_paste_ui_item_on_images_preview_changed),
                            self);
    g_signal_group_set_target (settings_signals, settings);
    g_paste_ui_item_set_text_size (settings, NULL, self);
    g_paste_ui_item_on_images_preview_changed (settings, NULL, self);

    g_paste_ui_item_set_index (self, index);

    return widget;
}
