/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <adwaita.h>

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-panel-history.h>
#include <gpaste-ui-panel.h>

struct _GPasteUiPanel
{
    GtkBox parent_instance;
};

enum
{
    C_SELECTION_CHANGED,
    C_SETUP_MENU,
    C_SWITCH_ACTIVATED,
    C_SWITCH_CLICKED,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteClient      *client;
    GPasteSettings    *settings;
    GSignalGroup      *client_signals;

    AdwSidebar        *sidebar;
    AdwSidebarSection *section;
    AdwSidebarItem    *menu_item;
    AdwEntryRow       *switch_entry;
    GtkButton         *jump_button;
    GList             *histories;

    GtkWindow         *rootwin;
    GtkWidget         *search_entry;
    gboolean           inhibit_switch;

    guint64            c_signals[C_LAST_SIGNAL];
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
        g_paste_ui_panel_history_set_length (h->data, length);
}

static void
on_history_deleted (GPasteClient *client G_GNUC_UNUSED,
                    const gchar  *history,
                    gpointer      user_data)
{
    GPasteUiPanelPrivate *priv = user_data;

    GList *h = history_find (priv->histories, history);

    if (!h)
        return;

    if (g_paste_str_equal (history, G_PASTE_DEFAULT_HISTORY))
    {
        g_paste_ui_panel_history_set_length (h->data, 0);
        return;
    }

    priv->histories = g_list_remove_link (priv->histories, h);
    adw_sidebar_section_remove (priv->section, ADW_SIDEBAR_ITEM (h->data));
    g_list_free_1 (h);
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
on_selection_changed (GtkSelectionModel *model G_GNUC_UNUSED,
                      guint              position G_GNUC_UNUSED,
                      guint              n_items G_GNUC_UNUSED,
                      gpointer           user_data)
{
    GPasteUiPanelPrivate *priv = user_data;

    if (priv->inhibit_switch)
        return;

    AdwSidebarItem *item = priv->menu_item;

    if (!item || !G_PASTE_IS_UI_PANEL_HISTORY (item))
        return;

    g_paste_ui_panel_history_activate (G_PASTE_UI_PANEL_HISTORY (item));
}

static void
g_paste_ui_panel_add_history (GPasteUiPanelPrivate *priv,
                              const gchar          *history,
                              gboolean              select)
{
    GList *concurrent = history_find (priv->histories, history);
    GPasteUiPanelHistory *h;

    if (concurrent)
        h = concurrent->data;
    else
    {
        h = g_paste_ui_panel_history_new (priv->client, history);
        adw_sidebar_section_append (priv->section, ADW_SIDEBAR_ITEM (h));

        priv->histories = g_list_prepend (priv->histories, h);
    }

    if (select)
    {
        priv->inhibit_switch = TRUE;
        adw_sidebar_set_selected (priv->sidebar, adw_sidebar_item_get_index (ADW_SIDEBAR_ITEM (h)));
        priv->inhibit_switch = FALSE;
    }
}

typedef struct
{
    GPasteUiPanel *self;
    gchar         *name;
} HistoriesData;

static void
on_histories_ready (GObject      *source_object G_GNUC_UNUSED,
                    GAsyncResult *res,
                    gpointer      user_data)
{
    g_autofree HistoriesData *data = user_data;
    g_autofree gchar *current = data->name;

    if (!GTK_IS_WIDGET (data->self))
        return;

    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (data->self);
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) histories = g_paste_client_list_histories_finish (priv->client, res, &error);

    g_paste_ui_panel_add_history (priv, G_PASTE_DEFAULT_HISTORY, g_paste_str_equal (G_PASTE_DEFAULT_HISTORY, current));

    if (error)
    {
        g_critical ("Error while listing available histories: %s", error->message);
        return;
    }

    for (GStrv h = histories; *h; ++h)
        g_paste_ui_panel_add_history (priv, *h, g_paste_str_equal (*h, current));
}

