/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-about.h>
#include <gpaste-ui-header.h>
#include <gpaste-ui-new-item.h>
#include <gpaste-ui-reexec.h>
#include <gpaste-ui-search.h>
#include <gpaste-ui-settings.h>
#include <gpaste-ui-switch.h>

typedef struct
{
    GtkButton       *settings;
    GtkToggleButton *search;
    AdwWindowTitle  *title;
} GPasteUiHeaderData;

/**
 * g_paste_ui_header_show_prefs:
 * @self: the header bar
 *
 * Show the prefs pane
 */
G_PASTE_VISIBLE void
g_paste_ui_header_show_prefs (AdwHeaderBar *self)
{
    g_return_if_fail (ADW_IS_HEADER_BAR (self));

    GPasteUiHeaderData *data = g_object_get_data (G_OBJECT (self), "header-data");

    gtk_widget_activate (GTK_WIDGET (data->settings));
}

/**
 * g_paste_ui_header_set_subtitle:
 * @self: the header bar
 * @subtitle: the subtitle to display (current history name)
 *
 * Update the subtitle shown in the window title widget
 */
G_PASTE_VISIBLE void
g_paste_ui_header_set_subtitle (AdwHeaderBar *self,
                                const gchar  *subtitle)
{
    g_return_if_fail (ADW_IS_HEADER_BAR (self));

    GPasteUiHeaderData *data = g_object_get_data (G_OBJECT (self), "header-data");

    adw_window_title_set_subtitle (data->title, subtitle);
}

/**
 * g_paste_ui_header_get_search_button:
 * @self: the header bar
 *
 * Get the search button
 *
 * Returns: (transfer none): the #GtkToggleButton for search
 */
G_PASTE_VISIBLE GtkToggleButton *
g_paste_ui_header_get_search_button (AdwHeaderBar *self)
{
    g_return_val_if_fail (ADW_IS_HEADER_BAR (self), NULL);

    GPasteUiHeaderData *data = g_object_get_data (G_OBJECT (self), "header-data");

    return data->search;
}

/**
 * g_paste_ui_header_new:
 * @topwin: the main #GtkWindow
 * @client: a #GPasteClient instance
 *
 * Create a new #AdwHeaderBar configured for GPaste
 *
 * Returns: a newly allocated #AdwHeaderBar
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
g_paste_ui_header_new (GtkWindow    *topwin,
                       GPasteClient *client)
{
    g_return_val_if_fail (GTK_IS_WINDOW (topwin), NULL);
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), NULL);

    GtkWidget *self = adw_header_bar_new ();
    AdwHeaderBar *bar = ADW_HEADER_BAR (self);
    GtkWidget *settings = g_paste_ui_settings_new ();
    GtkWidget *search = g_paste_ui_search_new ();
    GtkWidget *title = adw_window_title_new (PACKAGE_NAME, NULL);

    GPasteUiHeaderData *data = g_new0 (GPasteUiHeaderData, 1);
    data->settings = GTK_BUTTON (settings);
    data->search = GTK_TOGGLE_BUTTON (search);
    data->title = ADW_WINDOW_TITLE (title);

    g_object_set_data_full (G_OBJECT (self), "header-data", data, g_free);

    adw_header_bar_set_title_widget (bar, title);
    adw_header_bar_pack_start (bar, g_paste_ui_switch_new (topwin, client));
    adw_header_bar_pack_start (bar, g_paste_ui_reexec_new (topwin, client));
    adw_header_bar_pack_end (bar, g_paste_ui_about_new (gtk_window_get_application (topwin)));
    adw_header_bar_pack_end (bar, g_paste_ui_new_item_new (topwin, client));
    adw_header_bar_pack_end (bar, settings);
    adw_header_bar_pack_end (bar, search);

    return self;
}
