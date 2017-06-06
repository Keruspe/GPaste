/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-ui-window.h>
#include <gpaste-util.h>

static GPasteUiWindow *
get_ui_window(gpointer user_data)
{
    return G_PASTE_UI_WINDOW (gtk_application_get_windows (GTK_APPLICATION (user_data))->data);
}

static void
prefs_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data)
{
    g_paste_ui_window_show_prefs (get_ui_window (user_data));
}

static gboolean
show_about_dialog (gpointer user_data)
{
    if (!GTK_IS_WIDGET (widget))
        return G_SOURCE_REMOVE;

    if (!gtk_widget_get_realized (GTK_WIDGET (user_data)))
        return G_SOURCE_CONTINUE;

    GtkWindow *parent = GTK_WINDOW (user_data);
    const gchar *authors[] = {
        "Marc-Antoine Perennou <Marc-Antoine@Perennou.com>",
        NULL
    };

    gtk_show_about_dialog (parent,
                           "program-name",   PACKAGE_NAME,
                           "version",        PACKAGE_VERSION,
                           "logo-icon-name", G_PASTE_ICON_NAME,
                           "license-type",   GTK_LICENSE_BSD,
                           "authors",        authors,
                           "copyright",      "Copyright (c) 2010-2017, Marc-Antoine Perennou",
                           "comments",       "Clipboard management system",
                           "website",        "http://www.imagination-land.org/tags/GPaste.html",
                           "website-label",  "Follow GPaste news",
                           "wrap-license",   TRUE,
                           NULL);

    return G_SOURCE_REMOVE;
}

static void
about_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data)
{
    g_source_set_name_by_id (g_idle_add (show_about_dialog, gtk_application_get_windows (GTK_APPLICATION (user_data))->data), "[GPaste] about_dialog");
}

static void
quit_activated (GSimpleAction *action    G_GNUC_UNUSED,
                GVariant      *parameter G_GNUC_UNUSED,
                gpointer       user_data)
{
    g_application_quit (G_APPLICATION (user_data));
}

static void
search_activated (GSimpleAction *action    G_GNUC_UNUSED,
                  GVariant      *parameter,
                  gpointer       user_data)
{
    g_paste_ui_window_search (get_ui_window (user_data), g_variant_get_string (parameter, NULL));
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_INIT_APPLICATION_FULL ("Ui", g_paste_util_show_win);

    GActionEntry app_entries[] = {
        { "about",  about_activated,  NULL, NULL, NULL, { 0 } },
        { "prefs",  prefs_activated,  NULL, NULL, NULL, { 0 } },
        { "quit",   quit_activated,   NULL, NULL, NULL, { 0 } },
        { "search", search_activated, "s",  NULL, NULL, { 0 } }
    };

    g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app);

    GMenu *menu = g_menu_new ();
    g_menu_append (menu, _("GPaste Settings"),    "app.prefs");
    g_menu_append (menu, _("Keyboard Shortcuts"), "win.show-help-overlay");
    g_menu_append (menu, _("About GPaste"),       "app.about");
    g_menu_append (menu, _("Quit"),               "app.quit");
    gtk_application_set_app_menu (app, G_MENU_MODEL (menu));

    G_GNUC_UNUSED GtkWidget *window = g_paste_ui_window_new (app);

    return g_application_run (gapp, argc, argv);
}
