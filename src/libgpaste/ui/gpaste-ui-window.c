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
#include <gpaste-ui-search-bar.h>
#include <gpaste-ui-window.h>

struct _GPasteUiWindow
{
    GtkApplicationWindow parent_instance;
};

typedef struct
{
    GPasteUiHeader *header;

    gboolean        initialized;
} GPasteUiWindowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiWindow, g_paste_ui_window, GTK_TYPE_WINDOW)

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

static void
g_paste_ui_window_class_init (GPasteUiWindowClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_window_init (GPasteUiWindow *self G_GNUC_UNUSED)
{
}

static void
on_client_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    GPasteUiWindowPrivate *priv = g_paste_ui_window_get_instance_private (user_data);
    GtkWindow *win = user_data;
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 10);
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClient) client = g_paste_client_new_finish (res, &error);

    if (error)
    {
        priv->initialized = TRUE;
        g_critical ("%s: %s\n", _("Couldn't connect to GPaste daemon"), error->message);
        gtk_window_close (win); /* will exit the application */
    }

    GtkWidget *header = g_paste_ui_header_new (win, client);
    GtkWidget *search_bar = g_paste_ui_search_bar_new ();
    GtkContainer *box = GTK_CONTAINER (vbox);

    priv->header = G_PASTE_UI_HEADER (header);

    gtk_window_set_titlebar (win, header);
    gtk_container_add (GTK_CONTAINER (win), vbox);
    gtk_container_add (box, search_bar);
    gtk_container_add (box, g_paste_ui_history_new (client));
    gtk_widget_show_all (GTK_WIDGET (win));

    g_object_bind_property (g_paste_ui_header_get_search_button (priv->header), "active",
                            search_bar,                                         "search-mode-enabled",
                            G_BINDING_BIDIRECTIONAL);

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
