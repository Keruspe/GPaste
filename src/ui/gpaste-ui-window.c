// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-dialog.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-header.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-new-item.h>
#include <gpaste-ui-window.h>
#include <gpaste-ui-shortcuts-window.h>

struct _GPasteUiWindow
{
    AdwApplicationWindow parent_instance;

    AdwHeaderBar    *header;
    GPasteUiHistory *history;
    GPasteClient    *client;
    GPasteSettings  *settings;

    AdwNavigationSplitView *split_view;
    GtkSearchBar    *search_bar;
    GtkSearchEntry  *search_entry;
    GtkBox          *content_box;
    AdwToastOverlay *toast_overlay;
    AdwBanner       *banner;

    GtkActionBar    *merge_bar;
    GtkWidget       *merge_button;
    GtkWidget       *merge_entry; /* custom separator */

    AdwDialog       *shortcuts;
    /* Weak: the dialog belongs to whatever is presenting it, and we only keep
     * it to raise the one already up rather than stack a second. */
    AdwDialog       *preferences;

    GSignalGroup    *search_signals;
    GSignalGroup    *client_signals;

    gboolean         initialized;
};

G_PASTE_DEFINE_TYPE (UiWindow, ui_window, ADW_TYPE_APPLICATION_WINDOW)

/* The three actions the application can be asked to perform before the window
 * has finished connecting to the daemon. Each waits on an idle for @initialized,
 * so it needs the window kept alive and, for two of them, an argument to carry
 * along -- @arg is owned and freed here, and NULL for the one with none. */
typedef struct
{
    GPasteUiWindow *self;
    gchar          *arg;
    void          (*run) (GPasteUiWindow *self,
                          const gchar    *arg);
} DeferredAction;

static gboolean
run_deferred_action (gpointer user_data)
{
    DeferredAction *deferred = user_data;
    GPasteUiWindow *self = deferred->self;

    /* Keep waiting until the connection attempt has concluded, unless the window
     * was destroyed meanwhile — @banner is what dispose() clears to say so.
     * Waiting on @client instead would drop the action on the very first idle
     * turn: it is only assigned once g_paste_client_new() has come back, well
     * after this runs. */
    if (self->banner && !self->initialized)
        return G_SOURCE_CONTINUE;

    /* Concluded without a client means it failed, and the banner is saying so;
     * there is nothing left for the action to act on. */
    if (self->client)
        deferred->run (self, deferred->arg);

    g_object_unref (self);
    g_free (deferred->arg);
    g_free (deferred);

    return G_SOURCE_REMOVE;
}

static void
run_when_initialized (GPasteUiWindow *self,
                      void          (*run) (GPasteUiWindow *self, const gchar *arg),
                      const gchar    *arg,
                      const gchar    *source_name)
{
    DeferredAction *deferred = g_new (DeferredAction, 1);

    deferred->self = g_object_ref (self);
    deferred->arg = g_strdup (arg);
    deferred->run = run;

    g_source_set_name_by_id (g_idle_add (run_deferred_action, deferred), source_name);
}

/**
 * g_paste_ui_window_toast:
 * @self: the #GPasteUiWindow
 * @message: what to say
 *
 * Say something to the user, in the window they are looking at.
 */
void
g_paste_ui_window_toast (GPasteUiWindow *self,
                         const gchar    *message)
{
    g_return_if_fail (G_PASTE_IS_UI_WINDOW (self));

    /* Disposed mid-flight: the overlay went with the widget tree. */
    if (!self->toast_overlay)
        return;

    adw_toast_overlay_add_toast (self->toast_overlay, adw_toast_new (message));
}

/* @window is owned, so a call outliving nothing in particular still has
 * somewhere to report to; @message is a translated literal and is not. */
typedef struct
{
    GPasteUiWindow      *window;
    const gchar         *message;
    GPasteUiVoidFinish   void_finish;
    GPasteUiStringFinish string_finish;
} GPasteUiReport;

static gpointer
report_new (GtkWidget   *origin,
            const gchar *message)
{
    GtkRoot *root = gtk_widget_get_root (origin);
    GPasteUiReport *report = g_new0 (GPasteUiReport, 1);

    /* A row is only ever in a window of ours, but it is asked rather than
     * assumed: the reply still has to be finished either way. */
    if (G_PASTE_IS_UI_WINDOW (root))
        report->window = g_object_ref (G_PASTE_UI_WINDOW (root));

    report->message = message;

    return report;
}

static void
report_done (GPasteUiReport *report,
             const GError   *error)
{
    if (error)
    {
        g_warning ("%s: %s", report->message, error->message);

        if (report->window)
            g_paste_ui_window_toast (report->window, report->message);
    }

    g_clear_object (&report->window);
    g_free (report);
}