static void
on_name_ready (GObject      *source_object G_GNUC_UNUSED,
               GAsyncResult *res,
               gpointer      user_data)
{
    if (!GTK_IS_WIDGET (user_data))
        return;

    GPasteUiPanel *self = user_data;
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (self);
    g_autofree gchar *name = g_paste_client_get_history_name_finish (priv->client, res, NULL);
    HistoriesData *data = g_new (HistoriesData, 1);

    data->self = self;
    data->name = g_steal_pointer (&name);

    g_paste_client_list_histories (priv->client, on_histories_ready, data);
}

static void
g_paste_ui_panel_do_switch (GPasteUiPanelPrivate *priv)
{
    const gchar *text = gtk_editable_get_text (GTK_EDITABLE (priv->switch_entry));

    g_paste_client_switch_history (priv->client, (text && *text) ? text : G_PASTE_DEFAULT_HISTORY, NULL, NULL);
    gtk_editable_set_text (GTK_EDITABLE (priv->switch_entry), "");

    gtk_widget_grab_focus (priv->search_entry);
}

static void
g_paste_ui_panel_switch_activated (AdwEntryRow *entry G_GNUC_UNUSED,
                                   gpointer     user_data)
{
    g_paste_ui_panel_do_switch (user_data);
}

static void
g_paste_ui_panel_switch_clicked (GtkButton *button G_GNUC_UNUSED,
                                 gpointer   user_data)
{
    g_paste_ui_panel_do_switch (user_data);
}

static void
on_setup_menu (AdwSidebar     *sidebar G_GNUC_UNUSED,
               AdwSidebarItem *item,
               gpointer        user_data)
{
    GPasteUiPanelPrivate *priv = user_data;

    priv->menu_item = item;
}

/* Context menu action callbacks */

typedef struct
{
    GPasteClient *client;
    gchar        *history;
    GtkEditable  *entry;
} BackupHistoryData;

static void
on_backup_response (GObject      *dialog,
                    GAsyncResult *result,
                    gpointer      user_data)
{
    g_autofree BackupHistoryData *data = user_data;
    g_autoptr (GPasteClient) client = data->client;
    g_autofree gchar *history = data->history;
    const gchar *response = adw_alert_dialog_choose_finish (ADW_ALERT_DIALOG (dialog), result);

    if (g_strcmp0 (response, "backup") == 0)
    {
        const gchar *text = gtk_editable_get_text (data->entry);
        if (text && *text)
            g_paste_client_backup_history (client, history, text, NULL, NULL);
    }
}

static void
on_backup_history_action (GSimpleAction *action    G_GNUC_UNUSED,
                          GVariant      *parameter G_GNUC_UNUSED,
                          gpointer       user_data)
{
    GPasteUiPanelPrivate *priv = user_data;
    AdwSidebarItem *item = priv->menu_item;

    if (!item || !G_PASTE_IS_UI_PANEL_HISTORY (item))
        return;

    const gchar *history = g_paste_ui_panel_history_get_history (G_PASTE_UI_PANEL_HISTORY (item));
    g_autofree gchar *default_name = g_strdup_printf ("%s_backup", history);
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (PACKAGE_STRING, _("Under which name do you want to backup this history?")));
    GtkWidget *entry = gtk_entry_new ();

    gtk_editable_set_text (GTK_EDITABLE (entry), default_name);
    adw_alert_dialog_add_responses (dialog, "cancel", _("Cancel"), "backup", _("Backup"), NULL);
    adw_alert_dialog_set_extra_child (dialog, entry);

    BackupHistoryData *data = g_new (BackupHistoryData, 1);
    data->client = g_object_ref (priv->client);
    data->history = g_strdup (history);
    data->entry = GTK_EDITABLE (entry);

    adw_alert_dialog_choose (dialog, GTK_WIDGET (priv->rootwin), NULL, on_backup_response, data);
}

typedef struct
{
    GPasteClient *client;
    gchar        *history;
} DeleteHistoryData;

static void
on_delete_confirmed (gboolean confirmed,
                     gpointer  user_data)
{
    g_autofree DeleteHistoryData *data = user_data;
    g_autoptr (GPasteClient) client = data->client;
    g_autofree gchar *history = data->history;

    if (confirmed)
        g_paste_client_delete_history (client, history, NULL, NULL);
}

