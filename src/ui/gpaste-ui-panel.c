// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <adwaita.h>

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-panel-history.h>
#include <gpaste-ui-panel.h>
#include <gpaste-ui-window.h>

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
    /* The listing out, the one whose answer the sidebar takes: see
     * g_paste_ui_panel_refresh (). */
    GCancellable      *listing;

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
g_paste_ui_panel_remove_history (GPasteUiPanel *self,
                                 const gchar   *history)
{
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
on_history_deleted (GPasteClient *client G_GNUC_UNUSED,
                    const gchar  *history,
                    gpointer      user_data)
{
    g_paste_ui_panel_remove_history (user_data, history);
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
                              const gchar   *history,
                              const guint64 *length,
                              gboolean       select);

static void
on_history_changed (GPasteClient *client,
                    GParamSpec   *pspec G_GNUC_UNUSED,
                    gpointer      user_data)
{
    GPasteUiPanel *self = user_data;
    g_autofree gchar *history = g_paste_client_get_history_name (client);

    /* The property is cleared, not merely changed, when the daemon leaves the
     * bus: nothing to select then, and nothing to add under no name at all. */
    if (!history)
        return;

    /* No size to give: a switch says which history is current, not how much it
     * holds. A history that had to be created for it lands its size with the
     * refresh HistoriesChanged raises. */
    g_paste_ui_panel_add_history (self, history, NULL, TRUE);
}

static void g_paste_ui_panel_refresh (GPasteUiPanel *self);

/* A backup creates a history without switching to it, so the list has to be
 * rebuilt from a signal of its own -- the property cannot report a history that
 * appeared beside the current one. */
static void
on_histories_changed (GPasteClient *client G_GNUC_UNUSED,
                      gpointer      user_data)
{
    GPasteUiPanel *self = user_data;

    g_paste_ui_panel_refresh (self);
}

/* Whether a row can be clicked. The rows stay while there is no daemon, so
 * the sidebar does not empty and refill around a daemon that is only away for
 * a moment, but what they name is the last daemon's: until the next one's
 * listing says which it has, a click on one it lacks would create that history
 * there. So every row but the default -- always there to land on -- waits for
 * the listing naming it (g_paste_ui_panel_add_history ()), and the prune in
 * on_histories_ready () drops the rest (/ui/daemon-presence/listing-handoff). */
static void
g_paste_ui_panel_set_rows_enabled (GPasteUiPanel *self,
                                   gboolean       enabled)
{
    for (const GList *h = self->histories; h; h = h->next)
    {
        if (!g_paste_str_equal (g_paste_ui_panel_history_get_history (h->data), G_PASTE_DEFAULT_HISTORY))
            adw_sidebar_item_set_enabled (ADW_SIDEBAR_ITEM (h->data), enabled);
    }
}

/* Listed whenever a daemon becomes ready: it announces its history, not the
 * set of them. A daemon taking the name over from another is one becoming
 * ready too, the presence leaving ready in between. */
static void
on_daemon_presence_changed (GPasteClient *client,
                            GParamSpec   *pspec G_GNUC_UNUSED,
                            gpointer      user_data)
{
    GPasteUiPanel *self = user_data;

    if (g_paste_client_get_daemon_presence (client) == G_PASTE_DAEMON_PRESENCE_READY)
        g_paste_ui_panel_refresh (self);
    else
    {
        g_paste_clear_cancellable (&self->listing);
        g_paste_ui_panel_set_rows_enabled (self, FALSE);
    }
}

/* The item a context menu was opened on, which the menu actions all act upon:
 * "setup-menu" hands it to us when the sidebar opens the menu, and hands us NULL
 * again once it closes, so this is only ever set while a menu is up.
 *
 * A row that cannot be clicked has no actions either: the menu is the sidebar's
 * and stays up when its row is disabled under it, and an action reaching the
 * daemon now there for a history it may not have would create it
 * (g_paste_ui_panel_set_rows_enabled (), /ui/daemon-presence/listing-handoff). */
static GPasteUiPanelHistory *
g_paste_ui_panel_get_menu_history (GPasteUiPanel *self)
{
    if (!G_PASTE_IS_UI_PANEL_HISTORY (self->menu_item) ||
        !adw_sidebar_item_get_enabled (self->menu_item))
        return NULL;

    return G_PASTE_UI_PANEL_HISTORY (self->menu_item);
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

    g_paste_ui_panel_history_activate (G_PASTE_UI_PANEL_HISTORY (item), GTK_WIDGET (self));
}

static void on_history_delete (GPasteUiPanelHistory *item,
                               gpointer              user_data);

/* @length is what the listing said this history holds, or %NULL when whoever
 * calls has no size to give -- a switch says which history is current, not how
 * much it holds, and a row already showing its size must not be blanked by
 * one. */
static void
g_paste_ui_panel_add_history (GPasteUiPanel *self,
                              const gchar   *history,
                              const guint64 *length,
                              gboolean       select)
{
    GList *concurrent = history_find (self->histories, history);
    GPasteUiPanelHistory *h;

    /* Every selection change we cause here is us following the daemon, never the
     * user picking a history, so none of them may switch anything: the sidebar
     * selects the very first item appended to it on its own, on top of the
     * explicit selection below. */
    self->inhibit_switch = TRUE;

    if (concurrent)
    {
        h = concurrent->data;

        if (length)
            g_paste_ui_panel_history_set_length (h, *length);
        /* Named by the daemon there now: see g_paste_ui_panel_set_rows_enabled (). */
        adw_sidebar_item_set_enabled (ADW_SIDEBAR_ITEM (h), TRUE);
    }
    else
    {
        h = g_paste_ui_panel_history_new (self->client, history, (length) ? *length : 0);
        g_signal_connect_object (h, "delete", G_CALLBACK (on_history_delete), self, 0);
        adw_sidebar_section_append (self->section, ADW_SIDEBAR_ITEM (h));

        self->histories = g_list_prepend (self->histories, h);
    }

    if (select)
        adw_sidebar_set_selected (self->sidebar, adw_sidebar_item_get_index (ADW_SIDEBAR_ITEM (h)));

    self->inhibit_switch = FALSE;
}

/* The panel held weakly, as a listing it tracks for its dispose () to give up
 * (g_paste_ui_panel_refresh ()): a reference of the listing's own would keep
 * the panel from ever being disposed while one is out. */
typedef struct
{
    GWeakRef self;
    gchar   *name;
} HistoriesData;

static void
histories_data_free (HistoriesData *data)
{
    g_weak_ref_clear (&data->self);
    g_free (data->name);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (HistoriesData, histories_data_free)

static void
on_histories_ready (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
    g_autoptr (HistoriesData) data = user_data;
    g_autoptr (GError) error = NULL;
    g_autolist (GPasteClientHistory) histories = g_paste_client_list_histories_finish (G_PASTE_CLIENT (source_object), res, &error);

    /* Given up on, overtaken, its daemon gone or the panel disposed: it says
     * nothing of the rows the sidebar has now. */
    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

    /* Opened after the finish rather than first, as other callbacks do: the
     * reply is finished on the client it came from whatever became of the
     * panel, and a cancelled one needs no panel at all. */
    g_autoptr (GPasteUiPanel) self = g_weak_ref_get (&data->self);

    if (!self || !self->client)
        return;

    const gchar *current = data->name;

    /* The row to select is the history current now, not when the listing was
     * asked for: a switch landing while it was out has selected its own row
     * already, and this must not move it back. The listing's name stands in
     * only while the daemon has none to say. */
    g_autofree gchar *now = g_paste_client_get_history_name (self->client);
    const gchar *selected = (now) ? now : current;

    /* The default history is always drawn, listed or not: it is where a switch
     * away from a deleted history lands. Zero until the loop below names it,
     * which is also what it holds when there is no store for it yet -- but only
     * when there is a listing for it to be absent from. A failed one says
     * nothing about any history's size, so the row keeps the length it is
     * already showing rather than being blanked by a call that answered
     * nothing -- a daemon that could not read its storage, say, or one too
     * slow to answer. */
    guint64 none = 0;

    g_paste_ui_panel_add_history (self, G_PASTE_DEFAULT_HISTORY, (error) ? NULL : &none,
                                  g_paste_str_equal (G_PASTE_DEFAULT_HISTORY, selected));

    if (error)
    {
        /* Its daemon gone, the presence says so and lists again: nothing to
         * report (g_paste_client_is_daemon_gone_error ()). */
        if (g_paste_client_is_daemon_gone_error (error))
            return;

        /* A daemon that is there and could not list: the rows stay as they
         * are, clickable again -- what they name is the best there is, and
         * the next listing prunes what it does not have. No test shows it:
         * the critical is fatal in the suite, and g_test_expect_message ()
         * does not catch it under structured logging. */
        g_critical ("Error while listing available histories: %s", error->message);
        g_paste_ui_panel_set_rows_enabled (self, TRUE);
        return;
    }

    g_autoptr (GHashTable) listed = g_hash_table_new (g_str_hash, g_str_equal);

    for (const GList *h = histories; h; h = h->next)
    {
        GPasteClientHistory *history = h->data;
        const gchar *name = g_paste_client_history_get_name (history);
        guint64 length = g_paste_client_history_get_size (history);

        g_hash_table_add (listed, (gpointer) name);
        g_paste_ui_panel_add_history (self, name, &length, g_paste_str_equal (name, selected));
    }

    /* What the listing no longer names is gone. HistoryDeleted says so one at
     * a time, but a daemon coming back announces nothing of what went while
     * it was away -- a migration run without importing leaves only the default
     * history -- and a row left standing for one would create it anew when
     * clicked. The default row stays, as above, and so does the current one,
     * which a storage keeping nothing does not list -- the one this listing
     * was asked under and the one now, a switch landing while it was out. Named
     * first and dropped after, since dropping one edits the list being
     * walked. */
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

    for (const GList *h = self->histories; h; h = h->next)
    {
        const gchar *name = g_paste_ui_panel_history_get_history (h->data);

        if (!g_hash_table_contains (listed, name) &&
            !g_paste_str_equal (name, G_PASTE_DEFAULT_HISTORY) &&
            !g_paste_str_equal (name, current) &&
            !g_paste_str_equal (name, now))
            g_strv_builder_add (builder, name);
    }

    g_auto (GStrv) gone = g_strv_builder_end (builder);

    for (GStrv name = gone; *name; ++name)
        g_paste_ui_panel_remove_history (self, *name);
}

/* Rebuild the list. The listing answers each history's size along with its name,
 * so the whole sidebar costs one call; the current history's name comes off the
 * proxy's cached property. */
static void
g_paste_ui_panel_refresh (GPasteUiPanel *self)
{
    /* The list is what the daemon answers, so a disposed panel has nothing to
     * rebuild it from -- and nothing to draw it on either. Reached from a signal
     * as well as from the constructor, where dispose's order (the handlers go
     * before the client does) is the only thing that would say otherwise. */
    if (!self->client)
        return;

    /* Only a daemon that is there is listed: one becoming ready lists itself
     * (on_daemon_presence_changed ()). Mid-handoff the proxy still addresses
     * the daemon standing down, which a listing would reach
     * (/ui/daemon-presence/listing-handoff has the client announce a change
     * then). What reaches here from the daemon is guarded as
     * g_paste_client_g_signal () says; this keeps the rule the list follows
     * (g_paste_ui_history_refresh ()) for what reaches it from elsewhere. */
    if (g_paste_client_get_daemon_presence (self->client) != G_PASTE_DAEMON_PRESENCE_READY)
        return;

    HistoriesData *data = g_new (HistoriesData, 1);

    g_weak_ref_init (&data->self, self);
    data->name = g_paste_client_get_history_name (self->client);

    /* Not fatal -- the list is still worth showing -- but with no current name
     * none of its rows will come out marked as the active one. */
    if (!data->name)
        g_warning ("Could not get the current history name.");

    /* Only the latest listing may say which rows still exist: an earlier one
     * answering after it would prune what it added, and one answering after
     * its daemon went, what the next daemon lists. So a listing gives up the
     * one before it, and the daemon going gives up the one out
     * (on_daemon_presence_changed (); /ui/daemon-presence/listing-overtaken and
     * listing-abandoned), as does dispose ()
     * (/ui/daemon-presence/listing-after-close). */
    g_paste_clear_cancellable (&self->listing);
    self->listing = g_cancellable_new ();
    g_paste_client_list_histories (self->client, self->listing, on_histories_ready, data);
}

static void
g_paste_ui_panel_do_switch (GPasteUiPanel *self)
{
    const gchar *text = gtk_editable_get_text (GTK_EDITABLE (self->switch_entry));

    g_paste_client_switch_history (self->client, (text && *text) ? text : G_PASTE_DEFAULT_HISTORY,
                                   NULL /* cancellable */,
                                   g_paste_ui_report_void_cb,
                                   g_paste_ui_report_void (GTK_WIDGET (self), g_paste_client_switch_history_finish,
                                                           _("Could not switch history")));
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
    /* The window the toast goes to. The entry is inside the dialog, which is
     * already closed and unparented by the time the response arrives, so asking
     * it for its root finds no window and the failure goes unsaid -- and a
     * refused backup, the name being taken, is a common answer. */
    GtkWindow    *rootwin;
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
        {
            g_paste_client_backup_history (client, history, text,
                                           NULL /* cancellable */,
                                           g_paste_ui_report_void_cb,
                                           g_paste_ui_report_void (GTK_WIDGET (data->rootwin),
                                                                   g_paste_client_backup_history_finish,
                                                                   _("Could not back up the history")));
        }
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
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (adw_alert_dialog_new (_("Back Up History"),
                                                                     _("Choose a name for the copy. The history being backed up is left as it is.")));
    GtkWidget *entry = gtk_entry_new ();

    gtk_editable_set_text (GTK_EDITABLE (entry), default_name);
    adw_alert_dialog_add_responses (dialog, "cancel", _("Cancel"), "backup", _("Back Up"), NULL);
    adw_alert_dialog_set_response_appearance (dialog, "backup", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response (dialog, "cancel");
    adw_alert_dialog_set_close_response (dialog, "cancel");
    adw_alert_dialog_set_extra_child (dialog, entry);

    BackupHistoryData *data = g_new (BackupHistoryData, 1);
    data->client = g_object_ref (self->client);
    data->history = g_strdup (history);
    data->entry = GTK_EDITABLE (entry);
    data->rootwin = self->rootwin;

    adw_alert_dialog_choose (dialog, GTK_WIDGET (self->rootwin), NULL, on_backup_response, data);
}

typedef struct
{
    GPasteClient *client;
    gchar        *history;
    GtkWindow    *rootwin; /* borrowed: it outlives the dialog */
} DeleteHistoryData;

static void
on_delete_confirmed (gboolean confirmed,
                     gpointer user_data)
{
    g_autofree DeleteHistoryData *data = user_data;
    g_autoptr (GPasteClient) client = data->client;
    g_autofree gchar *history = data->history;

    if (confirmed)
    {
        g_paste_client_delete_history (client, history,
                                       NULL /* cancellable */,
                                       g_paste_ui_report_void_cb,
                                       g_paste_ui_report_void (GTK_WIDGET (data->rootwin),
                                                               g_paste_client_delete_history_finish,
                                                               _("Could not delete the history")));
    }
}

/* Asked for from the context menu, from a row's own delete button and from its
 * Delete key alike: three ways to the one question, which is asked here so the
 * three cannot come to word it differently. */
static void
g_paste_ui_panel_delete_history (GPasteUiPanel *self,
                                 const gchar   *history)
{
    DeleteHistoryData *data = g_new (DeleteHistoryData, 1);

    data->client = g_object_ref (self->client);
    data->history = g_strdup (history);
    data->rootwin = self->rootwin;
    /* Translators: %s is the name of the history being deleted. */
    g_autofree gchar *heading = g_strdup_printf (_("Delete \u201c%s\u201d?"), history);
    g_paste_gtk_util_confirm_dialog (self->rootwin,
                                     heading,
                                     _("The history and everything in it are deleted for good."),
                                     _("Delete"),
                                     ADW_RESPONSE_DESTRUCTIVE,
                                     on_delete_confirmed,
                                     data);
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

    g_paste_ui_panel_delete_history (self, g_paste_ui_panel_history_get_history (item));
}

static void
on_history_delete (GPasteUiPanelHistory *item,
                   gpointer              user_data)
{
    g_paste_ui_panel_delete_history (user_data, g_paste_ui_panel_history_get_history (item));
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
    g_paste_clear_cancellable (&self->listing);
    g_clear_object (&self->client);

    g_clear_object (&self->settings);

    /* Borrowed: the section owns every item we appended to it, so only the list
     * spine is ours to free. */
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
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (switch_entry), _("Switch or Create"));
    /* An AdwEntryRow has no subtitle to say it, and that a name nobody has used
     * before makes a history rather than failing to find one is not something a
     * user can be expected to guess. */
    gtk_widget_set_tooltip_text (switch_entry,
                                 _("Type the name of a history to switch to it, or a new name to create one"));
    gtk_editable_set_enable_undo (GTK_EDITABLE (switch_entry), FALSE);

    GtkWidget *jump_button = gtk_button_new_from_icon_name ("go-jump-symbolic");
    self->jump_button = GTK_BUTTON (jump_button);
    gtk_widget_set_valign (jump_button, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (jump_button, "flat");
    gtk_widget_set_tooltip_text (jump_button, _("Switch or Create"));
    gtk_accessible_update_property (GTK_ACCESSIBLE (jump_button), GTK_ACCESSIBLE_PROPERTY_LABEL, _("Switch or Create"), -1);
    adw_entry_row_add_suffix (ADW_ENTRY_ROW (switch_entry), jump_button);

    self->c_signals[C_SWITCH_ACTIVATED] = g_signal_connect (G_OBJECT (switch_entry),
                                                            "entry-activated",
                                                            G_CALLBACK (g_paste_ui_panel_switch_activated),
                                                            self);
    self->c_signals[C_SWITCH_CLICKED] = g_signal_connect (G_OBJECT (jump_button),
                                                          "clicked",
                                                          G_CALLBACK (g_paste_ui_panel_switch_clicked),
                                                          self);

    /* An AdwEntryRow is a list row and says so: put alone in the sidebar's
     * suffix slot -- an AdwBin -- libadwaita warns and leaves it unstyled. It
     * wants a GtkListBox, so give it the one-row boxed list every other
     * preferences row lives in. */
    GtkWidget *switch_list = gtk_list_box_new ();

    gtk_list_box_set_selection_mode (GTK_LIST_BOX (switch_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class (switch_list, "boxed-list");
    gtk_list_box_append (GTK_LIST_BOX (switch_list), switch_entry);

    adw_sidebar_set_suffix (self->sidebar, switch_list);

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
    g_menu_append (menu, _("Back Up"), "panel.backup-history");
    g_menu_append (menu, C_("verb", "Empty"), "panel.empty-history");
    g_menu_append (menu, _("Delete"), "panel.delete-history");
    adw_sidebar_set_menu_model (self->sidebar, G_MENU_MODEL (menu));

    GSignalGroup *client_signals = self->client_signals = g_signal_group_new (G_PASTE_TYPE_CLIENT);
    g_signal_group_connect (client_signals,
                            "history-deleted",
                            G_CALLBACK (on_history_deleted),
                            self);
    g_signal_group_connect (client_signals,
                            "history-emptied",
                            G_CALLBACK (on_history_emptied),
                            self);
    g_signal_group_connect (client_signals,
                            "notify::history",
                            G_CALLBACK (on_history_changed),
                            self);
    g_signal_group_connect (client_signals,
                            "histories-changed",
                            G_CALLBACK (on_histories_changed),
                            self);
    g_signal_group_connect (client_signals,
                            "notify::daemon-presence",
                            G_CALLBACK (on_daemon_presence_changed),
                            self);
    g_signal_group_set_target (client_signals, client);

    self->c_signals[C_SETUP_MENU] = g_signal_connect (self->sidebar,
                                                       "setup-menu",
                                                       G_CALLBACK (on_setup_menu),
                                                       self);

    on_daemon_presence_changed (client, NULL, self);

    return widget;
}