/**
 * g_paste_ui_report_void:
 * @origin: any widget inside the window to report through
 * @finish: the call's _finish ()
 * @message: what to say should the call have failed
 *
 * Returns: (transfer full): the user_data to pass alongside
 *          g_paste_ui_report_void_cb ()
 */
gpointer
g_paste_ui_report_void (GtkWidget         *origin,
                        GPasteUiVoidFinish finish,
                        const gchar       *message)
{
    GPasteUiReport *report = report_new (origin, message);

    report->void_finish = finish;

    return report;
}

/**
 * g_paste_ui_report_string:
 * @origin: any widget inside the window to report through
 * @finish: the call's _finish ()
 * @message: what to say should the call have failed
 *
 * Returns: (transfer full): the user_data to pass alongside
 *          g_paste_ui_report_string_cb ()
 */
gpointer
g_paste_ui_report_string (GtkWidget           *origin,
                          GPasteUiStringFinish finish,
                          const gchar         *message)
{
    GPasteUiReport *report = report_new (origin, message);

    report->string_finish = finish;

    return report;
}

/**
 * g_paste_ui_report_void_cb:
 * @source_object: the #GPasteClient the call was made on
 * @result: the call's result
 * @user_data: what g_paste_ui_report_void () returned
 *
 * Finish a call whose only interesting outcome is whether it failed.
 */
void
g_paste_ui_report_void_cb (GObject      *source_object,
                           GAsyncResult *result,
                           gpointer      user_data)
{
    GPasteUiReport *report = user_data;
    g_autoptr (GError) error = NULL;

    report->void_finish (G_PASTE_CLIENT (source_object), result, &error);
    report_done (report, error);
}

/**
 * g_paste_ui_report_string_cb:
 * @source_object: the #GPasteClient the call was made on
 * @result: the call's result
 * @user_data: what g_paste_ui_report_string () returned
 *
 * Finish a call answering the uuid it created, which nothing here wants: the
 * item it stands for arrives as an update of its own.
 */
void
g_paste_ui_report_string_cb (GObject      *source_object,
                             GAsyncResult *result,
                             gpointer      user_data)
{
    GPasteUiReport *report = user_data;
    g_autoptr (GError) error = NULL;
    g_autofree gchar *uuid = report->string_finish (G_PASTE_CLIENT (source_object), result, &error);

    report_done (report, error);
}

static void
do_empty_history (GPasteUiWindow *self,
                  const gchar    *history)
{
    g_paste_gtk_util_empty_history (GTK_WINDOW (self), self->client, self->settings, history);
}

/**
 * g_paste_ui_window_empty_history:
 * @self: the #GPasteUiWindow
 * @history: the history to empty
 *
 * Empty a history
 */
void
g_paste_ui_window_empty_history (GPasteUiWindow *self,
                                 const gchar    *history)
{
    g_return_if_fail (G_PASTE_IS_UI_WINDOW (self));
    g_return_if_fail (g_utf8_validate (history, -1, NULL));

    run_when_initialized (self, do_empty_history, history, "[GPaste] empty");
}

/* Connected swapped, so the button is the emitter and its state is read back
 * from it rather than carried by the signal. */
static void
on_favourites_toggled (GPasteUiWindow  *self,
                       GtkToggleButton *button)
{
    g_paste_ui_history_set_favourites (self->history, gtk_toggle_button_get_active (button));
}

static void
do_search (GPasteUiWindow *self,
           const gchar    *search)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_paste_ui_header_get_search_button (self->header)), TRUE);
    gtk_editable_set_text (GTK_EDITABLE (self->search_entry), search);
}

/**
 * g_paste_ui_window_search:
 * @self: the #GPasteUiWindow
 * @search: the text to search
 *
 * Do a search
 */
void
g_paste_ui_window_search (GPasteUiWindow *self,
                          const gchar    *search)
{
    g_return_if_fail (G_PASTE_IS_UI_WINDOW (self));
    g_return_if_fail (g_utf8_validate (search, -1, NULL));

    run_when_initialized (self, do_search, search, "[GPaste] search");
}

static void
do_show_prefs (GPasteUiWindow *self,
               const gchar    *arg G_GNUC_UNUSED)
{
    g_action_group_activate_action (G_ACTION_GROUP (self), "preferences", NULL);
}

static void
do_show_about (GPasteUiWindow *self,
               const gchar    *arg G_GNUC_UNUSED)
{
    g_action_group_activate_action (G_ACTION_GROUP (self), "about", NULL);
}

/**
 * g_paste_ui_window_show_prefs:
 * @self: the #GPasteUiWindow
 *
 * Show the prefs pane
 */
void
g_paste_ui_window_show_prefs (GPasteUiWindow *self)
{
    g_return_if_fail (G_PASTE_IS_UI_WINDOW (self));

    run_when_initialized (self, do_show_prefs, NULL, "[GPaste] show_prefs");
}

