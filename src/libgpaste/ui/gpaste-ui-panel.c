/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-history-actions.h>
#include <gpaste-ui-panel.h>

#include "gpaste-gtk-compat.h"

struct _GPasteUiPanel
{
    GtkBox parent_instance;
};

enum
{
    C_ACTIVATED,
    C_BUTTON_PRESSED,
    C_DELETE_HISTORY,
    C_EMPTY_HISTORY,
    C_SWITCH_ACTIVATED,
    C_SWITCH_CLICKED,
    C_SWITCH_HISTORY,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteClient           *client;
    GPasteSettings         *settings;
    GPasteUiHistoryActions *actions;

    GtkListBox             *list_box;
    GtkEntry               *switch_entry;
    GList                  *histories;

    GtkWidget              *search_entry;

    guint64                 c_signals[C_LAST_SIGNAL];
} GPasteUiPanelPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiPanel, ui_panel, GTK_TYPE_BOX)

static gint32
history_equals (gconstpointer a,
                gconstpointer b)
{
    return !g_paste_str_equal (b, g_paste_ui_panel_history_get_history (a));
}

static GList *
history_find (GList       *histories,
              const gchar *history)
{
    return g_list_find_custom (histories, history, history_equals);
}

/**
 * g_paste_ui_panel_update_history_length:
 * @self: a #GPasteUiPanel instance
 * @history: the history to update
 * @length: the new length
 *
 * Update the displayed length of the specified history
 */
G_PASTE_VISIBLE void
g_paste_ui_panel_update_history_length (GPasteUiPanel *self,
                                        const gchar   *history,
                                        guint64        length)
{
    g_return_if_fail (_G_PASTE_IS_UI_PANEL (self));

    const GPasteUiPanelPrivate *priv = _g_paste_ui_panel_get_instance_private (self);
    GList *h = history_find (priv->histories, history);

    if (h)
    {
        GPasteUiPanelHistory *hh = h->data;

        g_paste_ui_panel_history_set_length (hh, length);
    }
}

static void
on_history_deleted (GPasteClient *client G_GNUC_UNUSED,
                    const gchar  *history,
                    gpointer      user_data)
{
    GPasteUiPanelPrivate *priv = user_data;

    if (g_paste_str_equal (history, G_PASTE_DEFAULT_HISTORY))
        return;

    GList *h = history_find (priv->histories, history);

    if (!h)
        return;

    priv->histories = g_list_remove_link (priv->histories, h);
    gtk_container_remove (GTK_CONTAINER (priv->list_box), h->data);
}

static void
on_history_emptied (GPasteClient *client G_GNUC_UNUSED,
                    const gchar  *history,
                    gpointer      user_data)
{
    GPasteUiPanel *self = user_data;

    g_paste_ui_panel_update_history_length (self, history, 0);
}

static void
g_paste_ui_panel_add_history (GPasteUiPanelPrivate *priv,
                              const gchar          *history,
                              gboolean              select);

static void
on_history_switched (GPasteClient *client G_GNUC_UNUSED,
                     const gchar  *history,
                     gpointer      user_data)
{
    GPasteUiPanelPrivate *priv = user_data;

    g_paste_ui_panel_add_history (priv, history, TRUE);
}

static void
on_row_activated (GtkListBox    *panel     G_GNUC_UNUSED,
                  GtkListBoxRow *row,
                  gpointer       user_data G_GNUC_UNUSED)
{
    g_paste_ui_panel_history_activate (G_PASTE_UI_PANEL_HISTORY (row));
}

static void
g_paste_ui_panel_add_history (GPasteUiPanelPrivate *priv,
                              const gchar          *history,
                              gboolean              select)
{
    GtkContainer *c = GTK_CONTAINER (priv->list_box);

    GList *concurrent = history_find (priv->histories, history);
    GtkListBoxRow *row;

    if (concurrent)
    {
        row = concurrent->data;
    }
    else
    {
        GtkWidget *h = g_paste_ui_panel_history_new (priv->client, history);

        g_object_ref (h);
        gtk_container_add (c, h);
        gtk_widget_show_all (h);

        priv->histories = g_list_prepend (priv->histories, h);

        row = GTK_LIST_BOX_ROW (h);
    }

    if (select)
        gtk_list_box_select_row (priv->list_box, row);
}

