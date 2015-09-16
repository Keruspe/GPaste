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

#include <gpaste-ui-about.h>
#include <gpaste-ui-empty.h>
#include <gpaste-ui-header.h>
#include <gpaste-ui-reexec.h>
#include <gpaste-ui-search.h>
#include <gpaste-ui-settings.h>
#include <gpaste-ui-switch.h>

struct _GPasteUiHeader
{
    GtkHeaderBar parent_instance;
};

typedef struct
{
    GtkButton *settings;
    GtkButton *search;
} GPasteUiHeaderPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteUiHeader, g_paste_ui_header, GTK_TYPE_HEADER_BAR)

/**
 * g_paste_ui_header_show_prefs:
 * @self: the #GPasteUiHeader
 *
 * Show the prefs pane
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_ui_header_show_prefs (const GPasteUiHeader *self)
{
    g_return_if_fail (G_PASTE_IS_UI_HEADER (self));

    GPasteUiHeaderPrivate *priv = g_paste_ui_header_get_instance_private (self);
    
    gtk_button_clicked (priv->settings);
}

/**
 * g_paste_ui_header_get_search_button:
 * @self: the #GPasteUiHeader
 *
 * Get the search button
 *
 * Returns: (transfer none): the #GPasteUISearch instance
 */
G_PASTE_VISIBLE GtkButton *
g_paste_ui_header_get_search_button (const GPasteUiHeader *self)
{
    g_return_val_if_fail (G_PASTE_IS_UI_HEADER (self), NULL);

    GPasteUiHeaderPrivate *priv = g_paste_ui_header_get_instance_private (self);

    return priv->search;
}

static void
g_paste_ui_header_class_init (GPasteUiHeaderClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_header_init (GPasteUiHeader *self)
{
    GPasteUiHeaderPrivate *priv = g_paste_ui_header_get_instance_private (self);
    GtkHeaderBar *header_bar = GTK_HEADER_BAR (self);
    GtkWidget *settings = g_paste_ui_settings_new ();
    GtkWidget *search = g_paste_ui_search_new ();

    priv->settings = GTK_BUTTON (settings);
    priv->search = GTK_BUTTON (search);

    gtk_header_bar_set_title(header_bar, PACKAGE_STRING);
    gtk_header_bar_set_show_close_button (header_bar, TRUE);
    gtk_header_bar_pack_end (header_bar, settings);
    gtk_header_bar_pack_end (header_bar, search);
}

/**
 * g_paste_ui_header_new:
 * @topwin: the main #GtkWindow
 * @client: a #GPasteClient instance
 *
 * Create a new instance of #GPasteUiHeader
 *
 * Returns: a newly allocated #GPasteUiHeader
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_header_new (GtkWindow    *topwin,
                       GPasteClient *client)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = gtk_widget_new (G_PASTE_TYPE_UI_HEADER, NULL);
    GtkHeaderBar *bar = GTK_HEADER_BAR (self);

    gtk_header_bar_pack_start (bar, g_paste_ui_switch_new (topwin, client));
    gtk_header_bar_pack_start (bar, g_paste_ui_reexec_new (topwin, client));

    gtk_header_bar_pack_end (bar, g_paste_ui_about_new (gtk_window_get_application (topwin)));
    gtk_header_bar_pack_end (bar, g_paste_ui_empty_new (topwin, client));

    return self;
}
