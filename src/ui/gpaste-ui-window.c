/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-header.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-search-bar.h>
#include <gpaste-ui-window.h>
#include <gpaste-ui-shortcuts-window.h>

struct _GPasteUiWindow
{
    AdwApplicationWindow parent_instance;
};

typedef struct
{
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

    AdwDialog       *shortcuts;

    GSignalGroup    *search_signals;
    GSignalGroup    *client_signals;

    gboolean         initialized;
} GPasteUiWindowPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiWindow, ui_window, ADW_TYPE_APPLICATION_WINDOW)

static gboolean
_empty (gpointer user_data)
{
    gpointer *data = (gpointer *) user_data;
    GPasteUiWindow *self = data[0];

    if (!GTK_IS_WIDGET (self))
    {
        g_free (data[1]);
        g_free (data);
        return G_SOURCE_REMOVE;
    }

    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    if (!priv->initialized)
        return G_SOURCE_CONTINUE;

    if (!priv->client)
    {
        g_free (data[1]);
        g_free (data);
        g_object_unref (self);
        return G_SOURCE_REMOVE;
    }

    g_autofree gchar *history = data[1];
    g_free (data);

    g_paste_gtk_util_empty_history (GTK_WINDOW (self), priv->client, priv->settings, history);
    g_object_unref (self);

    return G_SOURCE_REMOVE;
}

/**
 * g_paste_ui_window_empty_history:
 * @self: the #GPasteUiWindow
 * @history: the history to empty
 *
 * Empty a history
 */
G_PASTE_VISIBLE void
g_paste_ui_window_empty_history (GPasteUiWindow *self,
                                 const gchar    *history)
{
    g_return_if_fail (_G_PASTE_IS_UI_WINDOW (self));
    g_return_if_fail (g_utf8_validate (history, -1, NULL));

    gpointer *data = g_new (gpointer, 2);
    data[0] = g_object_ref (self);
    data[1] = g_strdup (history);

    g_source_set_name_by_id (g_idle_add (_empty, data), "[GPaste] empty");
}

static gboolean
_search (gpointer user_data)
{
    gpointer *data = (gpointer *) user_data;
    GPasteUiWindow *self = data[0];

    if (!GTK_IS_WIDGET (self))
    {
        g_free (data[1]);
        g_free (data);
        return G_SOURCE_REMOVE;
    }

    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    if (!priv->initialized)
        return G_SOURCE_CONTINUE;

    g_autofree gchar *search = data[1];
    g_free (data);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_paste_ui_header_get_search_button (priv->header)), TRUE);
    gtk_editable_set_text (GTK_EDITABLE (priv->search_entry), search);
    g_object_unref (self);

    return G_SOURCE_REMOVE;
}

/**
 * g_paste_ui_window_search:
 * @self: the #GPasteUiWindow
 * @search: the text to search
 *
 * Do a search
 */
G_PASTE_VISIBLE void
g_paste_ui_window_search (GPasteUiWindow *self,
                          const gchar    *search)
{
    g_return_if_fail (_G_PASTE_IS_UI_WINDOW (self));
    g_return_if_fail (g_utf8_validate (search, -1, NULL));

    gpointer *data = g_new (gpointer, 2);
    data[0] = g_object_ref (self);
    data[1] = g_strdup (search);

    g_source_set_name_by_id (g_idle_add (_search, data), "[GPaste] search");
}

static gboolean
_show_prefs (gpointer user_data)
{
    GPasteUiWindow *self = user_data;

    if (!GTK_IS_WIDGET (self))
        return G_SOURCE_REMOVE;

    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    if (!priv->initialized)
        return G_SOURCE_CONTINUE;

    g_paste_ui_header_show_prefs (priv->header);
    g_object_unref (self);

    return G_SOURCE_REMOVE;
}

/**
 * g_paste_ui_window_show_prefs:
 * @self: the #GPasteUiWindow
 *
 * Show the prefs pane
 */
G_PASTE_VISIBLE void
g_paste_ui_window_show_prefs (GPasteUiWindow *self)
{
    g_return_if_fail (_G_PASTE_IS_UI_WINDOW (self));

    g_source_set_name_by_id (g_idle_add (_show_prefs, g_object_ref (self)), "[GPaste] show_prefs");
}

