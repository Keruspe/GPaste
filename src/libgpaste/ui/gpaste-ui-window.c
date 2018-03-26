/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-header.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-search-bar.h>
#include <gpaste-ui-window.h>
#include <gpaste-ui-shortcuts-window.h>
#include <gpaste-util.h>

#include "gpaste-gtk-compat.h"

struct _GPasteUiWindow
{
    GtkApplicationWindow parent_instance;
};

enum
{
    C_SEARCH,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteUiHeader  *header;
    GPasteUiHistory *history;
    GPasteClient    *client;
    GPasteSettings  *settings;

    GtkSearchBar    *search_bar;
    GtkSearchEntry  *search_entry;

    gboolean         initialized;

    guint64          c_signals[C_LAST_SIGNAL];
} GPasteUiWindowPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (UiWindow, ui_window, GTK_TYPE_APPLICATION_WINDOW)

static gboolean
_empty (gpointer user_data)
{
    gpointer *data = (gpointer *) user_data;
    GPasteUiWindow *self = data[0];
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    if (!priv->initialized)
        return G_SOURCE_CONTINUE;

    g_autofree gchar *history = data[1];
    g_free (data);

    g_paste_util_empty_history (GTK_WINDOW (self), priv->client, priv->settings, history);

    return G_SOURCE_REMOVE;
}

/**
 * g_paste_ui_window_empty_history:
 * @self: the #GPasteUiWindow
 * @history: the history to empty
 *
 * Empty an history
 */
G_PASTE_VISIBLE void
g_paste_ui_window_empty_history (GPasteUiWindow *self,
                                 const gchar    *history)
{
    g_return_if_fail (_G_PASTE_IS_UI_WINDOW (self));
    g_return_if_fail (g_utf8_validate (history, -1, NULL));

    gpointer *data = g_new (gpointer, 2);
    data[0] = self;
    data[1] = g_strdup (history);

    g_source_set_name_by_id (g_idle_add (_empty, data), "[GPaste] empty");
}

static gboolean
_search (gpointer user_data)
{
    gpointer *data = (gpointer *) user_data;
    GPasteUiWindowPrivate *priv = data[0];

    if (!priv->initialized)
        return G_SOURCE_CONTINUE;

    g_autofree gchar *search = data[1];
    g_free (data);

    gtk_button_clicked (g_paste_ui_header_get_search_button (priv->header));
    gtk_entry_set_text (GTK_ENTRY (priv->search_entry), search);

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

    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    gpointer *data = g_new (gpointer, 2);
    data[0] = priv;
    data[1] = g_strdup (search);

    g_source_set_name_by_id (g_idle_add (_search, data), "[GPaste] search");
}

static gboolean
_show_prefs (gpointer user_data)
{
    GPasteUiWindowPrivate *priv = user_data;

    if (!priv->initialized)
        return G_SOURCE_CONTINUE;

    g_paste_ui_header_show_prefs (priv->header);

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

    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    g_source_set_name_by_id (g_idle_add (_show_prefs, priv), "[GPaste] show_prefs");
}

static gboolean
on_key_press_event (GtkWidget   *widget,
                    GdkEventKey *event)
{
    const GPasteUiWindowPrivate *priv = _g_paste_ui_window_get_instance_private (G_PASTE_UI_WINDOW (widget));
    GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (widget));
    GdkEvent *_event = (GdkEvent *) event;
    gboolean search_has_focus = focus == GTK_WIDGET (priv->search_entry);
    gboolean search_in_progress = search_has_focus && gtk_entry_get_text_length (GTK_ENTRY (priv->search_entry));
    gboolean forward_to_search = FALSE;
    guint keyval;

    if (gdk_event_get_keyval (_event, &keyval))
    {
        switch (keyval)
        {
        case GDK_KEY_Escape:
            if (!search_in_progress)
            {
                gtk_window_close (GTK_WINDOW (widget));
                return GDK_EVENT_STOP;
            }
            else
            {
                forward_to_search = TRUE;
            }
            break;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
        case GDK_KEY_ISO_Enter:
            if (search_in_progress && g_paste_ui_history_select_first (priv->history))
                return GDK_EVENT_STOP;
            break;
        default:
            forward_to_search = TRUE;
            break;
        }
    }

    if (forward_to_search && gtk_search_bar_handle_event (priv->search_bar, _event))
        return GDK_EVENT_STOP;

    gboolean res = GTK_WIDGET_CLASS (g_paste_ui_window_parent_class)->key_press_event (widget, event);

    if (res == GDK_EVENT_STOP || !forward_to_search || search_has_focus)
        return res;

    // fallback to explicitely focusing search to see if key can be handled
    gtk_entry_grab_focus_without_selecting (GTK_ENTRY (priv->search_entry));

    if (gtk_search_bar_handle_event (priv->search_bar, _event))
        return GDK_EVENT_STOP;

    if (GTK_WIDGET_CLASS (g_paste_ui_window_parent_class)->key_press_event (widget, event) == GDK_EVENT_STOP)
        return GDK_EVENT_STOP;

    gtk_widget_grab_focus (focus);

    return res;
}