/**
 * g_paste_ui_window_show_about:
 * @self: the #GPasteUiWindow
 *
 * Show the about dialog
 */
void
g_paste_ui_window_show_about (GPasteUiWindow *self)
{
    g_return_if_fail (G_PASTE_IS_UI_WINDOW (self));

    run_when_initialized (self, do_show_about, NULL, "[GPaste] show_about");
}

static void
on_show_help_overlay (GSimpleAction *action    G_GNUC_UNUSED,
                      GVariant      *parameter G_GNUC_UNUSED,
                      gpointer       user_data)
{
    GPasteUiWindow *self = user_data;

    adw_dialog_present (self->shortcuts, GTK_WIDGET (self));
}

static void
on_new_item (GSimpleAction *action    G_GNUC_UNUSED,
             GVariant      *parameter G_GNUC_UNUSED,
             gpointer       user_data)
{
    GPasteUiWindow *self = user_data;

    g_paste_ui_new_item_show (self->client, GTK_WINDOW (self));
}

static void
on_toggle_search (GSimpleAction *action    G_GNUC_UNUSED,
                  GVariant      *parameter G_GNUC_UNUSED,
                  gpointer       user_data)
{
    GPasteUiWindow *self = user_data;
    GtkToggleButton *button = g_paste_ui_header_get_search_button (self->header);

    gtk_toggle_button_set_active (button, !gtk_toggle_button_get_active (button));
}

static void
on_preferences (GSimpleAction *action    G_GNUC_UNUSED,
                GVariant      *parameter G_GNUC_UNUSED,
                gpointer       user_data)
{
    GPasteUiWindow *self = user_data;

    /* Presenting the one already up raises it; building a second would stack
     * two dialogs saying the same thing. */
    if (!self->preferences)
    {
        self->preferences = g_paste_gtk_preferences_dialog_new (NULL);
        g_object_add_weak_pointer (G_OBJECT (self->preferences), (gpointer *) &self->preferences);
    }

    adw_dialog_present (self->preferences, GTK_WIDGET (self));
}

static void
on_reexec_confirmed (gboolean confirmed,
                     gpointer user_data)
{
    g_autoptr (GPasteUiWindow) self = user_data;

    /* The dialog holds its own ref, so the answer can come back after the window
     * was closed and dispose() dropped the client: there is nothing left to ask,
     * and the report the call would carry would never be handed over. */
    if (confirmed && self->client)
    {
        g_paste_client_reexecute (self->client,
                                  g_paste_ui_report_void_cb,
                                  g_paste_ui_report_void (GTK_WIDGET (self),
                                                          g_paste_client_reexecute_finish,
                                                          _("Could not restart the daemon")));
    }
}

static void
on_restart_daemon (GSimpleAction *action    G_GNUC_UNUSED,
                   GVariant      *parameter G_GNUC_UNUSED,
                   gpointer       user_data)
{
    GPasteUiWindow *self = user_data;

    g_paste_gtk_util_confirm_dialog (GTK_WINDOW (self),
                                     _("Restart the GPaste Daemon?"),
                                     _("The clipboard is not recorded while it restarts."),
                                     _("Restart"),
                                     ADW_RESPONSE_DEFAULT,
                                     on_reexec_confirmed,
                                     g_object_ref (self));
}

/* Asks the daemon, and sets nothing: the check follows the daemon's own
 * "tracking" signal, so a refused call leaves it where it was and the menu never
 * claims something the daemon did not do. It does say so, though. */
static void
g_paste_ui_window_set_tracking (GPasteUiWindow *self,
                                gboolean        state)
{
    /* Reached from a confirmation the window may not have outlived. */
    if (!self->client)
        return;

    g_paste_client_set_active (self->client, state,
                               g_paste_ui_report_void_cb,
                               g_paste_ui_report_void (GTK_WIDGET (self),
                                                       g_paste_client_set_active_finish,
                                                       (state) ? _("Could not start tracking the clipboard")
                                                               : _("Could not stop tracking the clipboard")));
}

static void
on_track_confirmed (gboolean confirmed,
                    gpointer user_data)
{
    g_autoptr (GPasteUiWindow) self = user_data;

    if (confirmed)
        g_paste_ui_window_set_tracking (self, FALSE);
}

static void
on_track_changes (GSimpleAction *action,
                  GVariant      *parameter G_GNUC_UNUSED,
                  gpointer       user_data)
{
    GPasteUiWindow *self = user_data;
    g_autoptr (GVariant) state = g_action_get_state (G_ACTION (action));

    if (g_variant_get_boolean (state))
    {
        g_paste_gtk_util_confirm_dialog (GTK_WINDOW (self),
                                         _("Stop Tracking the Clipboard?"),
                                         _("What you copy will not be added to the history until tracking is turned back on."),
                                         _("Stop"),
                                         ADW_RESPONSE_DEFAULT,
                                         on_track_confirmed,
                                         g_object_ref (self));
    }
    else
        g_paste_ui_window_set_tracking (self, TRUE);
}

