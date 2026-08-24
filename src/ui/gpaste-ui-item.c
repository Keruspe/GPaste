// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-3/gpaste-gsettings-keys.h>

#include <gpaste-ui-color-swatch.h>
#include <gpaste-ui-edit-item.h>
#include <gpaste-ui-item.h>
#include <gpaste-ui-window.h>

struct _GPasteUiItem
{
    GtkBox parent_instance;

    GSignalGroup       *settings_signals;

    /* Everything this row can be asked to do, under the "item." prefix. The
     * inline buttons and the context menu both go through them, so there is one
     * implementation of each action and one place that says whether the row can
     * currently perform it. */
    GSimpleActionGroup *actions;
    GtkWidget          *favourite;       /* the star: shown while pinned, or on hover */
    GtkWidget          *remove;          /* shown on hover */
    GtkWidget          *menu;            /* the context menu, parented to the row */

    GtkWidget          *hbox;

    GtkLabel           *index_label;
    GtkInscription     *label;
    GtkPicture         *thumbnail;
    GtkWidget          *swatch;          /* colour items: what the value looks like */
    GtkWidget          *tooltip_preview; /* cached hover preview, rebuilt on thumbnail change */
    gboolean            editable;
    gboolean            uploadable;
    gboolean            favourited;
    gboolean            hovered;
    gboolean            selection_mode;

    GPasteClient       *client;
    GPasteSettings     *settings;

    GtkWindow          *rootwin;

    guint64             index;
    gboolean            fake_index;
    gchar              *uuid;

    /* Bumped on every (re)binding. GtkListView recycles row widgets, so a reply
     * for a previous binding must be dropped rather than overwrite the content
     * the widget has since been rebound to. */
    guint64             generation;
};

enum
{
    PROP_0,
    PROP_SELECTION_MODE,

    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

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

/* Keep a colour's preview proportional to a single line of this row's text.
 * Measure the label rather than the row: the swatch participates in the row's
 * allocation, so measuring that would make its own size feed back into it. */
static void
g_paste_ui_item_update_swatch_size (GPasteUiItem *self)
{
    gint minimum;
    gint natural;

    gtk_widget_measure (GTK_WIDGET (self->label),
                        GTK_ORIENTATION_VERTICAL,
                        -1,
                        &minimum,
                        &natural,
                        NULL,
                        NULL);
    g_paste_ui_color_swatch_set_size (self->swatch,
                                      CLAMP ((natural + 7) / 4 * 4, 16, 32));
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

static GSimpleAction *
item_action (GPasteUiItem *self,
             const gchar  *name)
{
    return G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self->actions), name));
}

/* A row is on screen before the item it stands for has been fetched, and a
 * recycled one is no longer the item it is still showing, so what it can do
 * follows the uuid it currently answers for: with none, every action is
 * disabled, which leaves the buttons insensitive and the menu items greyed
 * rather than aimed at whatever the row used to hold. */
static void
g_paste_ui_item_update_actions (GPasteUiItem *self)
{
    gboolean armed = (self->uuid != NULL);

    g_simple_action_set_enabled (item_action (self, "edit"), armed && self->editable);
    g_simple_action_set_enabled (item_action (self, "upload"), armed && self->uploadable);
    g_simple_action_set_enabled (item_action (self, "pin"), armed);
    g_simple_action_set_enabled (item_action (self, "delete"), armed);
}

/* Four buttons on every row read as a stack of button bars, so only the two
 * frequent ones are inline and they wait to be asked for. The star is the
 * exception: on a pinned item it is not a button but a badge, and a badge that
 * only appears under the pointer says nothing. Everything a row can do is in
 * its context menu, which is also the whole of the keyboard path. */
static void
g_paste_ui_item_update_actions_visibility (GPasteUiItem *self)
{
    gboolean revealed = self->hovered && !self->selection_mode;

    gtk_widget_set_visible (self->favourite, revealed || (self->favourited && !self->selection_mode));
    gtk_widget_set_visible (self->remove, revealed);
}

static void
g_paste_ui_item_set_editable (GPasteUiItem *self,
                              gboolean      editable)
{
    self->editable = editable;
}

static void
g_paste_ui_item_set_uploadable (GPasteUiItem *self,
                                gboolean      uploadable)
{
    self->uploadable = uploadable;
}

/* The star says what the item is, not what the button does, so its icon is the
 * state and clicking it asks for the other one. */