static void
on_delete_history_action (GSimpleAction *action    G_GNUC_UNUSED,
                          GVariant      *parameter G_GNUC_UNUSED,
                          gpointer       user_data)
{
    GPasteUiPanelPrivate *priv = user_data;
    AdwSidebarItem *item = priv->menu_item;

    if (!item || !G_PASTE_IS_UI_PANEL_HISTORY (item))
        return;

    const gchar *history = g_paste_ui_panel_history_get_history (G_PASTE_UI_PANEL_HISTORY (item));
    DeleteHistoryData *data = g_new (DeleteHistoryData, 1);

    data->client = g_object_ref (priv->client);
    data->history = g_strdup (history);
    /* Translators: %s is the name of the history being deleted. */
    g_autofree gchar *msg = g_strdup_printf (_("Are you sure you want to delete \"%s\"?"), history);
    g_paste_gtk_util_confirm_dialog (priv->rootwin, _("Delete"), msg, on_delete_confirmed, data);
}

static void
on_empty_history_action (GSimpleAction *action    G_GNUC_UNUSED,
                         GVariant      *parameter G_GNUC_UNUSED,
                         gpointer       user_data)
{
    GPasteUiPanelPrivate *priv = user_data;
    AdwSidebarItem *item = priv->menu_item;

    if (!item || !G_PASTE_IS_UI_PANEL_HISTORY (item))
        return;

    const gchar *history = g_paste_ui_panel_history_get_history (G_PASTE_UI_PANEL_HISTORY (item));

    g_paste_gtk_util_empty_history (priv->rootwin, priv->client, priv->settings, history);
}