static void
on_tracking_changed (GPasteClient *client G_GNUC_UNUSED,
                     gboolean      state,
                     gpointer      user_data)
{
    GPasteUiWindow *self = user_data;
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (self), "track-changes");

    if (action)
        g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (state));
}

static void
on_about (GSimpleAction *action    G_GNUC_UNUSED,
          GVariant      *parameter G_GNUC_UNUSED,
          gpointer       user_data)
{
    GPasteUiWindow *self = user_data;
    const gchar *authors[] = {
        "Marc-Antoine Perennou <Marc-Antoine@Perennou.com>",
        NULL
    };

    AdwAboutDialog *dialog = ADW_ABOUT_DIALOG (adw_about_dialog_new ());

    adw_about_dialog_set_application_name (dialog, PACKAGE_NAME);
    adw_about_dialog_set_version (dialog, PACKAGE_VERSION);
    adw_about_dialog_set_application_icon (dialog, G_PASTE_ICON_NAME);
    adw_about_dialog_set_license_type (dialog, GTK_LICENSE_BSD);
    adw_about_dialog_set_developers (dialog, authors);
    adw_about_dialog_set_copyright (dialog, "Copyright (c) 2010-2026, Marc-Antoine Perennou");
    adw_about_dialog_set_comments (dialog, _("Clipboard management system"));
    adw_about_dialog_set_website (dialog, "https://www.imagination-land.org/tags/GPaste.html");
    adw_about_dialog_set_issue_url (dialog, "https://github.com/Keruspe/GPaste/issues");
    adw_about_dialog_set_support_url (dialog, "https://github.com/Keruspe/GPaste/issues");
    /* Translators: put your names here, one per line, and they show up in the
     * about dialog's credits. */
    adw_about_dialog_set_translator_credits (dialog, _("translator-credits"));

    adw_dialog_present (ADW_DIALOG (dialog), GTK_WIDGET (self));
}

/* Every control in the header drives one of these, so the window owns the state
 * and the header owns only how it is shown -- which is what lets the rare,
 * app-level ones live in a menu rather than as an icon apiece. */
static void
add_window_actions (GPasteUiWindow *self)
{
    static const GActionEntry entries[] = {
        { "about",          on_about,          NULL, NULL,    NULL, { 0 } },
        { "new-item",       on_new_item,       NULL, NULL,    NULL, { 0 } },
        { "preferences",    on_preferences,    NULL, NULL,    NULL, { 0 } },
        { "restart-daemon", on_restart_daemon, NULL, NULL,    NULL, { 0 } },
        { "toggle-search",  on_toggle_search,  NULL, NULL,    NULL, { 0 } },
        { "track-changes",  on_track_changes,  NULL, "false", NULL, { 0 } },
    };

    g_action_map_add_action_entries (G_ACTION_MAP (self), entries, G_N_ELEMENTS (entries), self);

    g_autoptr (GSimpleAction) show_shortcuts = g_simple_action_new ("show-help-overlay", NULL);
    g_signal_connect_object (show_shortcuts, "activate", G_CALLBACK (on_show_help_overlay), self, 0);
    g_action_map_add_action (G_ACTION_MAP (self), G_ACTION (show_shortcuts));

    on_tracking_changed (self->client, g_paste_client_is_active (self->client), self);

    GtkApplication *app = gtk_window_get_application (GTK_WINDOW (self));

    struct { const gchar *action; const gchar *accel; } accels[] = {
        { "win.show-help-overlay", "<primary>question" },
        { "win.toggle-search",     "<primary>f" },
        { "win.new-item",          "<primary>n" },
        { "win.preferences",       "<primary>comma" },
        { "window.close",          "<primary>w" },
    };

    for (guint i = 0; i < G_N_ELEMENTS (accels); ++i)
    {
        gtk_application_set_accels_for_action (app, accels[i].action,
                                               (const char *[]) { accels[i].accel, NULL });
    }
}

static void exit_selection_mode (GPasteUiWindow *self);

/* Ctrl+0-9 activates the item displayed at that index, mirroring the GNOME
 * Shell extension (the index is shown to the left of each item). Only once
 * there is a list: a window presented for the connection-failure banner alone
 * has no history (nor merge bar) behind it. */
static gboolean
activate_index (GtkWidget *widget,
                GVariant  *args,
                gpointer   user_data G_GNUC_UNUSED)
{
    GPasteUiWindow *self = G_PASTE_UI_WINDOW (widget);

    if (!self->history)
        return FALSE;

    return g_paste_ui_history_activate_index (self->history, g_variant_get_uint32 (args));
}

