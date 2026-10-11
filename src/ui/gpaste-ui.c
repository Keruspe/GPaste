// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-macros.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-window.h>

/* %NULL once the window is gone: an action can still be dispatched in the
 * iteration that closed it, the application quitting at its end. */
static GPasteUiWindow *
get_ui_window (gpointer user_data)
{
    GList *windows = gtk_application_get_windows (GTK_APPLICATION (user_data));

    return (windows) ? G_PASTE_UI_WINDOW (windows->data) : NULL;
}

static void
prefs_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data)
{
    GPasteUiWindow *window = get_ui_window (user_data);

    if (window)
        g_paste_ui_window_show_prefs (window);
}

static void
about_activated (GSimpleAction *action    G_GNUC_UNUSED,
                 GVariant      *parameter G_GNUC_UNUSED,
                 gpointer       user_data)
{
    GPasteUiWindow *window = get_ui_window (user_data);

    if (window)
        g_paste_ui_window_show_about (window);
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
    GPasteUiWindow *window = get_ui_window (user_data);

    if (window)
        g_paste_ui_window_search (window, g_variant_get_string (parameter, NULL));
}

/* As gpaste-preferences' on_activate (): a storage request from the
 * preferences it hosts holds the application past its window's closing, and a
 * launch meanwhile is given a window again. */
static void
on_activate (GApplication *gapp,
             gpointer      user_data G_GNUC_UNUSED)
{
    if (!gtk_application_get_windows (GTK_APPLICATION (gapp)))
        g_paste_ui_window_new (GTK_APPLICATION (gapp));
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_GTK_INIT_APPLICATION ("Ui");

    GActionEntry app_entries[] = {
        { "about",  about_activated,  NULL, NULL, NULL, { 0 } },
        { "prefs",  prefs_activated,  NULL, NULL, NULL, { 0 } },
        { "quit",   quit_activated,   NULL, NULL, NULL, { 0 } },
        { "search", search_activated, "s",  NULL, NULL, { 0 } }
    };

    g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app);

    G_GNUC_UNUSED GtkWidget *window = g_paste_ui_window_new (app);

    g_signal_connect (gapp, "activate", G_CALLBACK (on_activate), NULL);

    return g_application_run (gapp, argc, argv);
}