static void
g_paste_ui_item_set_favourited (GPasteUiItem *self,
                                gboolean      favourited)
{
    self->favourited = favourited;

    gtk_button_set_icon_name (GTK_BUTTON (self->favourite),
                              (favourited) ? "starred-symbolic" : "non-starred-symbolic");
    gtk_widget_set_tooltip_text (self->favourite, (favourited) ? _("Unpin") : _("Pin"));
    g_simple_action_set_state (item_action (self, "pin"), g_variant_new_boolean (favourited));
    g_paste_ui_item_update_actions_visibility (self);
}

static void
g_paste_ui_item_set_text (GPasteUiItem *self,
                          const gchar  *text)
{
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    gtk_inscription_set_attributes (self->label, NULL);
    gtk_inscription_set_text (self->label, text);
}

static void
g_paste_ui_item_set_text_bold (GPasteUiItem *self,
                               const gchar  *text)
{
    g_return_if_fail (g_utf8_validate (text, -1, NULL));

    g_autoptr (PangoAttrList) attrs = pango_attr_list_new ();
    pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
    gtk_inscription_set_attributes (self->label, attrs);
    gtk_inscription_set_text (self->label, text);
}

static void
g_paste_ui_item_apply_index (GPasteUiItem *self,
                             guint64       index)
{
    if (index == (guint64) -1 || index == (guint64) -2)
        gtk_label_set_text (self->index_label, "");
    else
    {
        g_autofree gchar *_index = g_strdup_printf ("%" G_GUINT64_FORMAT, index);

        gtk_label_set_text (self->index_label, _index);
    }
}

static void
g_paste_ui_item_set_thumbnail (GPasteUiItem *self,
                               GdkTexture   *texture)
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

/* The actions themselves. Each is disabled without a uuid (see
 * g_paste_ui_item_update_actions), so none has to check for one. */

static void
on_edit (GSimpleAction *action    G_GNUC_UNUSED,
         GVariant      *parameter G_GNUC_UNUSED,
         gpointer       user_data)
{
    GPasteUiItem *self = user_data;

    g_paste_ui_edit_item_show (self->client, self->rootwin, self->uuid);
}

/* The address is only copied once the daemon has taken it: an add can be refused
 * (a url shorter than min-text-item-size, say), and saying it was copied when it
 * was not is worse than saying nothing. */
static void
on_upload_address_copied (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
    g_autoptr (GPasteUiItem) self = user_data;
    g_autoptr (GError) error = NULL;
    g_autofree gchar *uuid = g_paste_client_add_text_finish (G_PASTE_CLIENT (source_object), res, &error);

    if (!uuid)
        g_warning ("Could not copy the address of the uploaded item: %s",
                   (error) ? error->message : "the daemon kept nothing");

    g_paste_gtk_util_toast (GTK_WIDGET (self),
                            (uuid) ? _("The item was uploaded, and its address copied")
                                   : _("The item was uploaded, but its address could not be copied"));
}

/* Upload answers the url it made and adds nothing itself, so the url is this
 * caller's to keep: put on the clipboard, which is what the keyboard shortcut's
 * own handler does with it daemon-side, and where a user who just uploaded
 * something wants it. Reporting the string generically would drop it -- the
 * report helper finishes a call answering a uuid, whose item comes back as an
 * update of its own, and a url is not that. */
static void
on_upload_done (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
    g_autoptr (GPasteUiItem) self = user_data;
    GPasteClient *client = G_PASTE_CLIENT (source_object);
    g_autoptr (GError) error = NULL;
    g_autofree gchar *url = g_paste_client_upload_finish (client, res, &error);

    if (!url)
    {
        g_warning ("Could not upload the item: %s", (error) ? error->message : "the daemon answered with no url");
        g_paste_gtk_util_toast (GTK_WIDGET (self), _("Could not upload the item"));

        return;
    }

    g_paste_client_add_text (client, url, on_upload_address_copied, g_object_ref (self));
}

static void
on_upload (GSimpleAction *action    G_GNUC_UNUSED,
           GVariant      *parameter G_GNUC_UNUSED,
           gpointer       user_data)
{
    GPasteUiItem *self = user_data;

    g_paste_client_upload (self->client, self->uuid, on_upload_done, g_object_ref (self));
}

static void
on_delete (GSimpleAction *action    G_GNUC_UNUSED,
           GVariant      *parameter G_GNUC_UNUSED,
           gpointer       user_data)
{
    GPasteUiItem *self = user_data;

    g_paste_client_delete_item (self->client, self->uuid,
                                g_paste_ui_report_void_cb,
                                g_paste_ui_report_void (GTK_WIDGET (self), g_paste_client_delete_item_finish,
                                                        _("Could not delete the item")));
}

/* Asks the daemon for the state the star is not showing; the answer comes back
 * as an update, which refills the row and so repaints it. Nothing is set here,
 * so a refused call simply leaves the star as it was. */
