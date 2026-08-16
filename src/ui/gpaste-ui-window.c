// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-header.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-window.h>
#include <gpaste-ui-shortcuts-window.h>

struct _GPasteUiWindow
{
    AdwApplicationWindow parent_instance;

    AdwHeaderBar    *header;
    GPasteUiHistory *history;
    GPasteClient    *client;
    GPasteSettings  *settings;

    AdwToolbarView  *toolbar_view;
    GtkSearchBar    *search_bar;
    GtkSearchEntry  *search_entry;
    GtkBox          *content_box;
    AdwToastOverlay *toast_overlay;
    AdwBanner       *banner;

    GtkActionBar    *merge_bar;
    GtkWidget       *merge_button;
    GtkWidget       *merge_entry; /* custom separator */

    AdwDialog       *shortcuts;

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
    g_paste_ui_header_show_prefs (self->header);
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

static void
on_show_help_overlay (GSimpleAction *action    G_GNUC_UNUSED,
                      GVariant      *parameter G_GNUC_UNUSED,
                      gpointer       user_data)
{
    GPasteUiWindow *self = user_data;

    adw_dialog_present (self->shortcuts, GTK_WIDGET (self));
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

    /* The search bar closes itself on Escape; let it. */
    if (gtk_search_bar_get_search_mode (self->search_bar))
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

    /* These two are wired up from init(), before there is anything to search:
     * the window can be on screen with only the connection-failure banner. */
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
}

static void
exit_selection_mode (GPasteUiWindow *self)
{
    g_paste_ui_history_set_selection_mode (self->history, FALSE);
    g_paste_ui_header_set_selection_mode (self->header, FALSE);
    gtk_action_bar_set_revealed (self->merge_bar, FALSE);
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
        g_paste_client_merge (self->client, "", separator, (const gchar * const *) uuids, NULL, NULL);

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
    add_separator_choice (GTK_BOX (box), self, _("Tabulation"), "\t");

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

    /* Chaining up unparents (and frees) every widget below, so drop the one
     * anything still in flight tests to know the window is gone. */
    self->banner = NULL;

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

    GtkWidget *search_bar = gtk_search_bar_new ();
    GtkWidget *search_entry = gtk_search_entry_new ();

    self->search_bar = GTK_SEARCH_BAR (search_bar);
    self->search_entry = GTK_SEARCH_ENTRY (search_entry);
    gtk_search_bar_set_child (self->search_bar, search_entry);
    gtk_search_bar_connect_entry (self->search_bar, GTK_EDITABLE (search_entry));
    gtk_box_append (GTK_BOX (vbox), search_bar);

    gtk_search_bar_set_key_capture_widget (self->search_bar, GTK_WIDGET (self));

    GtkWidget *toolbar_view = adw_toolbar_view_new ();
    self->toolbar_view = ADW_TOOLBAR_VIEW (toolbar_view);

    GtkWidget *toast_overlay = adw_toast_overlay_new ();
    self->toast_overlay = ADW_TOAST_OVERLAY (toast_overlay);
    adw_toast_overlay_set_child (ADW_TOAST_OVERLAY (toast_overlay), vbox);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar_view), toast_overlay);

    adw_application_window_set_content (ADW_APPLICATION_WINDOW (self), toolbar_view);

    GtkSearchEntry *entry = self->search_entry;

    GSignalGroup *search_signals = self->search_signals = g_signal_group_new (GTK_TYPE_SEARCH_ENTRY);
    g_signal_group_connect (search_signals, "activate", G_CALLBACK (on_search_activate), self);
    g_signal_group_connect (search_signals, "search-changed", G_CALLBACK (on_search), self);
    g_signal_group_set_target (search_signals, entry);

    self->client_signals = g_signal_group_new (G_PASTE_TYPE_CLIENT);
    g_signal_group_connect (self->client_signals, "notify::history", G_CALLBACK (on_history_changed), self);

    add_shortcuts (self);

    gtk_window_set_default_size (GTK_WINDOW (self), 800, 600);
    gtk_widget_set_size_request (GTK_WIDGET (self), 400, 300);
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
        /* Nothing was built to search through, so stop a keystroke from pulling
         * up a search bar that could only ignore it. */
        gtk_search_bar_set_key_capture_widget (self->search_bar, NULL);
        /* Present anyway: the banner explaining what went wrong is the whole
         * point, and a window that is never shown leaves the application
         * running with nothing on screen at all. */
        gtk_window_present (win);
        return;
    }

    GPasteSettings *settings = self->settings;
    GtkWidget *header = g_paste_ui_header_new (win, client);
    GtkWidget *panel = g_paste_ui_panel_new (client, settings, win, self->search_entry);
    GtkWidget *history = g_paste_ui_history_new (client, settings, G_PASTE_UI_PANEL (panel), win);

    self->header = ADW_HEADER_BAR (header);
    self->history = G_PASTE_UI_HISTORY (history);
    self->client = g_object_ref (client);

    self->shortcuts = g_object_ref_sink (ADW_DIALOG (g_paste_ui_shortcuts_window_new (settings)));

    g_autoptr (GSimpleAction) show_shortcuts = g_simple_action_new ("show-help-overlay", NULL);
    g_signal_connect_object (show_shortcuts, "activate", G_CALLBACK (on_show_help_overlay), user_data, 0);
    g_action_map_add_action (G_ACTION_MAP (user_data), G_ACTION (show_shortcuts));

    gtk_application_set_accels_for_action (GTK_APPLICATION (gtk_window_get_application (win)),
                                           "win.show-help-overlay",
                                           (const char *[]) { "<primary>question", NULL });

    adw_toolbar_view_add_top_bar (self->toolbar_view, header);
    adw_toolbar_view_add_bottom_bar (self->toolbar_view, build_merge_bar (user_data));

    g_signal_connect_object (g_paste_ui_header_get_merge_button (self->header), "clicked",
                             G_CALLBACK (on_enter_selection_mode), user_data, 0);
    g_signal_connect_object (g_paste_ui_header_get_cancel_button (self->header), "clicked",
                             G_CALLBACK (on_cancel_selection_mode), user_data, 0);
    g_signal_connect_object (self->history, "selection-changed",
                             G_CALLBACK (on_selection_changed), user_data, 0);

    AdwNavigationPage *sidebar_page = adw_navigation_page_new (panel, _("Histories"));
    AdwNavigationPage *content_page = adw_navigation_page_new (history, _("History"));

    GtkWidget *nav_split_view = adw_navigation_split_view_new ();
    adw_navigation_split_view_set_sidebar (ADW_NAVIGATION_SPLIT_VIEW (nav_split_view), sidebar_page);
    adw_navigation_split_view_set_content (ADW_NAVIGATION_SPLIT_VIEW (nav_split_view), content_page);
    adw_navigation_split_view_set_min_sidebar_width (ADW_NAVIGATION_SPLIT_VIEW (nav_split_view), 240);

    gtk_widget_set_hexpand (nav_split_view, TRUE);
    gtk_widget_set_vexpand (nav_split_view, TRUE);
    gtk_widget_set_halign (nav_split_view, GTK_ALIGN_FILL);
    gtk_widget_set_valign (nav_split_view, GTK_ALIGN_FILL);

    gtk_box_append (self->content_box, nav_split_view);

    g_object_bind_property (g_paste_ui_header_get_search_button (self->header), "active",
                            self->search_bar, "search-mode-enabled",
                            G_BINDING_BIDIRECTIONAL);

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
