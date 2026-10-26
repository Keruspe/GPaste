// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-dialog.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

static void
quit_activated (GSimpleAction *action    G_GNUC_UNUSED,
                GVariant      *parameter G_GNUC_UNUSED,
                gpointer       user_data)
{
    g_application_quit (G_APPLICATION (user_data));
}

/* Presented with no parent, the dialog makes a window of its own: made the
 * application's, it is the one a second launch raises
 * (g_paste_gtk_util_show_window ()) and the one the shell matches to the
 * desktop entry. No test shows it, since that takes a display. */
static void
present_preferences (GtkApplication *app)
{
    AdwDialog *dialog = g_paste_gtk_preferences_dialog_new (G_APPLICATION (app));

    adw_dialog_present (dialog, NULL);
    gtk_window_set_application (GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (dialog))), app);
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_GTK_INIT_APPLICATION ("Preferences");

    GActionEntry app_entries[] = {
        { "quit",   quit_activated,   NULL, NULL, NULL, { 0 } },
    };

    g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app);

    present_preferences (app);

    return g_application_run (gapp, argc, argv);
}
