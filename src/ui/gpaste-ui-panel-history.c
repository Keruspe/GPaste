/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-panel-history.h>

struct _GPasteUiPanelHistory
{
    AdwSidebarItem parent_instance;
};

typedef struct
{
    GPasteClient *client;

    gchar        *history;
} GPasteUiPanelHistoryPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiPanelHistory, ui_panel_history, ADW_TYPE_SIDEBAR_ITEM)

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
 * Update the displayed length of this history
 */
G_PASTE_VISIBLE void
g_paste_ui_panel_history_set_length (GPasteUiPanelHistory *self,
                                     guint64               length)
{
    g_return_if_fail (_G_PASTE_IS_UI_PANEL_HISTORY (self));

    g_autofree gchar *str = g_strdup_printf ("%" G_GUINT64_FORMAT, length);

    adw_sidebar_item_set_subtitle (ADW_SIDEBAR_ITEM (self), str);
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
g_paste_ui_panel_history_finalize (GObject *object)
{
    GPasteUiPanelHistoryPrivate *priv = g_paste_ui_panel_history_get_instance_private (G_PASTE_UI_PANEL_HISTORY (object));

    g_clear_pointer (&priv->history, g_free);

    G_OBJECT_CLASS (g_paste_ui_panel_history_parent_class)->finalize (object);
}

static void
g_paste_ui_panel_history_class_init (GPasteUiPanelHistoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_ui_panel_history_dispose;
    object_class->finalize = g_paste_ui_panel_history_finalize;
}

static void
g_paste_ui_panel_history_init (GPasteUiPanelHistory *self G_GNUC_UNUSED)
{
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
G_PASTE_VISIBLE GPasteUiPanelHistory *
g_paste_ui_panel_history_new (GPasteClient *client,
                              const gchar  *history)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (g_utf8_validate (history, -1, NULL), NULL);

    GPasteUiPanelHistory *self = g_object_new (G_PASTE_TYPE_UI_PANEL_HISTORY, NULL);
    GPasteUiPanelHistoryPrivate *priv = g_paste_ui_panel_history_get_instance_private (self);

    priv->client = g_object_ref (client);
    priv->history = g_strdup (history);

    adw_sidebar_item_set_title (ADW_SIDEBAR_ITEM (self), history);

    g_paste_client_get_history_size (client, history, on_size_ready, self);

    return self;
}
