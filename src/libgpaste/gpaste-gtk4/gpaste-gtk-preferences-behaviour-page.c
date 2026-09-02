// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>

/* The question the group is waiting on, kept on the group so that the group
 * going is what takes it back. */
#define SHELL_QUESTION_KEY "gnome-shell-question"

static void
on_has_gnome_shell_extension (GObject      *source_object G_GNUC_UNUSED,
                              GAsyncResult *res,
                              gpointer      user_data)
{
    g_autofree GWeakRef *group_ref = user_data;
    gboolean present = g_paste_util_has_gnome_shell_extension_finish (res);
    g_autoptr (GtkWidget) group = g_weak_ref_get (group_ref);

    g_weak_ref_clear (group_ref);

    if (!group || g_cancellable_is_cancelled (g_object_get_data (G_OBJECT (group), SHELL_QUESTION_KEY)))
        return;

    gtk_widget_set_visible (group, present);
}

/* Stop the query when the group goes, even if the shell is not answering. */
static void
on_shell_group_destroy (GtkWidget *group,
                        gpointer   user_data G_GNUC_UNUSED)
{
    g_cancellable_cancel (g_object_get_data (G_OBJECT (group), SHELL_QUESTION_KEY));
}

/**
 * g_paste_gtk_preferences_behaviour_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Build the preferences page
 *
 * Returns: (transfer full): a newly allocated #AdwPreferencesPage
 */
AdwPreferencesPage *
g_paste_gtk_preferences_behaviour_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwPreferencesPage *self = ADW_PREFERENCES_PAGE (g_object_new (ADW_TYPE_PREFERENCES_PAGE,
                                                                   "name", "behaviour",
                                                                   "title", _("General Behaviour"),
                                                                   "icon-name", "preferences-system",
                                                                   NULL));

    GPasteGtkPreferencesGroup *group = g_paste_gtk_preferences_group_new (_("General Behaviour"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Track Clipboard Changes"),
                                                       G_PASTE_TRACK_CHANGES_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Close UI on Select"),
                                                       G_PASTE_CLOSE_ON_SELECT_SETTING,
                                                       settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    /* Every row below is about the extension, so the group stands or falls with
     * it: a switch that enables an extension which is not installed writes
     * GPaste into the shell's enabled-extensions and changes nothing anybody
     * can see. Only the shell knows what it has loaded, and it answers over the
     * bus, so the group is built in place and hidden until the answer comes
     * back -- built here rather than appended from the callback so that it
     * lands between the groups it belongs between whenever it does appear. */
    group = g_paste_gtk_preferences_group_new ("GNOME Shell");
    gtk_widget_set_visible (GTK_WIDGET (group), FALSE);

    /* "extension-enabled" is derived from the shell schema, not a plain key, so
     * it has no default to reset to: bind it without a reset suffix. */
    AdwSwitchRow *extension_enabled_switch = ADW_SWITCH_ROW (adw_switch_row_new ());
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (extension_enabled_switch), _("Enable the GNOME Shell Extension"));
    g_object_bind_property (settings, G_PASTE_EXTENSION_ENABLED_SETTING, extension_enabled_switch, "active",
                            G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), GTK_WIDGET (extension_enabled_switch));

    AdwSwitchRow *track_extension_state_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                                    _("Match the Daemon State to the Extension's"),
                                                                                                    G_PASTE_TRACK_EXTENSION_STATE_SETTING,
                                                                                                    settings);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (track_extension_state_switch),
                                 _("When enabled, the daemon automatically starts or stops tracking clipboard changes to match the GNOME Shell extension's enabled state"));

    AdwSwitchRow *experimental_meta_daemon_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                                       _("Use the Experimental In-Shell Daemon"),
                                                                                                       G_PASTE_EXPERIMENTAL_META_DAEMON_SETTING,
                                                                                                       settings);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (experimental_meta_daemon_switch),
                                 _("Experimental: run the daemon inside GNOME Shell (mutter clipboard) instead of the standalone one. Takes effect after the extension restarts"));

    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    GCancellable *cancellable = g_cancellable_new ();

    g_object_set_data_full (G_OBJECT (group), SHELL_QUESTION_KEY, cancellable, g_object_unref);
    g_signal_connect (group, "destroy", G_CALLBACK (on_shell_group_destroy), NULL);

    /* A strong reference would keep the group alive and prevent its destroy
     * handler from cancelling the query when the page releases it. */
    GWeakRef *group_ref = g_new0 (GWeakRef, 1);

    g_weak_ref_init (group_ref, group);
    g_paste_util_has_gnome_shell_extension (cancellable, on_has_gnome_shell_extension, group_ref);

    group = g_paste_gtk_preferences_group_new (_("Clipboard Synchronization"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Primary Selection Affects History"),
                                                       G_PASTE_PRIMARY_TO_HISTORY_SETTING,
                                                       settings);
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Synchronize Clipboard With Primary Selection"),
                                                       G_PASTE_SYNCHRONIZE_CLIPBOARDS_SETTING,
                                                       settings);
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    group = g_paste_gtk_preferences_group_new (_("Optional Features"));
    g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                       _("Trim Items"),
                                                       G_PASTE_TRIM_ITEMS_SETTING,
                                                       settings);
    AdwSwitchRow *growing_lines_switch = g_paste_gtk_preferences_group_add_boolean_setting (group,
                                                                                           _("Detect Growing Lines"),
                                                                                           G_PASTE_GROWING_LINES_SETTING,
                                                                                           settings);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (growing_lines_switch),
                                 _("When enabled, if a new clipboard entry starts with the previous one, the previous entry is replaced instead of creating a new one"));
    AdwSpinRow *password_timeout_spin = g_paste_gtk_preferences_group_add_range_setting (group,
                                                                                         _("Clear a Password After (seconds)"),
                                                                                         G_PASTE_PASSWORD_TIMEOUT_SETTING,
                                                                                         0, G_PASTE_PASSWORD_TIMEOUT_MAX, 5,
                                                                                         settings);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (password_timeout_spin),
                                 _("How long a password stays on the clipboard once it is the active item. When it runs out, the next item that is not a password is selected. 0 lets a password stay for as long as anything else would"));
    /* An AdwEntryRow has a title and no subtitle, so what the command has to
     * honour goes in a tooltip rather than under the field. */
    AdwEntryRow *upload_command_entry = g_paste_gtk_preferences_group_add_text_setting (group,
                                                                                        _("Pastebin Upload Command"),
                                                                                        G_PASTE_UPLOAD_COMMAND_SETTING,
                                                                                        settings);
    gtk_widget_set_tooltip_text (GTK_WIDGET (upload_command_entry),
                                 _("The item is written to this command's standard input, and the command answers the url it uploaded it to, and nothing else, on its standard output. Arguments are allowed; pipes and redirections are not"));
    adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    return self;
}
