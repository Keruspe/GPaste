/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gpaste-ui-header.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-panel.h>
#include <gpaste-ui-search-bar.h>
#include <gpaste-ui-window.h>

struct _GPasteUiWindow
{
    GtkApplicationWindow parent_instance;
};

typedef struct
{
    GPasteUiHeader  *header;
    GPasteUiHistory *history;

    GtkSearchEntry  *search_entry;

    gboolean         initialized;

    gulong           key_press_signal;
    gulong           search_signal;
} GPasteUiWindowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiWindow, g_paste_ui_window, GTK_TYPE_WINDOW)

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
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_window_search (const GPasteUiWindow *self,
                          const gchar          *search)
{
    g_return_if_fail (G_PASTE_IS_UI_WINDOW (self));
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
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_window_show_prefs (const GPasteUiWindow *self)
{
    g_return_if_fail (G_PASTE_IS_UI_WINDOW (self));

    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);
    
    g_source_set_name_by_id (g_idle_add (_show_prefs, priv), "[GPaste] show_prefs");
}

static gboolean
on_key_press_event (GtkWidget *widget,
                    GdkEvent  *event,
                    gpointer   user_data)
{
    GtkSearchBar *bar = user_data;
    GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (widget));

    if (GTK_IS_ENTRY (focus) && !GTK_IS_SEARCH_ENTRY (focus))
        return FALSE;

    return gtk_search_bar_handle_event (bar, event);
}

static void
on_search (GtkSearchEntry *entry,
           gpointer        user_data)
{
    GPasteUiWindowPrivate *priv = user_data;

    g_paste_ui_history_search (priv->history, gtk_entry_get_text (GTK_ENTRY (entry)));
}

static void
g_paste_ui_window_dispose (GObject *object)
{
    GPasteUiWindow *self = G_PASTE_UI_WINDOW (object);
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (self);

    if (priv->key_press_signal)
    {
        GtkContainer *box = GTK_CONTAINER (gtk_bin_get_child (GTK_BIN (self)));
        GPasteUiSearchBar *search_bar = G_PASTE_UI_SEARCH_BAR (gtk_container_get_children (box)->data);
        GtkSearchEntry *entry = g_paste_ui_search_bar_get_entry (search_bar);

        g_signal_handler_disconnect (self, priv->key_press_signal);
        g_signal_handler_disconnect (entry, priv->search_signal);
        priv->key_press_signal = 0;
    }

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
    GtkWindow *win = GTK_WINDOW (self);
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

    gtk_widget_set_margin_start (vbox, 5);
    gtk_widget_set_margin_end (vbox, 5);
    gtk_widget_set_margin_bottom (vbox, 5);

    GtkWidget *search_bar = g_paste_ui_search_bar_new ();
    GtkContainer *box = GTK_CONTAINER (vbox);

    gtk_container_add (GTK_CONTAINER (win), vbox);
    gtk_box_pack_start (GTK_BOX (box), search_bar, FALSE, FALSE, 0);

    GtkSearchEntry *entry = priv->search_entry = g_paste_ui_search_bar_get_entry (G_PASTE_UI_SEARCH_BAR (search_bar));
    priv->key_press_signal = g_signal_connect (self,
                                               "key-press-event",
                                               G_CALLBACK (on_key_press_event),
                                               search_bar);
    priv->search_signal = g_signal_connect (entry,
                                            "search-changed",
                                            G_CALLBACK (on_search),
                                            priv);

    gtk_window_set_focus (win, GTK_WIDGET (entry));
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
    GtkWidget *history = g_paste_ui_history_new (client, settings);
    GPasteUiHeader *h = priv->header = G_PASTE_UI_HEADER (header);

    priv->history = G_PASTE_UI_HISTORY (history);

    gtk_window_set_titlebar (win, header);

    GtkContainer *vbox = GTK_CONTAINER (gtk_bin_get_child (GTK_BIN (win)));
    GtkWidget *hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    GtkBox *box = GTK_BOX (hbox);

    gtk_box_pack_start (box, panel, FALSE, FALSE, 0);
    gtk_box_pack_start (box, gtk_separator_new (GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 2);
    gtk_box_pack_start (box, history, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

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
                                      "width-request",   800,
                                      "height-request",  600,
                                      NULL);

    g_paste_client_new (on_client_ready, self);

    return self;
}