static void
on_pin (GSimpleAction *action    G_GNUC_UNUSED,
        GVariant      *parameter G_GNUC_UNUSED,
        gpointer       user_data)
{
    GPasteUiItem *self = user_data;

    g_paste_client_set_favourite (self->client, self->uuid, !self->favourited,
                                  g_paste_ui_report_void_cb,
                                  g_paste_ui_report_void (GTK_WIDGET (self), g_paste_client_set_favourite_finish,
                                                          (self->favourited) ? _("Could not unpin the item")
                                                                             : _("Could not pin the item")));
}

/**
 * g_paste_ui_item_popup_menu:
 * @self: a #GPasteUiItem instance
 *
 * Open the row's context menu, centred on the row -- the keyboard's way in,
 * where the pointer has the row's own gesture.
 */
void
g_paste_ui_item_popup_menu (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

    if (!self->uuid || self->selection_mode)
        return;

    gtk_popover_set_pointing_to (GTK_POPOVER (self->menu), NULL);
    gtk_popover_popup (GTK_POPOVER (self->menu));
}

static void
on_secondary_click (GtkGestureClick *gesture,
                    gint             n_press G_GNUC_UNUSED,
                    gdouble          x,
                    gdouble          y,
                    gpointer         user_data)
{
    GPasteUiItem *self = user_data;

    if (!self->uuid || self->selection_mode)
        return;

    gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    gtk_popover_set_pointing_to (GTK_POPOVER (self->menu), &(GdkRectangle) { (gint) x, (gint) y, 1, 1 });
    gtk_popover_popup (GTK_POPOVER (self->menu));
}

static void
on_long_press (GtkGestureLongPress *gesture G_GNUC_UNUSED,
               gdouble              x,
               gdouble              y,
               gpointer             user_data)
{
    GPasteUiItem *self = user_data;

    if (!self->uuid || self->selection_mode)
        return;

    gtk_popover_set_pointing_to (GTK_POPOVER (self->menu), &(GdkRectangle) { (gint) x, (gint) y, 1, 1 });
    gtk_popover_popup (GTK_POPOVER (self->menu));
}

static void
on_enter (GtkEventControllerMotion *controller G_GNUC_UNUSED,
          gdouble                   x          G_GNUC_UNUSED,
          gdouble                   y          G_GNUC_UNUSED,
          gpointer                  user_data)
{
    GPasteUiItem *self = user_data;

    self->hovered = TRUE;
    g_paste_ui_item_update_actions_visibility (self);
}

static void
on_leave (GtkEventControllerMotion *controller G_GNUC_UNUSED,
          gpointer                  user_data)
{
    GPasteUiItem *self = user_data;

    self->hovered = FALSE;
    g_paste_ui_item_update_actions_visibility (self);
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

    g_paste_client_select (self->client, self->uuid,
                           g_paste_ui_report_void_cb,
                           g_paste_ui_report_void (GTK_WIDGET (self), g_paste_client_select_finish,
                                                   _("Could not select the item")));

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
        g_paste_gtk_util_toast (GTK_WIDGET (self), _("Could not load an image preview"));

        /* Rather than leave the row showing an image that is not the one it is
         * now bound to. */
        g_paste_ui_item_set_thumbnail (self, NULL);
        return;
    }

    g_paste_ui_item_set_thumbnail (self, texture);
}

static void
_g_paste_ui_item_ready (GPasteUiItem     *self,
                        GPasteClientItem *item)
{
    const gchar *txt = g_paste_client_item_get_display_string (item);

    if (!txt)
        return;

    GPasteItemKind kind = g_paste_client_item_get_kind (item);
    g_autofree gchar *oneline = g_paste_util_one_line (txt);

    g_paste_ui_item_apply_index (self, self->index);

    /* The kind arrives with the item, so only an image still costs a call. */
    g_paste_ui_item_set_editable (self, kind == G_PASTE_ITEM_KIND_TEXT);
    g_paste_ui_item_set_uploadable (self, kind == G_PASTE_ITEM_KIND_TEXT);
    g_paste_ui_item_set_favourited (self, g_paste_client_item_is_favourite (item));
    g_paste_ui_item_update_actions (self);

    if (kind == G_PASTE_ITEM_KIND_IMAGE)
        g_paste_client_get_image (self->client, self->uuid, g_paste_ui_item_on_image_ready, async_callback_data_new (self));
    else
        g_paste_ui_item_set_thumbnail (self, NULL);

    /* A colour needs no call of its own: the item's value is its colour, the
     * "[Color]" a user reads in front of it being ours to add rather than the
     * daemon's. The swatch hides itself for anything GDK cannot read back. */
    g_paste_ui_color_swatch_set_color (self->swatch,
                                       (kind == G_PASTE_ITEM_KIND_COLOR) ? g_paste_client_item_get_value (item) : NULL);
    g_paste_ui_item_update_swatch_size (self);

    if (!self->index)
        g_paste_ui_item_set_text_bold (self, oneline);
    else
        g_paste_ui_item_set_text (self, oneline);
}

