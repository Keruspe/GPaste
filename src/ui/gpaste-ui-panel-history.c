// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-ui-panel-history.h>
#include <gpaste-ui-window.h>

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
 * @origin: a widget in the window to report a failure through -- an
 *          #AdwSidebarItem is a #GObject, so this one has none of its own
 *
 * Switch to this history
 */
void
g_paste_ui_panel_history_activate (GPasteUiPanelHistory *self,
                                   GtkWidget            *origin)
{
    g_return_if_fail (G_PASTE_IS_UI_PANEL_HISTORY (self));
    g_return_if_fail (GTK_IS_WIDGET (origin));

    g_paste_client_switch_history (self->client, self->history,
                                   g_paste_ui_report_void_cb,
                                   g_paste_ui_report_void (origin, g_paste_client_switch_history_finish,
                                                           _("Could not switch history")));
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

    return self->history;
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
 * @length: how many items it holds
 *
 * Create a new instance of #GPasteUiPanelHistory
 *
 * @length is passed in rather than asked for: the listing that named this
 * history answered its size along with it, so a sidebar of them costs one call
 * rather than one per row.
 *
 * Returns: a newly allocated #GPasteUiPanelHistory
 *          free it with g_object_unref
 */
GPasteUiPanelHistory *
g_paste_ui_panel_history_new (GPasteClient *client,
                              const gchar  *history,
                              guint64       length)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (g_utf8_validate (history, -1, NULL), NULL);

    GPasteUiPanelHistory *self = g_object_new (G_PASTE_TYPE_UI_PANEL_HISTORY, NULL);

    self->client = g_object_ref (client);
    self->history = g_strdup (history);

    adw_sidebar_item_set_title (ADW_SIDEBAR_ITEM (self), history);
    g_paste_ui_panel_history_set_length (self, length);

    return self;
}