static void
on_show_help_overlay (GSimpleAction *action    G_GNUC_UNUSED,
                      GVariant      *parameter G_GNUC_UNUSED,
                      gpointer       user_data)
{
    GPasteUiWindow *self = user_data;
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    adw_dialog_present (priv->shortcuts, GTK_WIDGET (self));
}

static gboolean
on_key_pressed (GtkEventControllerKey *controller G_GNUC_UNUSED,
                guint                  keyval,
                guint                  keycode     G_GNUC_UNUSED,
                GdkModifierType        state       G_GNUC_UNUSED,
                gpointer               user_data)
{
    GPasteUiWindow *self = user_data;
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);
    GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (self));
    gboolean search_has_focus = focus == GTK_WIDGET (priv->search_entry);
    gboolean search_in_progress = search_has_focus && gtk_entry_get_text_length (GTK_ENTRY (priv->search_entry));
    gboolean other_entry_has_focus = focus && GTK_IS_EDITABLE (focus) && !search_has_focus;

    switch (keyval)
    {
    case GDK_KEY_Escape:
        if (!search_in_progress)
        {
            gtk_window_close (GTK_WINDOW (self));
            return GDK_EVENT_STOP;
        }
        break;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
        if (search_in_progress && g_paste_ui_history_select_first (priv->history))
            return GDK_EVENT_STOP;
        break;
    }

    return other_entry_has_focus ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
}

static void
on_search (GtkSearchEntry *entry,
           gpointer        user_data)
{
    GPasteUiWindowPrivate *priv = user_data;

    g_paste_ui_history_search (priv->history, gtk_editable_get_text (GTK_EDITABLE (entry)));
}

static gboolean
focus_search (gpointer user_data)
{
    GPasteUiWindow *self = user_data;
    const GPasteUiWindowPrivate *priv = _g_paste_ui_window_get_instance_private (self);
    GtkWindow *win = user_data;
    GtkWidget *widget = user_data;

    if (!GTK_IS_WIDGET (widget))
        return G_SOURCE_REMOVE;

    if (!gtk_widget_get_realized (widget))
        return G_SOURCE_CONTINUE;

    gtk_window_set_focus (win, GTK_WIDGET (priv->search_entry));

    return G_SOURCE_REMOVE;
}

static void
on_banner_quit (AdwBanner *banner G_GNUC_UNUSED,
                gpointer   user_data)
{
    GPasteUiWindow *self = user_data;

    g_application_quit (G_APPLICATION (gtk_window_get_application (GTK_WINDOW (self))));
}

static void
on_switch_history (GPasteClient *client G_GNUC_UNUSED,
                   const gchar  *history,
                   gpointer      user_data)
{
    GPasteUiWindowPrivate *priv = user_data;

    g_paste_ui_header_set_subtitle (priv->header, history);
}

static void
on_initial_history_name (GObject      *source_object G_GNUC_UNUSED,
                         GAsyncResult *res,
                         gpointer      user_data)
{
    GPasteUiWindowPrivate *priv = user_data;
    g_autofree gchar *name = g_paste_client_get_history_name_finish (priv->client, res, NULL);

    if (name)
        g_paste_ui_header_set_subtitle (priv->header, name);
}