typedef struct
{
    GPasteUiPanelPrivate *priv;
    gchar                *name;
} HistoriesData;

static void
on_histories_ready (GObject      *source_object G_GNUC_UNUSED,
                    GAsyncResult *res,
                    gpointer      user_data)
{
    g_autofree HistoriesData *data = user_data;
    GPasteUiPanelPrivate *priv = data->priv;
    g_auto (GStrv) histories = g_paste_client_list_histories_finish (priv->client, res, NULL);
    g_autofree gchar *current = data->name;

    g_paste_ui_panel_add_history (priv, G_PASTE_DEFAULT_HISTORY, g_paste_str_equal (G_PASTE_DEFAULT_HISTORY, current));
    for (GStrv h = histories; *h; ++h)
        g_paste_ui_panel_add_history (priv, *h, g_paste_str_equal (*h, current));
}

static void
on_name_ready (GObject      *source_object G_GNUC_UNUSED,
               GAsyncResult *res,
               gpointer      user_data)
{
    GPasteUiPanelPrivate *priv = user_data;
    gchar *name = g_paste_client_get_history_name_finish (priv->client, res, NULL);
    HistoriesData *data = g_malloc (sizeof (HistoriesData));

    data->priv = priv;
    data->name = name;

    g_paste_client_list_histories (priv->client, on_histories_ready, data);
}

static gboolean
g_paste_ui_panel_button_press_event (GtkWidget      *widget G_GNUC_UNUSED,
                                     GdkEventButton *event,
                                     gpointer        user_data)
{
    GPasteUiPanelPrivate *priv = user_data;
    GdkEvent *_event = (GdkEvent *) event;

    if (gdk_event_triggers_context_menu (_event))
    {
        gdouble y;

        if (!gdk_event_get_axis (_event, GDK_AXIS_Y, &y))
            return FALSE;

        g_paste_ui_history_actions_set_relative_to (priv->actions,
                                                    G_PASTE_UI_PANEL_HISTORY (gtk_list_box_get_row_at_y (priv->list_box, y)));
        gtk_widget_show_all (GTK_WIDGET (priv->actions));
    }

    return FALSE;
}

static void
g_paste_ui_panel_switch_activated (GtkEntry *entry,
                                   gpointer  user_data)
{
    GPasteUiPanelPrivate *priv = user_data;
    const gchar *text = gtk_entry_get_text (entry);

    g_paste_client_switch_history (priv->client, (text && *text) ? text : G_PASTE_DEFAULT_HISTORY, NULL, NULL);
    gtk_entry_set_text (entry, "");

    gtk_widget_grab_focus (priv->search_entry);
}

static void
g_paste_ui_panel_switch_clicked (GtkEntry            *entry,
                                 GtkEntryIconPosition icon_pos G_GNUC_UNUSED,
                                 GdkEvent            *event G_GNUC_UNUSED,
                                 gpointer             user_data)
{
    g_paste_ui_panel_switch_activated (entry, user_data);
}

static void
g_paste_ui_panel_dispose (GObject *object)
{
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (object));

    if (priv->c_signals[C_ACTIVATED])
    {
        g_signal_handler_disconnect (priv->list_box, priv->c_signals[C_ACTIVATED]);
        g_signal_handler_disconnect (priv->list_box, priv->c_signals[C_BUTTON_PRESSED]);
        g_signal_handler_disconnect (priv->switch_entry, priv->c_signals[C_SWITCH_ACTIVATED]);
        g_signal_handler_disconnect (priv->switch_entry, priv->c_signals[C_SWITCH_CLICKED]);
        priv->c_signals[C_ACTIVATED] = 0;
    }

    if (priv->client)
    {
        g_signal_handler_disconnect (priv->client, priv->c_signals[C_DELETE_HISTORY]);
        g_signal_handler_disconnect (priv->client, priv->c_signals[C_EMPTY_HISTORY]);
        g_signal_handler_disconnect (priv->client, priv->c_signals[C_SWITCH_HISTORY]);
        g_clear_object (&priv->client);
    }

    g_clear_object (&priv->settings);

    G_OBJECT_CLASS (g_paste_ui_panel_parent_class)->dispose (object);
}