static void
g_paste_ui_panel_dispose (GObject *object)
{
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (object));

    if (priv->c_signals[C_SELECTION_CHANGED])
    {
        g_autoptr (GtkSelectionModel) selection = adw_sidebar_get_items (priv->sidebar);

        g_signal_handler_disconnect (selection, priv->c_signals[C_SELECTION_CHANGED]);
        g_signal_handler_disconnect (priv->switch_entry, priv->c_signals[C_SWITCH_ACTIVATED]);
        g_signal_handler_disconnect (priv->jump_button, priv->c_signals[C_SWITCH_CLICKED]);
        g_signal_handler_disconnect (priv->sidebar, priv->c_signals[C_SETUP_MENU]);
        priv->c_signals[C_SELECTION_CHANGED] = 0;
    }

    g_clear_object (&priv->client_signals);
    g_clear_object (&priv->client);

    g_clear_object (&priv->settings);

    /* FIXME: adw_sidebar_section_dispose crashes with leftover items in libadwaita 1.9.0; drain manually until fixed upstream */
    for (GList *h = priv->histories; h; h = h->next)
        adw_sidebar_section_remove (priv->section, ADW_SIDEBAR_ITEM (h->data));
    g_clear_pointer (&priv->histories, g_list_free);

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
    GtkBox *box = GTK_BOX (self);

    GtkWidget *sidebar = adw_sidebar_new ();
    priv->sidebar = ADW_SIDEBAR (sidebar);

    AdwSidebarSection *section = adw_sidebar_section_new ();
    priv->section = section;
    adw_sidebar_append (priv->sidebar, section);

    gtk_widget_set_vexpand (sidebar, TRUE);

    GtkSelectionModel *selection = adw_sidebar_get_items (priv->sidebar);
    priv->c_signals[C_SELECTION_CHANGED] = g_signal_connect (selection,
                                                             "selection-changed",
                                                             G_CALLBACK (on_selection_changed),
                                                             priv);

    GtkWidget *entry_box = gtk_list_box_new ();
    gtk_widget_add_css_class (entry_box, "boxed-list");
    gtk_list_box_set_selection_mode (GTK_LIST_BOX (entry_box), GTK_SELECTION_NONE);
    gtk_widget_set_margin_top (entry_box, 6);
    gtk_widget_set_margin_bottom (entry_box, 6);

    GtkWidget *switch_entry = adw_entry_row_new ();
    priv->switch_entry = ADW_ENTRY_ROW (switch_entry);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (switch_entry), _("Switch to history"));
    gtk_editable_set_enable_undo (GTK_EDITABLE (switch_entry), FALSE);

    GtkWidget *jump_button = gtk_button_new_from_icon_name ("go-jump-symbolic");
    priv->jump_button = GTK_BUTTON (jump_button);
    gtk_widget_set_valign (jump_button, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (jump_button, "flat");
    gtk_widget_set_tooltip_text (jump_button, _("Switch to"));
    adw_entry_row_add_suffix (ADW_ENTRY_ROW (switch_entry), jump_button);

    priv->c_signals[C_SWITCH_ACTIVATED] = g_signal_connect (G_OBJECT (switch_entry),
                                                            "entry-activated",
                                                            G_CALLBACK (g_paste_ui_panel_switch_activated),
                                                            priv);
    priv->c_signals[C_SWITCH_CLICKED] = g_signal_connect (G_OBJECT (jump_button),
                                                          "clicked",
                                                          G_CALLBACK (g_paste_ui_panel_switch_clicked),
                                                          priv);

    gtk_list_box_append (GTK_LIST_BOX (entry_box), switch_entry);

    gtk_box_append (box, sidebar);
    gtk_box_append (box, entry_box);
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

    GtkWidget *self = g_object_new (G_PASTE_TYPE_UI_PANEL,
                                      "orientation", GTK_ORIENTATION_VERTICAL,
                                      NULL);
    GPasteUiPanelPrivate *priv = g_paste_ui_panel_get_instance_private (G_PASTE_UI_PANEL (self));

    priv->client = g_object_ref (client);
    priv->settings = g_object_ref (settings);
    priv->rootwin = rootwin;
    priv->search_entry = GTK_WIDGET (search_entry);

    g_autoptr (GSimpleActionGroup) ag = g_simple_action_group_new ();

    g_autoptr (GSimpleAction) backup_action = g_simple_action_new ("backup-history", NULL);
    g_signal_connect (backup_action, "activate", G_CALLBACK (on_backup_history_action), priv);
    g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (backup_action));

    g_autoptr (GSimpleAction) delete_action = g_simple_action_new ("delete-history", NULL);
    g_signal_connect (delete_action, "activate", G_CALLBACK (on_delete_history_action), priv);
    g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (delete_action));

    g_autoptr (GSimpleAction) empty_action = g_simple_action_new ("empty-history", NULL);
    g_signal_connect (empty_action, "activate", G_CALLBACK (on_empty_history_action), priv);
    g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (empty_action));

    gtk_widget_insert_action_group (self, "panel", G_ACTION_GROUP (ag));

    g_autoptr (GMenu) menu = g_menu_new ();
    g_menu_append (menu, _("Backup"), "panel.backup-history");
    g_menu_append (menu, _("Empty"), "panel.empty-history");
    g_menu_append (menu, _("Delete"), "panel.delete-history");
    adw_sidebar_set_menu_model (priv->sidebar, G_MENU_MODEL (menu));

    GSignalGroup *client_signals = priv->client_signals = g_signal_group_new (G_PASTE_TYPE_CLIENT);
    g_signal_group_connect (client_signals,
                            "delete-history",
                            G_CALLBACK (on_history_deleted),
                            priv);
    g_signal_group_connect (client_signals,
                            "empty-history",
                            G_CALLBACK (on_history_emptied),
                            self);
    g_signal_group_connect (client_signals,
                            "switch-history",
                            G_CALLBACK (on_history_switched),
                            priv);
    g_signal_group_set_target (client_signals, client);

    priv->c_signals[C_SETUP_MENU] = g_signal_connect (priv->sidebar,
                                                       "setup-menu",
                                                       G_CALLBACK (on_setup_menu),
                                                       priv);

    g_paste_client_get_history_name (client, on_name_ready, self);

    return self;
}
