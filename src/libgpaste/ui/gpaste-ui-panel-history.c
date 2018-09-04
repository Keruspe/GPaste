/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-panel-history.h>

#include "gpaste-gtk-compat.h"

struct _GPasteUiPanelHistory
{
    GtkListBoxRow parent_instance;
};

typedef struct
{
    GPasteClient *client;

    GtkLabel     *index_label;
    GtkLabel     *label;

    gchar        *history;
} GPasteUiPanelHistoryPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiPanelHistory, ui_panel_history, GTK_TYPE_LIST_BOX_ROW)

/**
 * g_paste_ui_panel_history_activate:
 * @self: a #GPasteUiPanelHistory instance
 *
 * Switch to this history
 */
G_PASTE_VISIBLE void
g_paste_ui_panel_history_activate (GPasteUiPanelHistory *self)
{
    g_return_if_fail (_G_PASTE_IS_UI_PANEL_HISTORY (self));

    const GPasteUiPanelHistoryPrivate *priv = _g_paste_ui_panel_history_get_instance_private (self);

    g_paste_client_switch_history (priv->client, priv->history, NULL, NULL);
}

/**
 * g_paste_ui_panel_history_set_length:
 * @self: a #GPasteUiPanelHistory instance
 * @length: the length of the #GPasteHistory
 *
 * Update the index label of this history
 */
G_PASTE_VISIBLE void
g_paste_ui_panel_history_set_length (GPasteUiPanelHistory *self,
                                     guint64               length)
{
    g_return_if_fail (_G_PASTE_IS_UI_PANEL_HISTORY (self));

    const GPasteUiPanelHistoryPrivate *priv = _g_paste_ui_panel_history_get_instance_private (self);
    g_autofree gchar *_length = g_strdup_printf("%" G_GUINT64_FORMAT, length);

    gtk_label_set_text (priv->index_label, _length);
}

/**
 * g_paste_ui_panel_history_get_history:
 * @self: a #GPasteUiPanelHistory instance
 *
 * Get the underlying history name
 *
 * Returns: the name of the history
 */
G_PASTE_VISIBLE const gchar *
g_paste_ui_panel_history_get_history (const GPasteUiPanelHistory *self)
{
    g_return_val_if_fail (_G_PASTE_IS_UI_PANEL_HISTORY (self), NULL);

    const GPasteUiPanelHistoryPrivate *priv = _g_paste_ui_panel_history_get_instance_private ((GPasteUiPanelHistory *) self);

    return priv->history;
}

static void
on_size_ready (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
    GPasteUiPanelHistory *self = user_data;

    g_paste_ui_panel_history_set_length (self, g_paste_client_get_history_size_finish (G_PASTE_CLIENT (source_object), res, NULL));
}

static void
g_paste_ui_panel_history_dispose (GObject *object)
{
    GPasteUiPanelHistoryPrivate *priv = g_paste_ui_panel_history_get_instance_private (G_PASTE_UI_PANEL_HISTORY (object));

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_panel_history_parent_class)->dispose (object);
}

static void
g_paste_ui_panel_history_class_init (GPasteUiPanelHistoryClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_panel_history_dispose;
}

static void
g_paste_ui_panel_history_init (GPasteUiPanelHistory *self)
{
    GPasteUiPanelHistoryPrivate *priv = g_paste_ui_panel_history_get_instance_private (self);
    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 10);
    GtkBox *box = GTK_BOX (hbox);
    GtkWidget *l = gtk_label_new ("");
    GtkLabel *label = priv->label = GTK_LABEL (l);
    GtkWidget *il = gtk_label_new ("");
    GtkLabel *index_label = priv->index_label = GTK_LABEL (il);

    gtk_widget_set_sensitive (il, FALSE);
    gtk_label_set_xalign (index_label, 1.0);
    gtk_label_set_width_chars (index_label, 3);
    gtk_label_set_selectable (index_label, FALSE);

    gtk_widget_set_margin_start (hbox, 5);
    gtk_widget_set_margin_end (hbox, 5);

    gtk_label_set_ellipsize (label, PANGO_ELLIPSIZE_END);
    gtk_box_pack_start (box, il, FALSE, FALSE);
    gtk_widget_set_hexpand (l, TRUE);
    gtk_widget_set_halign (l, TRUE);
    gtk_box_pack_start (box, l, TRUE, TRUE);
    gtk_container_add (GTK_CONTAINER (self), hbox);
}

/**
 * g_paste_ui_panel_history_new:
 * @client: a #GPasteClient instance
 * @history: the history we represent
 *
 * Create a new instance of #GPasteUiPanelHistory
 *
 * Returns: a newly allocated #GPasteUiPanelHistory
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_panel_history_new (GPasteClient *client,
                              const gchar  *history)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (g_utf8_validate (history, -1, NULL), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_PANEL_HISTORY,
                                      "width-request",  100,
                                      "height-request", 50,
                                      NULL);
    GPasteUiPanelHistoryPrivate *priv = g_paste_ui_panel_history_get_instance_private (G_PASTE_UI_PANEL_HISTORY (self));

    priv->client = g_object_ref (client);
    priv->history = g_strdup (history);

    gtk_label_set_text (priv->label, history);

    g_paste_client_get_history_size (client, history, on_size_ready, self);

    return self;
}
