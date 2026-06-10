// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-dialog.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-header.h>
#include <gpaste-ui-new-item.h>
#include <gpaste-ui-search.h>
#include <gpaste-ui-switch.h>

typedef struct
{
    GtkButton       *settings;
    GtkToggleButton *search;
    AdwWindowTitle  *title;
} GPasteUiHeaderData;

/* A plain header GtkButton: shared icon/tooltip/valign setup plus a click
 * handler, so the toolbar's simple action buttons don't each need a subclass. */
static GtkWidget *
header_button_new (const gchar    *icon_name,
                   const gchar    *tooltip,
                   GCallback       clicked,
                   gpointer        user_data,
                   GClosureNotify  destroy)
{
    GtkWidget *button = gtk_button_new ();

    gtk_widget_set_tooltip_text (button, tooltip);
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_button_set_child (GTK_BUTTON (button), gtk_image_new_from_icon_name (icon_name));
    g_signal_connect_data (button, "clicked", clicked, user_data, destroy, 0);

    return button;
}

static void
on_about_clicked (GtkButton *button G_GNUC_UNUSED,
                  gpointer   user_data)
{
    g_action_group_activate_action (G_ACTION_GROUP (user_data), "about", NULL);
}

static void
on_settings_clicked (GtkButton *button,
                     gpointer   user_data G_GNUC_UNUSED)
{
    GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (button));
    AdwDialog *dialog = g_paste_gtk_preferences_dialog_new (NULL);

    adw_dialog_present (dialog, GTK_WIDGET (root));
}

typedef struct
{
    GPasteClient *client;
    GtkWindow    *topwin; /* not ref'd: outlives the header */
} ReexecData;

static void
reexec_data_free (gpointer  data,
                  GClosure *closure G_GNUC_UNUSED)
{
    g_autofree ReexecData *reexec = data;

    g_object_unref (reexec->client);
}

static void
on_reexec_confirmed (gboolean confirmed,
                     gpointer user_data)
{
    g_autoptr (GPasteClient) client = user_data;

    if (confirmed)
        g_paste_client_reexecute (client, NULL, NULL);
}

static void
on_reexec_clicked (GtkButton *button G_GNUC_UNUSED,
                   gpointer   user_data)
{
    const ReexecData *reexec = user_data;

    g_paste_gtk_util_confirm_dialog (reexec->topwin,
                                     _("Restart"),
                                     _("Do you really want to restart the daemon?"),
                                     on_reexec_confirmed,
                                     g_object_ref (reexec->client));
}

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
    GtkWidget *settings = header_button_new ("preferences-system-symbolic", _("GPaste Settings"), G_CALLBACK (on_settings_clicked), NULL, NULL);
    GtkWidget *search = g_paste_ui_search_new ();
    GtkWidget *title = adw_window_title_new (PACKAGE_NAME, NULL);

    gtk_widget_add_css_class (settings, "flat");

    ReexecData *reexec_data = g_new (ReexecData, 1);
    reexec_data->client = g_object_ref (client);
    reexec_data->topwin = topwin;

    GtkWidget *about = header_button_new ("dialog-information-symbolic", _("About"),
                                          G_CALLBACK (on_about_clicked), gtk_window_get_application (topwin), NULL);
    GtkWidget *reexec = header_button_new ("view-refresh-symbolic", _("Restart the daemon"),
                                           G_CALLBACK (on_reexec_clicked), reexec_data, reexec_data_free);

    GPasteUiHeaderData *data = g_new0 (GPasteUiHeaderData, 1);
    data->settings = GTK_BUTTON (settings);
    data->search = GTK_TOGGLE_BUTTON (search);
    data->title = ADW_WINDOW_TITLE (title);

    g_object_set_data_full (G_OBJECT (self), "header-data", data, g_free);

    adw_header_bar_set_title_widget (bar, title);
    adw_header_bar_pack_start (bar, g_paste_ui_switch_new (topwin, client));
    adw_header_bar_pack_start (bar, reexec);
    adw_header_bar_pack_end (bar, about);
    adw_header_bar_pack_end (bar, g_paste_ui_new_item_new (topwin, client));
    adw_header_bar_pack_end (bar, settings);
    adw_header_bar_pack_end (bar, search);

    return self;
}
