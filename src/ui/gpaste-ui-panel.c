// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <adwaita.h>

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-panel-history.h>
#include <gpaste-ui-panel.h>

enum
{
    C_SELECTION_CHANGED,
    C_SETUP_MENU,
    C_SWITCH_ACTIVATED,
    C_SWITCH_CLICKED,

    C_LAST_SIGNAL
};

struct _GPasteUiPanel
{
    GtkBox parent_instance;

    GPasteClient      *client;
    GPasteSettings    *settings;
    GSignalGroup      *client_signals;

    AdwSidebar        *sidebar;
    /* adw_sidebar_get_items() is (transfer full), so keep the one model we
     * connected to: asking again would leak a reference, and give us no
     * guarantee of getting the very object our handler is connected to back. */
    GtkSelectionModel *items;
    AdwSidebarSection *section;
    /* The item a context menu was opened on, which the menu actions act upon:
     * "setup-menu" hands it to us when the sidebar opens the menu, and hands us
     * NULL again once it closes, so this is only ever set while a menu is up. */
    AdwSidebarItem    *menu_item;
    AdwEntryRow       *switch_entry;
    GtkButton         *jump_button;
    GList             *histories;

    GtkWindow         *rootwin;
    GtkWidget         *search_entry;
    gboolean           inhibit_switch;

    gulong             c_signals[C_LAST_SIGNAL];
};

G_PASTE_DEFINE_TYPE (UiPanel, ui_panel, GTK_TYPE_BOX)