/* A search row knows its uuid and a positional one its index, so the two paths
 * differ only in which call they make; both come back with the same item. */
static void
g_paste_ui_item_on_uuid_ready (GObject      *source_object G_GNUC_UNUSED,
                               GAsyncResult *res,
                               gpointer      user_data)
{
    g_autoptr (AsyncCallbackData) data = user_data;
    GPasteUiItem *self = data->self;

    if (!g_paste_ui_item_still_wants (self, data))
        return;

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClientItem) item = g_paste_client_get_item_finish (self->client, res, &error);

    if (!item || error)
        return;

    _g_paste_ui_item_ready (self, item);
}

static void
g_paste_ui_item_on_index_ready (GObject      *source_object G_GNUC_UNUSED,
                                GAsyncResult *res,
                                gpointer      user_data)
{
    g_autoptr (AsyncCallbackData) data = user_data;
    GPasteUiItem *self = data->self;

    if (!g_paste_ui_item_still_wants (self, data))
        return;

    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClientItem) item = g_paste_client_get_item_at_index_finish (self->client, res, &error);

    if (!item || error)
        return;

    g_set_str (&self->uuid, g_paste_client_item_get_uuid (item));

    _g_paste_ui_item_ready (self, item);
}

static void
g_paste_ui_item_reset_text (GPasteUiItem *self)
{
    g_return_if_fail (G_PASTE_IS_UI_ITEM (self));

    if (self->fake_index)
        g_paste_client_get_item (self->client, self->uuid, g_paste_ui_item_on_uuid_ready, async_callback_data_new (self));
    else
        g_paste_client_get_item_at_index (self->client, self->index, g_paste_ui_item_on_index_ready, async_callback_data_new (self));
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
     * the item it used to display, and every action it offers is disabled with
     * it, since what a row can do is asked of the uuid it answers for. The star
     * reads @favourited on top of that, and a stale flag would flip the pin the
     * wrong way, so it is reset too: whatever the next item turns out to be, the
     * row is drawing nothing about it yet.
     *
     * What the row draws for itself goes with them, swatch and thumbnail alike:
     * a thumbnail is two round trips away (the item, then its bytes) and the
     * widget is handed straight to the next row, so the next image would be
     * shown as the previous one for that whole while — and for good, should the
     * call fail. A colour arrives with its item, but the row is drawing nothing
     * about that item yet either. */
    if (index != (guint64) -1)
        g_paste_ui_item_reset_text (self);
    else
    {
        g_clear_pointer (&self->uuid, g_free);
        g_paste_ui_item_apply_index (self, index);
        g_paste_ui_item_set_favourited (self, FALSE);
        g_paste_ui_item_update_actions (self);
        g_paste_ui_color_swatch_set_color (self->swatch, NULL);
        g_paste_ui_item_set_thumbnail (self, NULL);
    }
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
    g_clear_object (&self->actions);
    g_clear_object (&self->tooltip_preview);

    /* A popover is a child of the widget it points at, and chaining up does not
     * take it with the box's own children. */
    g_clear_pointer (&self->menu, gtk_widget_unparent);

    g_clear_pointer (&self->uuid, g_free);

    G_OBJECT_CLASS (g_paste_ui_item_parent_class)->dispose (object);
}