static gboolean
on_escape (GtkWidget *widget,
           GVariant  *args      G_GNUC_UNUSED,
           gpointer   user_data G_GNUC_UNUSED)
{
    GPasteUiWindow *self = G_PASTE_UI_WINDOW (widget);

    /* The search bar closes itself on Escape; let it. A window presented for
     * the connection-failure banner alone has none. */
    if (self->search_bar && gtk_search_bar_get_search_mode (self->search_bar))
        return FALSE;

    if (self->merge_bar && gtk_action_bar_get_revealed (self->merge_bar))
    {
        exit_selection_mode (self);
        return TRUE;
    }

    gtk_window_close (GTK_WINDOW (self));
    return TRUE;
}

static void
add_shortcuts (GPasteUiWindow *self)
{
    GtkEventController *shortcuts = gtk_shortcut_controller_new ();

    gtk_event_controller_set_propagation_phase (shortcuts, GTK_PHASE_BUBBLE);

    for (guint index = 0; index < 10; ++index)
    {
        /* One trigger per digit covers the keypad twin too: GTK knows KP_0-9
         * are aliases of 0-9, so there is no second range to spell out. */
        GtkShortcutTrigger *trigger = gtk_shortcut_trigger_create_with_aliases (GDK_KEY_0 + index,
                                                                                GDK_CONTROL_MASK);
        GtkShortcutAction *action = gtk_callback_action_new (activate_index, NULL, NULL);
        GtkShortcut *shortcut = gtk_shortcut_new (trigger, action);

        gtk_shortcut_set_arguments (shortcut, g_variant_new_uint32 (index));
        gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (shortcuts), shortcut);
    }

    gtk_shortcut_controller_add_shortcut (GTK_SHORTCUT_CONTROLLER (shortcuts),
                                          gtk_shortcut_new (gtk_keyval_trigger_new (GDK_KEY_Escape, 0),
                                                            gtk_callback_action_new (on_escape, NULL, NULL)));

    gtk_widget_add_controller (GTK_WIDGET (self), shortcuts);
}

static void
on_search_activate (GtkSearchEntry *entry,
                    gpointer        user_data)
{
    GPasteUiWindow *self = user_data;
    const gchar *search = gtk_editable_get_text (GTK_EDITABLE (entry));

    if (self->history && search && *search)
        g_paste_ui_history_select_first (self->history);
}

static void
on_search (GtkSearchEntry *entry,
           gpointer        user_data)
{
    GPasteUiWindow *self = user_data;

    if (!self->history)
        return;

    g_paste_ui_history_search (self->history, gtk_editable_get_text (GTK_EDITABLE (entry)));
}

static void
on_banner_quit (AdwBanner *banner G_GNUC_UNUSED,
                gpointer   user_data)
{
    GPasteUiWindow *self = user_data;

    g_application_quit (G_APPLICATION (gtk_window_get_application (GTK_WINDOW (self))));
}

/* Which history is in use is a property, so the subtitle follows its notify
 * rather than a signal, and the initial value is simply read off the proxy's
 * cache -- no call, nothing in flight for dispose to race. */
static void
on_history_changed (GPasteClient *client,
                    GParamSpec   *pspec G_GNUC_UNUSED,
                    gpointer      user_data)
{
    GPasteUiWindow *self = user_data;
    g_autofree gchar *history = g_paste_client_get_history_name (client);

    /* A daemon leaving the bus empties that cache rather than changing it, and
     * the subtitle takes a string: keep the name we were showing. */
    if (!history)
        return;

    g_paste_ui_header_set_subtitle (self->header, history);

    /* Collapsed, the sidebar and the list are two pages rather than two panes:
     * having picked a history, the user wants to see it. */
    if (self->split_view && adw_navigation_split_view_get_collapsed (self->split_view))
        adw_navigation_split_view_set_show_content (self->split_view, TRUE);
}

static void
exit_selection_mode (GPasteUiWindow *self)
{
    g_paste_ui_history_set_selection_mode (self->history, FALSE);
    g_paste_ui_header_set_selection_mode (self->header, FALSE);
    gtk_action_bar_set_revealed (self->merge_bar, FALSE);

    if (self->search_bar)
        gtk_search_bar_set_key_capture_widget (self->search_bar, GTK_WIDGET (self));
}

static void
on_enter_selection_mode (GtkButton *button G_GNUC_UNUSED,
                         gpointer   user_data)
{
    GPasteUiWindow *self = user_data;

    g_paste_ui_history_set_selection_mode (self->history, TRUE);
    g_paste_ui_header_set_selection_mode (self->header, TRUE);
    g_paste_ui_header_set_selection_count (self->header, 0);
    gtk_action_bar_set_revealed (self->merge_bar, TRUE);

    /* Typing is how the search bar opens itself, and the header just took the
     * toggle that would close it again off the bar: while items are being
     * picked, keys belong to the list. */
    if (self->search_bar)
        gtk_search_bar_set_key_capture_widget (self->search_bar, NULL);
}