static gint32
history_equals (gconstpointer a,
                gconstpointer b)
{
    /* GCompareFunc hands us a gconstpointer; GObject accessors take a mutable
     * instance, so the cast is the caller's to make. */
    return !g_paste_str_equal (b, g_paste_ui_panel_history_get_history ((GPasteUiPanelHistory *) a));
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
void
g_paste_ui_panel_update_history_length (GPasteUiPanel *self,
                                        const gchar   *history,
                                        guint64        length)
{
    g_return_if_fail (G_PASTE_IS_UI_PANEL (self));

    GList *h = history_find (self->histories, history);

    if (h)
        g_paste_ui_panel_history_set_length (h->data, length);
}

static void
on_history_deleted (GPasteClient *client G_GNUC_UNUSED,
                    const gchar  *history,
                    gpointer      user_data)
{
    GPasteUiPanel *self = user_data;

    GList *h = history_find (self->histories, history);

    if (!h)
        return;

    if (g_paste_str_equal (history, G_PASTE_DEFAULT_HISTORY))
    {
        g_paste_ui_panel_history_set_length (h->data, 0);
        return;
    }

    /* The context menu may still be up on the very item we are dropping, and we
     * do not hold a reference to it. */
    if (self->menu_item == ADW_SIDEBAR_ITEM (h->data))
        self->menu_item = NULL;

    self->histories = g_list_remove_link (self->histories, h);

    /* Dropping an item shifts the selection, which we must not read back as the
     * user asking for a switch. */
    self->inhibit_switch = TRUE;
    adw_sidebar_section_remove (self->section, ADW_SIDEBAR_ITEM (h->data));
    self->inhibit_switch = FALSE;

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
g_paste_ui_panel_add_history (GPasteUiPanel *self,
                              const gchar          *history,
                              gboolean              select);

static void
on_history_switched (GPasteClient *client G_GNUC_UNUSED,
                     const gchar  *history,
                     gpointer      user_data)
{
    GPasteUiPanel *self = user_data;

    g_paste_ui_panel_add_history (self, history, TRUE);
}

/* The item a context menu was opened on, which the menu actions all act upon:
 * "setup-menu" hands it to us when the sidebar opens the menu, and hands us NULL
 * again once it closes, so this is only ever set while a menu is up. */
static GPasteUiPanelHistory *
g_paste_ui_panel_get_menu_history (GPasteUiPanel *self)
{
    return G_PASTE_IS_UI_PANEL_HISTORY (self->menu_item) ? G_PASTE_UI_PANEL_HISTORY (self->menu_item) : NULL;
}

static void
on_selection_changed (GtkSelectionModel *model G_GNUC_UNUSED,
                      guint              position G_GNUC_UNUSED,
                      guint              n_items G_GNUC_UNUSED,
                      gpointer           user_data)
{
    GPasteUiPanel *self = user_data;

    if (self->inhibit_switch)
        return;

    AdwSidebarItem *item = adw_sidebar_get_selected_item (self->sidebar);

    if (!G_PASTE_IS_UI_PANEL_HISTORY (item))
        return;

    g_paste_ui_panel_history_activate (G_PASTE_UI_PANEL_HISTORY (item));
}

static void
g_paste_ui_panel_add_history (GPasteUiPanel *self,
                              const gchar          *history,
                              gboolean              select)
{
    GList *concurrent = history_find (self->histories, history);
    GPasteUiPanelHistory *h;

    /* Every selection change we cause here is us following the daemon, never the
     * user picking a history, so none of them may switch anything: the sidebar
     * selects the very first item appended to it on its own, on top of the
     * explicit selection below. */
    self->inhibit_switch = TRUE;

    if (concurrent)
        h = concurrent->data;
    else
    {
        h = g_paste_ui_panel_history_new (self->client, history);
        adw_sidebar_section_append (self->section, ADW_SIDEBAR_ITEM (h));

        self->histories = g_list_prepend (self->histories, h);
    }

    if (select)
        adw_sidebar_set_selected (self->sidebar, adw_sidebar_item_get_index (ADW_SIDEBAR_ITEM (h)));

    self->inhibit_switch = FALSE;
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
    g_autoptr (GPasteUiPanel) self = data->self;
    g_autofree gchar *current = data->name;

    if (!self->client) /* panel was disposed while the call was in flight */
        return;

    g_autoptr (GError) error = NULL;
    g_auto (GStrv) histories = g_paste_client_list_histories_finish (self->client, res, &error);

    g_paste_ui_panel_add_history (self, G_PASTE_DEFAULT_HISTORY, g_paste_str_equal (G_PASTE_DEFAULT_HISTORY, current));

    if (error)
    {
        g_critical ("Error while listing available histories: %s", error->message);
        return;
    }

    for (GStrv h = histories; *h; ++h)
        g_paste_ui_panel_add_history (self, *h, g_paste_str_equal (*h, current));
}

static void
on_name_ready (GObject      *source_object G_GNUC_UNUSED,
               GAsyncResult *res,
               gpointer      user_data)
{
    g_autoptr (GPasteUiPanel) self = user_data;

    if (!self->client) /* panel was disposed while the call was in flight */
        return;

    g_autoptr (GError) error = NULL;
    g_autofree gchar *name = g_paste_client_get_history_name_finish (self->client, res, &error);

    /* Not fatal -- the list is still worth showing -- but with no current name
     * none of its rows will come out marked as the active one. */
    if (!name)
        g_warning ("Could not get the current history name: %s", error->message);

    HistoriesData *data = g_new (HistoriesData, 1);
    /* Grab the client before the steal below hands our ref to @data and leaves
     * @self NULL. */
    GPasteClient *client = self->client;

    data->self = g_steal_pointer (&self);
    data->name = g_steal_pointer (&name);

    g_paste_client_list_histories (client, on_histories_ready, data);
}

static void
g_paste_ui_panel_do_switch (GPasteUiPanel *self)
{
    const gchar *text = gtk_editable_get_text (GTK_EDITABLE (self->switch_entry));

    g_paste_client_switch_history (self->client, (text && *text) ? text : G_PASTE_DEFAULT_HISTORY, NULL, NULL);
    gtk_editable_set_text (GTK_EDITABLE (self->switch_entry), "");

    gtk_widget_grab_focus (self->search_entry);
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
    GPasteUiPanel *self = user_data;

    self->menu_item = item;
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
    GPasteUiPanel *self = user_data;
    GPasteUiPanelHistory *item = g_paste_ui_panel_get_menu_history (self);

    if (!item)
        return;

    const gchar *history = g_paste_ui_panel_history_get_history (item);
    g_autofree gchar *default_name = g_strdup_printf ("%s_backup", history);
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (PACKAGE_STRING, _("Under which name do you want to backup this history?")));
    GtkWidget *entry = gtk_entry_new ();

    gtk_editable_set_text (GTK_EDITABLE (entry), default_name);
    adw_alert_dialog_add_responses (dialog, "cancel", _("Cancel"), "backup", _("Backup"), NULL);
    adw_alert_dialog_set_extra_child (dialog, entry);

    BackupHistoryData *data = g_new (BackupHistoryData, 1);
    data->client = g_object_ref (self->client);
    data->history = g_strdup (history);
    data->entry = GTK_EDITABLE (entry);

    adw_alert_dialog_choose (dialog, GTK_WIDGET (self->rootwin), NULL, on_backup_response, data);
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
    GPasteUiPanel *self = user_data;
    GPasteUiPanelHistory *item = g_paste_ui_panel_get_menu_history (self);

    if (!item)
        return;

    const gchar *history = g_paste_ui_panel_history_get_history (item);
    DeleteHistoryData *data = g_new (DeleteHistoryData, 1);

    data->client = g_object_ref (self->client);
    data->history = g_strdup (history);
    /* Translators: %s is the name of the history being deleted. */
    g_autofree gchar *msg = g_strdup_printf (_("Are you sure you want to delete \"%s\"?"), history);
    g_paste_gtk_util_confirm_dialog (self->rootwin, _("Delete"), msg, on_delete_confirmed, data);
}

static void
on_empty_history_action (GSimpleAction *action    G_GNUC_UNUSED,
                         GVariant      *parameter G_GNUC_UNUSED,
                         gpointer       user_data)
{
    GPasteUiPanel *self = user_data;
    GPasteUiPanelHistory *item = g_paste_ui_panel_get_menu_history (self);

    if (!item)
        return;

    g_paste_gtk_util_empty_history (self->rootwin, self->client, self->settings,
                                    g_paste_ui_panel_history_get_history (item));
}

static void
g_paste_ui_panel_dispose (GObject *object)
{
    GPasteUiPanel *self = G_PASTE_UI_PANEL (object);

    if (self->c_signals[C_SELECTION_CHANGED])
    {
        g_signal_handler_disconnect (self->items, self->c_signals[C_SELECTION_CHANGED]);
        g_signal_handler_disconnect (self->switch_entry, self->c_signals[C_SWITCH_ACTIVATED]);
        g_signal_handler_disconnect (self->jump_button, self->c_signals[C_SWITCH_CLICKED]);
        g_signal_handler_disconnect (self->sidebar, self->c_signals[C_SETUP_MENU]);
        self->c_signals[C_SELECTION_CHANGED] = 0;
    }

    g_clear_object (&self->items);

    g_clear_object (&self->client_signals);
    g_clear_object (&self->client);

    g_clear_object (&self->settings);

    /* Borrowed: the section owns every item we appended to it, so only the list
     * spine is ours to free. Draining it by hand was a workaround for
     * adw_sidebar_section_dispose crashing on leftover items, fixed in the
     * libadwaita 1.10.alpha we now require. */
    g_clear_pointer (&self->histories, g_list_free);

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
    GtkBox *box = GTK_BOX (self);

    GtkWidget *sidebar = adw_sidebar_new ();
    self->sidebar = ADW_SIDEBAR (sidebar);

    AdwSidebarSection *section = adw_sidebar_section_new ();
    self->section = section;
    adw_sidebar_append (self->sidebar, section);

    gtk_widget_set_vexpand (sidebar, TRUE);

    self->items = adw_sidebar_get_items (self->sidebar);
    self->c_signals[C_SELECTION_CHANGED] = g_signal_connect (self->items,
                                                             "selection-changed",
                                                             G_CALLBACK (on_selection_changed),
                                                             self);

    GtkWidget *switch_entry = adw_entry_row_new ();
    self->switch_entry = ADW_ENTRY_ROW (switch_entry);
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (switch_entry), _("Switch to history"));
    gtk_editable_set_enable_undo (GTK_EDITABLE (switch_entry), FALSE);

    GtkWidget *jump_button = gtk_button_new_from_icon_name ("go-jump-symbolic");
    self->jump_button = GTK_BUTTON (jump_button);
    gtk_widget_set_valign (jump_button, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (jump_button, "flat");
    gtk_widget_set_tooltip_text (jump_button, _("Switch to"));
    adw_entry_row_add_suffix (ADW_ENTRY_ROW (switch_entry), jump_button);

    self->c_signals[C_SWITCH_ACTIVATED] = g_signal_connect (G_OBJECT (switch_entry),
                                                            "entry-activated",
                                                            G_CALLBACK (g_paste_ui_panel_switch_activated),
                                                            self);
    self->c_signals[C_SWITCH_CLICKED] = g_signal_connect (G_OBJECT (jump_button),
                                                          "clicked",
                                                          G_CALLBACK (g_paste_ui_panel_switch_clicked),
                                                          self);

    /* The sidebar's own suffix slot, rather than a boxed-list GtkListBox
     * alongside it: libadwaita styles and spaces the slot for us. */
    adw_sidebar_set_suffix (self->sidebar, switch_entry);

    gtk_box_append (box, sidebar);
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
GtkWidget *
g_paste_ui_panel_new (GPasteClient   *client,
                      GPasteSettings *settings,
                      GtkWindow      *rootwin,
                      GtkSearchEntry *search_entry)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (GTK_IS_WINDOW (rootwin), NULL);
    g_return_val_if_fail (GTK_IS_SEARCH_ENTRY (search_entry), NULL);

    GtkWidget *widget = g_object_new (G_PASTE_TYPE_UI_PANEL,
                                      "orientation", GTK_ORIENTATION_VERTICAL,
                                      NULL);
    GPasteUiPanel *self = G_PASTE_UI_PANEL (widget);

    self->client = g_object_ref (client);
    self->settings = g_object_ref (settings);
    self->rootwin = rootwin;
    self->search_entry = GTK_WIDGET (search_entry);

    g_autoptr (GSimpleActionGroup) ag = g_simple_action_group_new ();

    g_autoptr (GSimpleAction) backup_action = g_simple_action_new ("backup-history", NULL);
    g_signal_connect (backup_action, "activate", G_CALLBACK (on_backup_history_action), self);
    g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (backup_action));

    g_autoptr (GSimpleAction) delete_action = g_simple_action_new ("delete-history", NULL);
    g_signal_connect (delete_action, "activate", G_CALLBACK (on_delete_history_action), self);
    g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (delete_action));

    g_autoptr (GSimpleAction) empty_action = g_simple_action_new ("empty-history", NULL);
    g_signal_connect (empty_action, "activate", G_CALLBACK (on_empty_history_action), self);
    g_action_map_add_action (G_ACTION_MAP (ag), G_ACTION (empty_action));

    gtk_widget_insert_action_group (widget, "panel", G_ACTION_GROUP (ag));

    g_autoptr (GMenu) menu = g_menu_new ();
    g_menu_append (menu, _("Backup"), "panel.backup-history");
    g_menu_append (menu, _("Empty"), "panel.empty-history");
    g_menu_append (menu, _("Delete"), "panel.delete-history");
    adw_sidebar_set_menu_model (self->sidebar, G_MENU_MODEL (menu));

    GSignalGroup *client_signals = self->client_signals = g_signal_group_new (G_PASTE_TYPE_CLIENT);
    g_signal_group_connect (client_signals,
                            "delete-history",
                            G_CALLBACK (on_history_deleted),
                            self);
    g_signal_group_connect (client_signals,
                            "empty-history",
                            G_CALLBACK (on_history_emptied),
                            self);
    g_signal_group_connect (client_signals,
                            "switch-history",
                            G_CALLBACK (on_history_switched),
                            self);
    g_signal_group_set_target (client_signals, client);

    self->c_signals[C_SETUP_MENU] = g_signal_connect (self->sidebar,
                                                       "setup-menu",
                                                       G_CALLBACK (on_setup_menu),
                                                       self);

    g_paste_client_get_history_name (client, on_name_ready, g_object_ref (self));

    return widget;
}
