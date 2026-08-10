// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-ui-panel-history.h>

struct _GPasteUiPanelHistory
{
    AdwSidebarItem parent_instance;

    GPasteClient *client;

    gchar        *history;
};

G_PASTE_DEFINE_TYPE (UiPanelHistory, ui_panel_history, ADW_TYPE_SIDEBAR_ITEM)

/**
 * g_paste_ui_panel_history_activate:
 * @self: a #GPasteUiPanelHistory instance
 *
 * Switch to this history
 */
void
g_paste_ui_panel_history_activate (GPasteUiPanelHistory *self)
{
    g_return_if_fail (G_PASTE_IS_UI_PANEL_HISTORY (self));

    g_paste_client_switch_history (self->client, self->history, NULL, NULL);
}

/**
 * g_paste_ui_panel_history_set_length:
 * @self: a #GPasteUiPanelHistory instance
 * @length: the length of the #GPasteHistory
 *
 * Update the displayed length of this history
 */
void
g_paste_ui_panel_history_set_length (GPasteUiPanelHistory *self,
                                     guint64               length)
{
    g_return_if_fail (G_PASTE_IS_UI_PANEL_HISTORY (self));

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
const gchar *
g_paste_ui_panel_history_get_history (GPasteUiPanelHistory *self)
{
    g_return_val_if_fail (G_PASTE_IS_UI_PANEL_HISTORY (self), NULL);

    GPasteUiPanelHistory *priv = (GPasteUiPanelHistory *) self;

    return priv->history;
}

static void
on_size_ready (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
    /* Held across the call (see _new): the sidebar drops us as soon as our
     * history is deleted, which can well happen before the size comes back. */
    g_autoptr (GPasteUiPanelHistory) self = user_data;

    g_autoptr (GError) error = NULL;
    guint64 size = g_paste_client_get_history_size_finish (G_PASTE_CLIENT (source_object), res, &error);

    /* A failure reads back as 0, which would show this history as empty. */
    if (error)
    {
        g_warning ("Could not get the size of history \"%s\": %s", self->history, error->message);
        return;
    }

    g_paste_ui_panel_history_set_length (self, size);
}

static void
g_paste_ui_panel_history_dispose (GObject *object)
{
    GPasteUiPanelHistory *self = G_PASTE_UI_PANEL_HISTORY (object);

    g_clear_object (&self->client);

    G_OBJECT_CLASS (g_paste_ui_panel_history_parent_class)->dispose (object);
}

static void
g_paste_ui_panel_history_finalize (GObject *object)
{
    GPasteUiPanelHistory *self = G_PASTE_UI_PANEL_HISTORY (object);

    g_clear_pointer (&self->history, g_free);

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
GPasteUiPanelHistory *
g_paste_ui_panel_history_new (GPasteClient *client,
                              const gchar  *history)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (g_utf8_validate (history, -1, NULL), NULL);

    GPasteUiPanelHistory *self = g_object_new (G_PASTE_TYPE_UI_PANEL_HISTORY, NULL);

    self->client = g_object_ref (client);
    self->history = g_strdup (history);

    adw_sidebar_item_set_title (ADW_SIDEBAR_ITEM (self), history);

    /* The callback owns this ref (see on_size_ready). */
    g_paste_client_get_history_size (client, history, on_size_ready, g_object_ref (self));

    return self;
}