static void
on_cancel_selection_mode (GtkButton *button G_GNUC_UNUSED,
                          gpointer   user_data)
{
    exit_selection_mode (user_data);
}

static void
on_selection_changed (GPasteUiHistory *history G_GNUC_UNUSED,
                      guint            count,
                      gpointer         user_data)
{
    GPasteUiWindow *self = user_data;

    g_paste_ui_header_set_selection_count (self->header, count);
    /* Merging is only meaningful with at least two items. */
    gtk_widget_set_sensitive (self->merge_button, count >= 2);
}

static void
do_merge (GPasteUiWindow *self,
          GtkWidget      *origin,
          const gchar    *separator)
{
    guint64 n = 0;
    g_auto (GStrv) uuids = g_paste_ui_history_get_selected_uuids (self->history, &n);

    gtk_popover_popdown (GTK_POPOVER (gtk_widget_get_ancestor (origin, GTK_TYPE_POPOVER)));

    /* uuids are in selection order, so the merge keeps it. */
    if (n >= 2)
    {
        g_paste_client_merge (self->client, "", separator, (const gchar * const *) uuids,
                              g_paste_ui_report_string_cb,
                              g_paste_ui_report_string (GTK_WIDGET (self), g_paste_client_merge_finish,
                                                        _("Could not merge the selected items")));
    }

    exit_selection_mode (self);
}

static void
on_separator_chosen (GtkButton *button,
                     gpointer   user_data)
{
    do_merge (user_data, GTK_WIDGET (button), g_object_get_data (G_OBJECT (button), "separator"));
}

static void
on_custom_separator (GtkWidget *widget,
                     gpointer   user_data)
{
    GPasteUiWindow *self = user_data;

    do_merge (self, widget, gtk_editable_get_text (GTK_EDITABLE (self->merge_entry)));
}

static void
add_separator_choice (GtkBox         *box,
                      GPasteUiWindow *self,
                      const gchar    *label,
                      const gchar    *separator)
{
    GtkWidget *button = gtk_button_new_with_label (label);

    gtk_button_set_has_frame (GTK_BUTTON (button), FALSE);
    g_object_set_data_full (G_OBJECT (button), "separator", g_strdup (separator), g_free);
    g_signal_connect (button, "clicked", G_CALLBACK (on_separator_chosen), self);
    gtk_box_append (box, button);
}

static GtkWidget *
build_merge_bar (GPasteUiWindow *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top (box, 6);
    gtk_widget_set_margin_bottom (box, 6);
    gtk_widget_set_margin_start (box, 6);
    gtk_widget_set_margin_end (box, 6);

    /* translators: separators inserted between the merged items */
    add_separator_choice (GTK_BOX (box), self, _("New line"), "\n");
    add_separator_choice (GTK_BOX (box), self, _("Space"), " ");
    add_separator_choice (GTK_BOX (box), self, _("Comma"), ", ");
    add_separator_choice (GTK_BOX (box), self, _("Tab"), "\t");

    gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

    GtkWidget *custom = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top (custom, 6);

    GtkWidget *entry = self->merge_entry = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (entry), _("Custom separator"));
    gtk_widget_set_hexpand (entry, TRUE);
    g_signal_connect (entry, "activate", G_CALLBACK (on_custom_separator), self);

    GtkWidget *custom_merge = gtk_button_new_from_icon_name ("object-select-symbolic");
    gtk_widget_set_tooltip_text (custom_merge, _("Merge with this separator"));
    g_signal_connect (custom_merge, "clicked", G_CALLBACK (on_custom_separator), self);

    gtk_box_append (GTK_BOX (custom), entry);
    gtk_box_append (GTK_BOX (custom), custom_merge);
    gtk_box_append (GTK_BOX (box), custom);

    GtkWidget *popover = gtk_popover_new ();
    gtk_popover_set_child (GTK_POPOVER (popover), box);

    GtkWidget *merge_button = self->merge_button = gtk_menu_button_new ();
    gtk_menu_button_set_label (GTK_MENU_BUTTON (merge_button), _("Merge"));
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (merge_button), popover);
    gtk_widget_add_css_class (merge_button, "suggested-action");
    gtk_widget_set_sensitive (merge_button, FALSE);

    GtkWidget *bar = gtk_action_bar_new ();
    self->merge_bar = GTK_ACTION_BAR (bar);
    gtk_action_bar_set_revealed (self->merge_bar, FALSE);
    gtk_action_bar_pack_end (self->merge_bar, merge_button);

    return bar;
}