static void
g_paste_ui_panel_class_init (GPasteUiPanelClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_panel_dispose;
}

static void
g_paste_ui_panel_init (GPasteUiPanel *self)
{
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (self);
    GtkWidget *list_box = gtk_list_box_new ();
    GtkWidget *switch_entry = gtk_entry_new ();
    GtkBox *box = GTK_BOX (self);

    priv->list_box = GTK_LIST_BOX (list_box);
    priv->switch_entry = GTK_ENTRY (switch_entry);

    gtk_style_context_add_class (gtk_widget_get_style_context (GTK_WIDGET (self)), GTK_STYLE_CLASS_SIDEBAR);
    gtk_entry_set_icon_from_icon_name (priv->switch_entry, GTK_ENTRY_ICON_SECONDARY, "go-jump-symbolic");
    gtk_widget_set_tooltip_text (switch_entry, _("Switch to"));
    gtk_widget_set_margin_top (switch_entry, 5);
    gtk_widget_set_margin_bottom (switch_entry, 5);
    gtk_entry_set_placeholder_text (priv->switch_entry, G_PASTE_DEFAULT_HISTORY);

    priv->c_signals[C_ACTIVATED] = g_signal_connect (G_OBJECT (priv->list_box),
                                                     "row-activated",
                                                     G_CALLBACK (on_row_activated),
                                                     NULL);
    priv->c_signals[C_BUTTON_PRESSED] = g_signal_connect (G_OBJECT (list_box),
                                                          "button-press-event",
                                                          G_CALLBACK (g_paste_ui_panel_button_press_event),
                                                          priv);
    priv->c_signals[C_SWITCH_ACTIVATED] = g_signal_connect (G_OBJECT (switch_entry),
                                                            "activate",
                                                            G_CALLBACK (g_paste_ui_panel_switch_activated),
                                                            priv);
    priv->c_signals[C_SWITCH_CLICKED] = g_signal_connect (G_OBJECT (switch_entry),
                                                          "icon-press",
                                                          G_CALLBACK (g_paste_ui_panel_switch_clicked),
                                                          priv);

    gtk_widget_set_valign (list_box, TRUE);
    gtk_box_pack_start (box, list_box, FALSE, TRUE);
    gtk_box_pack_start (box, switch_entry, FALSE, FALSE);
}

/**
 * g_paste_ui_panel_new:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @rootwin: the root #GtkWindow
 * @search_entry: the #GtkSearchEntry
 *
 * Create a new instance of #GPasteUiPanel
 *
 * Returns: a newly allocated #GPasteUiPanel
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_panel_new (GPasteClient   *client,
                      GPasteSettings *settings,
                      GtkWindow      *rootwin,
                      GtkSearchEntry *search_entry)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);
    g_return_val_if_fail (GTK_IS_SEARCH_ENTRY (search_entry), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_PANEL,
                                      "orientation", GTK_ORIENTATION_VERTICAL,
                                      NULL);
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (self));

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->actions = G_PASTE_UI_HISTORY_ACTIONS (g_paste_ui_history_actions_new (client, settings, rootwin));
    priv->search_entry = GTK_WIDGET (search_entry);

    priv->c_signals[C_DELETE_HISTORY] = g_signal_connect (priv->client,
                                                          "delete-history",
                                                          G_CALLBACK (on_history_deleted),
                                                          priv);
    priv->c_signals[C_EMPTY_HISTORY] = g_signal_connect (priv->client,
                                                         "empty-history",
                                                         G_CALLBACK (on_history_emptied),
                                                         self);
    priv->c_signals[C_SWITCH_HISTORY] = g_signal_connect (priv->client,
                                                          "switch-history",
                                                          G_CALLBACK (on_history_switched),
                                                          priv);

    g_paste_client_get_history_name (client, on_name_ready, priv);

    return self;
}
