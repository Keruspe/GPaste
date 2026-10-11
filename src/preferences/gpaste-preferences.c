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

/* A request a storage button sent holds the application past its dialog's
 * closing, until the daemon answers: a launch meanwhile reaches this process,
 * which has no dialog left to present, and is given one again. What runs
 * main () is tested by nothing, which is why this says so here. */
static void
on_activate (GApplication *gapp,
             gpointer      user_data G_GNUC_UNUSED)
{
    if (!gtk_application_get_windows (GTK_APPLICATION (gapp)))
        adw_dialog_present (g_paste_gtk_preferences_dialog_new (gapp), NULL);
}

gint
main (gint argc, gchar *argv[])
{
    G_PASTE_GTK_INIT_APPLICATION ("Preferences");

    GActionEntry app_entries[] = {
        { "quit",   quit_activated,   NULL, NULL, NULL, { 0 } },
    };

    g_action_map_add_action_entries (G_ACTION_MAP (app), app_entries, G_N_ELEMENTS (app_entries), app);

    AdwDialog *dialog = g_paste_gtk_preferences_dialog_new (gapp);

    adw_dialog_present (dialog, NULL);
    g_signal_connect (gapp, "activate", G_CALLBACK (on_activate), NULL);

    return g_application_run (gapp, argc, argv);
}