static void
g_paste_ui_window_dispose (GObject *object)
{
    GPasteUiWindow *self = G_PASTE_UI_WINDOW (object);

    g_clear_object (&self->search_signals);
    g_clear_object (&self->client_signals);
    g_clear_object (&self->client);
    g_clear_object (&self->settings);
    g_clear_object (&self->shortcuts);
    /* Registered on the dialog, pointing at a field of ours: left in place it
     * would have GObject write into freed memory should the dialog outlive us. */
    g_clear_weak_pointer (&self->preferences);

    /* Chaining up unparents (and frees) every widget below, so drop the one
     * anything still in flight tests to know the window is gone. */
    self->banner = NULL;
    self->split_view = NULL;
    self->toast_overlay = NULL;

    G_OBJECT_CLASS (g_paste_ui_window_parent_class)->dispose (object);
}

static void
g_paste_ui_window_class_init (GPasteUiWindowClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_window_dispose;
}

static void
g_paste_ui_window_init (GPasteUiWindow *self)
{
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    self->settings = g_paste_settings_new ();
    self->content_box = GTK_BOX (vbox);

    gtk_widget_set_hexpand (vbox, TRUE);
    gtk_widget_set_vexpand (vbox, TRUE);
    gtk_widget_set_halign (vbox, GTK_ALIGN_FILL);
    gtk_widget_set_valign (vbox, GTK_ALIGN_FILL);

    GtkWidget *banner = adw_banner_new ("");
    self->banner = ADW_BANNER (banner);
    adw_banner_set_button_label (self->banner, _("Quit"));
    g_signal_connect_object (banner, "button-clicked", G_CALLBACK (on_banner_quit), self, 0);
    gtk_box_append (GTK_BOX (vbox), banner);

    /* The banner is the only thing above the split view, and the header bars,
     * the search bar and the merge bar all belong to one page of it or the
     * other -- which is what gives a collapsed window a header of its own to
     * carry the back button. So there is no window-level toolbar view. */
    GtkWidget *toast_overlay = adw_toast_overlay_new ();
    self->toast_overlay = ADW_TOAST_OVERLAY (toast_overlay);
    adw_toast_overlay_set_child (ADW_TOAST_OVERLAY (toast_overlay), vbox);

    adw_application_window_set_content (ADW_APPLICATION_WINDOW (self), toast_overlay);

    self->client_signals = g_signal_group_new (G_PASTE_TYPE_CLIENT);
    g_signal_group_connect (self->client_signals, "notify::history", G_CALLBACK (on_history_changed), self);
    g_signal_group_connect (self->client_signals, "tracking", G_CALLBACK (on_tracking_changed), self);

    add_shortcuts (self);

    gtk_window_set_default_size (GTK_WINDOW (self), 800, 600);
    /* The narrowest the HIG asks an application window to survive. */
    gtk_widget_set_size_request (GTK_WIDGET (self), 360, 294);
}