static void
on_search (GtkSearchEntry *entry,
           gpointer        user_data)
{
    GPasteUiWindowPrivate *priv = user_data;

    g_paste_ui_history_search (priv->history, gtk_entry_get_text (GTK_ENTRY (entry)));
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
g_paste_ui_window_dispose (GObject *object)
{
    GPasteUiWindow *self = G_PASTE_UI_WINDOW (object);
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    if (priv->c_signals[C_SEARCH])
    {
        GPasteUiSearchBar *search_bar = G_PASTE_UI_SEARCH_BAR (priv->search_bar);
        GtkSearchEntry *entry = g_paste_ui_search_bar_get_entry (search_bar);

        g_signal_handler_disconnect (entry, priv->c_signals[C_SEARCH]);
        priv->c_signals[C_SEARCH] = 0;
    }

    g_clear_object (&priv->client);

    G_OBJECT_CLASS (g_paste_ui_window_parent_class)->dispose (object);
}

static void
g_paste_ui_window_class_init (GPasteUiWindowClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_ui_window_dispose;
    GTK_WIDGET_CLASS (klass)->key_press_event = on_key_press_event;
}

static void
g_paste_ui_window_init (GPasteUiWindow *self)
{
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);
    GtkWindow *win = GTK_WINDOW (self);
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    gtk_widget_set_margin_start (vbox, 5);
    gtk_widget_set_margin_end (vbox, 5);
    gtk_widget_set_margin_bottom (vbox, 5);

    GtkWidget *search_bar = g_paste_ui_search_bar_new ();
    GtkContainer *box = GTK_CONTAINER (vbox);

    priv->search_bar = GTK_SEARCH_BAR (search_bar);

    gtk_container_add (GTK_CONTAINER (win), vbox);
    gtk_box_pack_start (GTK_BOX (box), search_bar, FALSE, FALSE);

    GtkSearchEntry *entry = priv->search_entry = g_paste_ui_search_bar_get_entry (G_PASTE_UI_SEARCH_BAR (search_bar));
    priv->c_signals[C_SEARCH] = g_signal_connect (entry,
                                                  "search-changed",
                                                  G_CALLBACK (on_search),
                                                  priv);

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
        gtk_window_close (win); /* will exit the application */
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GtkWidget *header = g_paste_ui_header_new (win, client);
    GtkWidget *panel = g_paste_ui_panel_new (client, settings, win, priv->search_entry);
    GtkWidget *history = g_paste_ui_history_new (client, settings, G_PASTE_UI_PANEL (panel), win);
    GPasteUiHeader *h = priv->header = G_PASTE_UI_HEADER (header);

    priv->history = G_PASTE_UI_HISTORY (history);
    priv->client = g_object_ref (client);
    priv->settings = g_paste_settings_new();

    gtk_window_set_titlebar (win, header);
    gtk_application_window_set_help_overlay (GTK_APPLICATION_WINDOW (user_data), GTK_SHORTCUTS_WINDOW (g_paste_ui_shortcuts_window_new (settings)));

    GtkContainer *vbox = GTK_CONTAINER (gtk_bin_get_child (GTK_BIN (win)));
    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    GtkBox *box = GTK_BOX (hbox);

    gtk_box_set_spacing (box, 2);
    gtk_box_pack_start (box, panel, FALSE, FALSE);
    gtk_box_pack_start (box, gtk_separator_new (GTK_ORIENTATION_VERTICAL), FALSE, FALSE);
    gtk_widget_set_hexpand (history, TRUE);
    gtk_widget_set_halign (history, TRUE);
    gtk_box_pack_start (box, history, TRUE, TRUE);
    gtk_widget_set_vexpand (hbox, TRUE);
    gtk_widget_set_valign (hbox, TRUE);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE);

    g_object_bind_property (g_paste_ui_header_get_search_button (h), "active",
                            gtk_container_get_children (vbox)->data, "search-mode-enabled",
                            G_BINDING_BIDIRECTIONAL);

    gtk_widget_show_all (GTK_WIDGET (win));
    priv->initialized = TRUE;
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

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_WINDOW,
                                      "application",     app,
                                      "type",            GTK_WINDOW_TOPLEVEL,
                                      "window-position", GTK_WIN_POS_CENTER_ALWAYS,
                                      "resizable",       FALSE,
                                      "icon-name",       G_PASTE_ICON_NAME,
                                      NULL);

    g_paste_client_new (on_client_ready, self);

    return self;
}