static void
g_paste_ui_item_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    GPasteUiItem *self = G_PASTE_UI_ITEM (object);

    switch (prop_id)
    {
    case PROP_SELECTION_MODE:
        g_value_set_boolean (value, self->selection_mode);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
g_paste_ui_item_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    GPasteUiItem *self = G_PASTE_UI_ITEM (object);

    switch (prop_id)
    {
    case PROP_SELECTION_MODE:
        self->selection_mode = g_value_get_boolean (value);
        g_paste_ui_item_update_actions_visibility (self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
g_paste_ui_item_class_init (GPasteUiItemClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_item_dispose;
    object_class->get_property = g_paste_ui_item_get_property;
    object_class->set_property = g_paste_ui_item_set_property;

    /**
     * GPasteUiItem:selection-mode:
     *
     * Whether the list is picking items to merge, in which case the row's own
     * actions have no business being offered: a click there is claimed by the
     * list's picking gesture before the button ever sees it.
     *
     * Bound from #GPasteUiHistory:selection-mode when the row is built, which is
     * what covers rows created later and rows recycled since.
     */
    properties[PROP_SELECTION_MODE] = g_param_spec_boolean ("selection-mode", NULL, NULL, FALSE,
                                                            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/* A row button: flat, so the list does not read as a stack of button bars, and
 * named for a screen reader, which an icon cannot name it for. */
static GtkWidget *
row_button_new (const gchar *icon_name,
                const gchar *label,
                const gchar *action_name)
{
    GtkWidget *button = gtk_button_new_from_icon_name (icon_name);

    gtk_widget_set_tooltip_text (button, label);
    gtk_accessible_update_property (GTK_ACCESSIBLE (button), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
    gtk_widget_add_css_class (button, "flat");
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_widget_set_visible (button, FALSE);
    gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action_name);

    return button;
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
    /* Dimmed, not insensitive: an insensitive label is one the theme greys and
     * the accessibility tree drops, and this one is neither disabled nor
     * unreadable -- it is simply not the row's point. */
    gtk_widget_add_css_class (index_label, "dim-label");
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

    /* An item is either an image or a colour, never both, so the two previews
     * share the one slot at the end of the row. */
    GtkWidget *swatch = self->swatch = g_paste_ui_color_swatch_new ();
    g_paste_ui_item_update_swatch_size (self);

    GtkWidget *thumbnail_container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand (thumbnail_container, FALSE);
    gtk_widget_set_halign (thumbnail_container, GTK_ALIGN_CENTER);

    gtk_box_append (GTK_BOX (thumbnail_container), thumbnail);
    gtk_box_append (GTK_BOX (thumbnail_container), swatch);
    gtk_box_append (GTK_BOX (hbox), thumbnail_container);

    self->favourite = row_button_new ("non-starred-symbolic", _("Pin"), "item.pin");
    self->remove = row_button_new ("edit-delete-symbolic", _("Delete"), "item.delete");
    gtk_box_append (GTK_BOX (hbox), self->favourite);
    gtk_box_append (GTK_BOX (hbox), self->remove);
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

    static const GActionEntry entries[] = {
        { "delete", on_delete, NULL, NULL,    NULL, { 0 } },
        { "edit",   on_edit,   NULL, NULL,    NULL, { 0 } },
        { "pin",    on_pin,    NULL, "false", NULL, { 0 } },
        { "upload", on_upload, NULL, NULL,    NULL, { 0 } },
    };

    self->actions = g_simple_action_group_new ();
    g_action_map_add_action_entries (G_ACTION_MAP (self->actions), entries, G_N_ELEMENTS (entries), self);
    gtk_widget_insert_action_group (widget, "item", G_ACTION_GROUP (self->actions));
    g_paste_ui_item_update_actions (self);

    /* Everything the row can do, for the pointer that has no button to press
     * and the keyboard that has no button to reach. */
    g_autoptr (GMenu) menu = g_menu_new ();

    g_autoptr (GMenu) edit_section = g_menu_new ();
    g_menu_append (edit_section, _("Edit…"), "item.edit");
    g_menu_append (edit_section, _("Upload"), "item.upload");
    g_menu_append_section (menu, NULL, G_MENU_MODEL (edit_section));

    g_autoptr (GMenu) item_section = g_menu_new ();
    g_menu_append (item_section, _("Pinned"), "item.pin");
    g_menu_append (item_section, _("Delete"), "item.delete");
    g_menu_append_section (menu, NULL, G_MENU_MODEL (item_section));

    self->menu = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
    gtk_popover_set_has_arrow (GTK_POPOVER (self->menu), FALSE);
    gtk_widget_set_halign (self->menu, GTK_ALIGN_START);
    gtk_widget_set_parent (self->menu, widget);

    GtkGesture *secondary = gtk_gesture_click_new ();
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (secondary), GDK_BUTTON_SECONDARY);
    g_signal_connect (secondary, "pressed", G_CALLBACK (on_secondary_click), self);
    gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (secondary));

    GtkGesture *long_press = gtk_gesture_long_press_new ();
    gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (long_press), TRUE);
    g_signal_connect (long_press, "pressed", G_CALLBACK (on_long_press), self);
    gtk_widget_add_controller (widget, GTK_EVENT_CONTROLLER (long_press));

    GtkEventController *motion = gtk_event_controller_motion_new ();
    g_signal_connect (motion, "enter", G_CALLBACK (on_enter), self);
    g_signal_connect (motion, "leave", G_CALLBACK (on_leave), self);
    gtk_widget_add_controller (widget, motion);

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