static void
on_client_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    /* The window is only presented at the end of this function, so nothing else
     * would keep it alive if the application quit while we were connecting. */
    g_autoptr (GPasteUiWindow) self = user_data;
    GtkWindow *win = GTK_WINDOW (user_data);
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_finish (res, &error);

    /* The window was destroyed (the application quit) while we were connecting:
     * our ref keeps it allocated, but its widgets are gone. Nothing to set up. */
    if (!self->banner)
        return;

    if (error)
    {
        self->initialized = TRUE;
        g_critical ("%s: %s", _("Couldn't connect to GPaste daemon"), error->message);
        adw_banner_set_title (self->banner, _("Couldn't connect to GPaste daemon"));
        adw_banner_set_revealed (self->banner, TRUE);
        /* Present anyway: the banner explaining what went wrong is the whole
         * point, and a window that is never shown leaves the application
         * running with nothing on screen at all. */
        gtk_window_present (win);
        return;
    }

    GPasteSettings *settings = self->settings;
    GtkWidget *header = g_paste_ui_header_new ();

    /* Built here rather than in init(): there is nothing to search through
     * until the daemon has answered, and a window presented for the
     * connection-failure banner alone would only be offering a search bar that
     * could ignore whatever was typed into it. */
    GtkWidget *search_bar = gtk_search_bar_new ();
    GtkWidget *search_entry = gtk_search_entry_new ();

    self->search_bar = GTK_SEARCH_BAR (search_bar);
    self->search_entry = GTK_SEARCH_ENTRY (search_entry);
    gtk_search_bar_set_child (self->search_bar, search_entry);
    gtk_search_bar_connect_entry (self->search_bar, GTK_EDITABLE (search_entry));
    gtk_search_bar_set_key_capture_widget (self->search_bar, GTK_WIDGET (self));

    GSignalGroup *search_signals = self->search_signals = g_signal_group_new (GTK_TYPE_SEARCH_ENTRY);
    g_signal_group_connect (search_signals, "activate", G_CALLBACK (on_search_activate), self);
    g_signal_group_connect (search_signals, "search-changed", G_CALLBACK (on_search), self);
    g_signal_group_set_target (search_signals, self->search_entry);

    GtkWidget *panel = g_paste_ui_panel_new (client, settings, win, self->search_entry);
    GtkWidget *history = g_paste_ui_history_new (client, settings, G_PASTE_UI_PANEL (panel), win);

    self->header = ADW_HEADER_BAR (header);
    self->history = G_PASTE_UI_HISTORY (history);
    self->client = g_object_ref (client);

    self->shortcuts = g_object_ref_sink (ADW_DIALOG (g_paste_ui_shortcuts_window_new (settings)));

    add_window_actions (self);

    g_signal_connect_object (g_paste_ui_header_get_merge_button (self->header), "clicked",
                             G_CALLBACK (on_enter_selection_mode), user_data, 0);
    g_signal_connect_object (g_paste_ui_header_get_cancel_button (self->header), "clicked",
                             G_CALLBACK (on_cancel_selection_mode), user_data, 0);
    g_signal_connect_object (self->history, "selection-changed",
                             G_CALLBACK (on_selection_changed), user_data, 0);

    /* A header bar apiece, which is what the pages need to stand on their own
     * once the split view collapses them into a navigation stack: the sidebar's
     * is bare, so it shows its page's title, and the content's carries ours. */
    GtkWidget *sidebar_view = adw_toolbar_view_new ();
    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (sidebar_view), adw_header_bar_new ());
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (sidebar_view), panel);

    GtkWidget *content_view = adw_toolbar_view_new ();
    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (content_view), header);
    adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (content_view), search_bar);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (content_view), history);
    adw_toolbar_view_add_bottom_bar (ADW_TOOLBAR_VIEW (content_view), build_merge_bar (user_data));

    AdwNavigationPage *sidebar_page = adw_navigation_page_new (sidebar_view, _("Histories"));
    AdwNavigationPage *content_page = adw_navigation_page_new (content_view, _("History"));

    GtkWidget *nav_split_view = adw_navigation_split_view_new ();
    self->split_view = ADW_NAVIGATION_SPLIT_VIEW (nav_split_view);
    adw_navigation_split_view_set_sidebar (self->split_view, sidebar_page);
    adw_navigation_split_view_set_content (self->split_view, content_page);
    adw_navigation_split_view_set_min_sidebar_width (self->split_view, 240);

    /* Below this, the two panes become two pages the user navigates between,
     * so the window still works at the 360px the HIG asks for. */
    AdwBreakpoint *breakpoint = adw_breakpoint_new (adw_breakpoint_condition_parse ("max-width: 600px"));
    adw_breakpoint_add_setters (breakpoint, G_OBJECT (nav_split_view), "collapsed", TRUE, NULL);
    adw_application_window_add_breakpoint (ADW_APPLICATION_WINDOW (self), breakpoint);

    gtk_widget_set_hexpand (nav_split_view, TRUE);
    gtk_widget_set_vexpand (nav_split_view, TRUE);
    gtk_widget_set_halign (nav_split_view, GTK_ALIGN_FILL);
    gtk_widget_set_valign (nav_split_view, GTK_ALIGN_FILL);

    gtk_box_append (self->content_box, nav_split_view);

    g_object_bind_property (g_paste_ui_header_get_search_button (self->header), "active",
                            self->search_bar, "search-mode-enabled",
                            G_BINDING_BIDIRECTIONAL);

    g_signal_connect_swapped (g_paste_ui_header_get_favourites_button (self->header), "toggled",
                              G_CALLBACK (on_favourites_toggled), self);

    g_signal_group_set_target (self->client_signals, self->client);

    on_history_changed (self->client, NULL, self);

    self->initialized = TRUE;
    gtk_window_present (win);
}

/**
 * g_paste_ui_window_new:
 * @app: the #GtkApplication
 *
 * Create a new instance of #GPasteUiWindow
 *
 * Returns: a newly allocated #GPasteUiWindow
 *          free it with g_object_unref
 */
GtkWidget *
g_paste_ui_window_new (GtkApplication *app)
{
    g_return_val_if_fail (GTK_IS_APPLICATION (app), NULL);

    GtkWidget *self = g_object_new (G_PASTE_TYPE_UI_WINDOW,
                                      "application", app,
                                      "resizable",   TRUE,
                                      "title",       PACKAGE_STRING,
                                      "icon-name",   G_PASTE_ICON_NAME,
                                      NULL);

    /* The callback owns this ref (see on_client_ready). */
    g_paste_client_new (on_client_ready, g_object_ref (self));

    return self;
}