static void
g_paste_ui_window_dispose (GObject *object)
{
    GPasteUiWindow *self = G_PASTE_UI_WINDOW (object);
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    g_clear_object (&priv->search_signals);
    g_clear_object (&priv->client_signals);
    g_clear_object (&priv->client);
    g_clear_object (&priv->settings);
    g_clear_object (&priv->shortcuts);

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
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    priv->settings = g_paste_settings_new ();
    priv->content_box = GTK_BOX (vbox);

    gtk_widget_set_hexpand (vbox, TRUE);
    gtk_widget_set_vexpand (vbox, TRUE);
    gtk_widget_set_halign (vbox, GTK_ALIGN_FILL);
    gtk_widget_set_valign (vbox, GTK_ALIGN_FILL);

    GtkWidget *banner = adw_banner_new ("");
    priv->banner = ADW_BANNER (banner);
    adw_banner_set_button_label (priv->banner, _("Quit"));
    g_signal_connect (banner, "button-clicked", G_CALLBACK (on_banner_quit), self);
    gtk_box_append (GTK_BOX (vbox), banner);

    GtkWidget *search_bar = g_paste_ui_search_bar_new ();
    priv->search_bar = GTK_SEARCH_BAR (search_bar);
    gtk_box_append (GTK_BOX (vbox), search_bar);

    gtk_search_bar_set_key_capture_widget (priv->search_bar, GTK_WIDGET (self));

    GtkWidget *toolbar_view = adw_toolbar_view_new ();
    priv->toolbar_view = ADW_TOOLBAR_VIEW (toolbar_view);

    GtkWidget *toast_overlay = adw_toast_overlay_new ();
    priv->toast_overlay = ADW_TOAST_OVERLAY (toast_overlay);
    adw_toast_overlay_set_child (ADW_TOAST_OVERLAY (toast_overlay), vbox);
    adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar_view), toast_overlay);

    adw_application_window_set_content (ADW_APPLICATION_WINDOW (self), toolbar_view);

    GtkSearchEntry *entry = priv->search_entry = g_paste_ui_search_bar_get_entry (priv->search_bar);

    GSignalGroup *search_signals = priv->search_signals = g_signal_group_new (GTK_TYPE_SEARCH_ENTRY);
    g_signal_group_connect (search_signals, "search-changed", G_CALLBACK (on_search), priv);
    g_signal_group_set_target (search_signals, entry);

    priv->client_signals = g_signal_group_new (G_PASTE_TYPE_CLIENT);
    g_signal_group_connect (priv->client_signals, "switch-history", G_CALLBACK (on_switch_history), priv);

    GtkEventController *key_controller = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (on_key_pressed), self);
    gtk_widget_add_controller (GTK_WIDGET (self), key_controller);

    g_source_set_name_by_id (g_idle_add (focus_search, self), "[GPaste] focus_search");
}

static void
on_client_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (user_data);
    GtkWindow *win = GTK_WINDOW (user_data);
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_finish (res, &error);

    if (error)
    {
        priv->initialized = TRUE;
        g_critical ("%s: %s\n", _("Couldn't connect to GPaste daemon"), error->message);
        adw_banner_set_title (priv->banner, _("Couldn't connect to GPaste daemon"));
        adw_banner_set_revealed (priv->banner, TRUE);
        return;
    }

    GPasteSettings *settings = priv->settings;
    GtkWidget *header = g_paste_ui_header_new (win, client);
    GtkWidget *panel = g_paste_ui_panel_new (client, settings, win, priv->search_entry);
    GtkWidget *history = g_paste_ui_history_new (client, settings, G_PASTE_UI_PANEL (panel), win);

    priv->header = ADW_HEADER_BAR (header);
    priv->history = G_PASTE_UI_HISTORY (history);
    priv->client = g_object_ref (client);

    priv->shortcuts = g_object_ref_sink (ADW_DIALOG (g_paste_ui_shortcuts_window_new (settings)));

    g_autoptr (GSimpleAction) show_shortcuts = g_simple_action_new ("show-help-overlay", NULL);
    g_signal_connect (show_shortcuts, "activate", G_CALLBACK (on_show_help_overlay), user_data);
    g_action_map_add_action (G_ACTION_MAP (user_data), G_ACTION (show_shortcuts));

    gtk_application_set_accels_for_action (GTK_APPLICATION (gtk_window_get_application (win)),
                                           "win.show-help-overlay",
                                           (const char *[]) { "<primary>question", NULL });

    adw_toolbar_view_add_top_bar (priv->toolbar_view, header);

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

    gtk_box_append (priv->content_box, nav_split_view);

    g_object_bind_property (g_paste_ui_header_get_search_button (priv->header), "active",
                            priv->search_bar, "search-mode-enabled",
                            G_BINDING_BIDIRECTIONAL);

    g_signal_group_set_target (priv->client_signals, priv->client);

    g_paste_client_get_history_name (priv->client, on_initial_history_name, priv);

    priv->initialized = TRUE;
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
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_window_new (GtkApplication *app)
{
    g_return_val_if_fail (GTK_IS_APPLICATION (app), NULL);

    GtkWidget *self = g_object_new (G_PASTE_TYPE_UI_WINDOW,
                                      "application", app,
                                      "resizable",   TRUE,
                                      "title",       PACKAGE_STRING,
                                      "icon-name",   G_PASTE_ICON_NAME,
                                      NULL);

    gtk_window_set_default_size (GTK_WINDOW (self), 800, 600);
    gtk_widget_set_size_request (self, 400, 300);

    g_paste_client_new (on_client_ready, self);

    return self;
}
