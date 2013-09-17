/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-settings-ui-notebook-private.h"

#include "gpaste-settings-keys.h"

#include <glib/gi18n.h>

#include <gpaste-client.h>
#include <gpaste-settings.h>

struct _GPasteSettingsUiNotebookPrivate
{
    GPasteClient    *client;
    GPasteSettings  *settings;
    GtkCheckButton  *fifo_button;
    GtkCheckButton  *images_support_button;
    GtkCheckButton  *primary_to_history_button;
    GtkCheckButton  *save_history_button;
    GtkCheckButton  *synchronize_clipboards_button;
    GtkCheckButton  *track_changes_button;
    GtkCheckButton  *track_extension_state_button;
    GtkCheckButton  *trim_items_button;
    GtkSpinButton   *element_size_button;
    GtkSpinButton   *max_displayed_history_size_button;
    GtkSpinButton   *max_history_size_button;
    GtkSpinButton   *max_text_item_size_button;
    GtkSpinButton   *min_text_item_size_button;
    GtkEntry        *backup_entry;
    GtkEntry        *paste_and_pop_entry;
    GtkEntry        *show_history_entry;
    GtkEntry        *sync_primary_to_clipboard_entry;
    GtkComboBoxText *targets;
    gchar         ***actions;
    gulong           settings_signal;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteSettingsUiNotebook, g_paste_settings_ui_notebook, GTK_TYPE_NOTEBOOK)

#define SETTING_CALLBACK_FULL(setting, type, cast)                            \
    static void                                                               \
    setting##_callback (type value,                                           \
                        gpointer user_data)                                   \
    {                                                                         \
        g_paste_settings_set_##setting (G_PASTE_SETTINGS (user_data), value); \
    }

#define SETTING_CALLBACK(setting, type) SETTING_CALLBACK_FULL (setting, type, type)

#define BOOLEAN_CALLBACK(setting) SETTING_CALLBACK (setting, gboolean)
#define STRING_CALLBACK(setting)  SETTING_CALLBACK (setting, const gchar *)

#define UINT_CALLBACK(setting) SETTING_CALLBACK_FULL (setting, gdouble, uint)

/**
 * g_paste_settings_ui_notebook_add_panel:
 * @self: a #GPasteSettingsUiNotebook instance
 * @label: the label to display
 * @panel: (transfer none): the #GPasteSettingsUiPanel to add
 *
 * Add a new panel to the #GPasteSettingsUiNotebook
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_ui_notebook_add_panel (GPasteSettingsUiNotebook *self,
                                        const gchar              *label,
                                        GPasteSettingsUiPanel    *panel)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS_UI_NOTEBOOK (self));

    gtk_notebook_append_page (GTK_NOTEBOOK (self),
                              GTK_WIDGET (panel),
                              gtk_label_new (label));
}

BOOLEAN_CALLBACK (track_changes)
#ifdef ENABLE_EXTENSION
BOOLEAN_CALLBACK (track_extension_state)
#endif
BOOLEAN_CALLBACK (primary_to_history)
BOOLEAN_CALLBACK (synchronize_clipboards)
BOOLEAN_CALLBACK (images_support)
BOOLEAN_CALLBACK (trim_items)
BOOLEAN_CALLBACK (save_history)
BOOLEAN_CALLBACK (fifo)

static GPasteSettingsUiPanel *
g_paste_settings_ui_notebook_make_behaviour_panel (GPasteSettingsUiNotebook *self)
{
    GPasteSettingsUiNotebookPrivate *priv = self->priv;
    GPasteSettings *settings = priv->settings;
    GPasteSettingsUiPanel *panel = g_paste_settings_ui_panel_new ();

    priv->track_changes_button = g_paste_settings_ui_panel_add_boolean_setting (panel,
                                                                                _("_Track clipboard changes"),
                                                                                g_paste_settings_get_track_changes (settings),
                                                                                track_changes_callback,
                                                                                settings);
#ifdef ENABLE_EXTENSION
    priv->track_extension_state_button = g_paste_settings_ui_panel_add_boolean_setting (panel,
                                                                                         _("Sync the daemon state with the _extension's one"),
                                                                                        g_paste_settings_get_track_extension_state (settings),
                                                                                        track_extension_state_callback,
                                                                                        settings);
#endif
    g_paste_settings_ui_panel_add_separator (panel);
    priv->primary_to_history_button = g_paste_settings_ui_panel_add_boolean_setting (panel,
                                                                                     _("_Primary selection affects history"),
                                                                                     g_paste_settings_get_primary_to_history (settings),
                                                                                     primary_to_history_callback,
                                                                                     settings);
    priv->synchronize_clipboards_button = g_paste_settings_ui_panel_add_boolean_setting (panel,
                                                                                         _("_Synchronize clipboard with primary selection"),
                                                                                         g_paste_settings_get_synchronize_clipboards (settings),
                                                                                         synchronize_clipboards_callback,
                                                                                         settings);
    g_paste_settings_ui_panel_add_separator (panel);
    priv->images_support_button = g_paste_settings_ui_panel_add_boolean_setting (panel,
                                                                                 _("_Images support"),
                                                                                 g_paste_settings_get_images_support (settings),
                                                                                 images_support_callback,
                                                                                 settings);
    priv->trim_items_button = g_paste_settings_ui_panel_add_boolean_setting (panel,
                                                                              _("_Trim items"),
                                                                             g_paste_settings_get_trim_items (settings),
                                                                             trim_items_callback,
                                                                             settings);
    g_paste_settings_ui_panel_add_separator (panel);
    priv->save_history_button = g_paste_settings_ui_panel_add_boolean_setting (panel,
                                                                               _("_Save history"),
                                                                               g_paste_settings_get_save_history (settings),
                                                                               save_history_callback,
                                                                               settings);
    priv->fifo_button = g_paste_settings_ui_panel_add_boolean_setting (panel,
                                                                       _("_Copy to end of history"),
                                                                       g_paste_settings_get_fifo (settings),
                                                                       fifo_callback,
                                                                       settings);

    return panel;
}

UINT_CALLBACK (element_size)
UINT_CALLBACK (max_displayed_history_size)
UINT_CALLBACK (max_history_size)
UINT_CALLBACK (max_text_item_size)
UINT_CALLBACK (min_text_item_size)

static GPasteSettingsUiPanel *
g_paste_settings_ui_notebook_make_history_settings_panel (GPasteSettingsUiNotebook *self)
{
    GPasteSettingsUiNotebookPrivate *priv = self->priv;
    GPasteSettings *settings = priv->settings;
    GPasteSettingsUiPanel *panel = g_paste_settings_ui_panel_new ();

    priv->element_size_button = g_paste_settings_ui_panel_add_range_setting (panel,
                                                                             _("Max element size when displaying: "),
                                                                             (gdouble) g_paste_settings_get_element_size (settings),
                                                                             0, 255, 5,
                                                                             element_size_callback, settings);
    priv->max_displayed_history_size_button = g_paste_settings_ui_panel_add_range_setting (panel,
                                                                                           _("Max displayed history size: "),
                                                                                           (gdouble) g_paste_settings_get_max_displayed_history_size (settings),
                                                                                           5, 255, 5,
                                                                                           max_displayed_history_size_callback, settings);
    priv->max_history_size_button = g_paste_settings_ui_panel_add_range_setting (panel,
                                                                                 _("Max history size: "),
                                                                                 (gdouble) g_paste_settings_get_max_history_size (settings),
                                                                                 5, 255, 5,
                                                                                 max_history_size_callback, settings);
    priv->max_text_item_size_button = g_paste_settings_ui_panel_add_range_setting (panel,
                                                                                   _("Max text item length: "),
                                                                                   (gdouble) g_paste_settings_get_max_text_item_size (settings),
                                                                                   1, G_MAXUINT, 1,
                                                                                   max_text_item_size_callback, settings);
    priv->min_text_item_size_button = g_paste_settings_ui_panel_add_range_setting (panel,
                                                                                   _("Min text item length: "),
                                                                                   (gdouble) g_paste_settings_get_min_text_item_size (settings),
                                                                                   1, G_MAXUINT, 1,
                                                                                   min_text_item_size_callback, settings);

    return panel;
}

STRING_CALLBACK (paste_and_pop)
STRING_CALLBACK (show_history)
STRING_CALLBACK (sync_primary_to_clipboard)

static GPasteSettingsUiPanel *
g_paste_settings_ui_notebook_make_keybindings_panel (GPasteSettingsUiNotebook *self)
{
    GPasteSettingsUiNotebookPrivate *priv = self->priv;
    GPasteSettings *settings = priv->settings;
    GPasteSettingsUiPanel *panel = g_paste_settings_ui_panel_new ();

    /* translators: Keyboard shortcut to paste and then delete the first item in history */
    priv->paste_and_pop_entry = g_paste_settings_ui_panel_add_text_setting (panel,
                                                                            _("Paste and then delete the first item in history: "),
                                                                            g_paste_settings_get_paste_and_pop (settings),
                                                                            paste_and_pop_callback, settings);
    /* translators: Keyboard shortcut to display the history */
    priv->show_history_entry = g_paste_settings_ui_panel_add_text_setting (panel,
                                                                           _("Display the history: "),
                                                                           g_paste_settings_get_show_history (settings),
                                                                           show_history_callback, settings);
    /* translators: Keyboard shortcut to sync the primary selection to the clipboard */
    priv->sync_primary_to_clipboard_entry = g_paste_settings_ui_panel_add_text_setting (panel,
                                                                                        _("Sync the primary selection to clipboard: "),
                                                                                        g_paste_settings_get_sync_primary_to_clipboard (settings),
                                                                                        sync_primary_to_clipboard_callback, settings);

    return panel;
}

static void
g_paste_settings_ui_notebook_refill_histories (GPasteSettingsUiNotebook *self)
{
    GPasteSettingsUiNotebookPrivate *priv = self->priv;
    GError *error = NULL;
    GStrv histories = g_paste_client_list_histories (priv->client, &error);

    if (error)
    {
        fprintf (stderr, "%s\n", _("Couldn't connect to GPaste daemon."));
        g_error_free (error);
        return;
    }

    GtkComboBoxText *targets = priv->targets;

    gtk_combo_box_text_remove_all (targets);

    for (guint i = 0; histories[i]; ++i)
        gtk_combo_box_text_append (targets, histories[i], histories[i]);

    gtk_combo_box_set_active_id (GTK_COMBO_BOX (targets),
                                 g_paste_settings_get_history_name (priv->settings));
}

static void
dummy_callback (const gchar *value G_GNUC_UNUSED,
                gpointer     user_data G_GNUC_UNUSED)
{
}

static void
backup_callback (const gchar *value,
                 gpointer     user_data)
{
    GPasteSettingsUiNotebook *self = G_PASTE_SETTINGS_UI_NOTEBOOK (user_data);
    GError *error = NULL;

    g_paste_client_backup_history (self->priv->client, value, &error);

    if (error)
    {
        fprintf (stderr, "%s\n", _("Couldn't connect to GPaste daemon."));
        g_error_free (error);
        return;
    }

    g_paste_settings_ui_notebook_refill_histories (self);
}

static void
targets_callback (const gchar *action,
                  const gchar *target,
                  gpointer     user_data)
{
    GPasteSettingsUiNotebook *self = G_PASTE_SETTINGS_UI_NOTEBOOK (user_data);
    GPasteClient *client = self->priv->client;
    GError *error = NULL;

    if (!g_strcmp0 (action, "switch"))
        g_paste_client_switch_history (client, target, &error);
    else if (!g_strcmp0 (action, "delete"))
        g_paste_client_delete_history (client, target, &error);
    else
        fprintf (stderr, "unknown action: %s\n", action);

    if (error)
    {
        fprintf(stderr, "%s\n", _("Couldn't connect to GPaste daemon."));
        g_error_free (error);
        return;
    }

    g_paste_settings_ui_notebook_refill_histories (self);
}

static GPasteSettingsUiPanel *
g_paste_settings_ui_notebook_make_histories_panel (GPasteSettingsUiNotebook *self)
{
    GPasteSettingsUiNotebookPrivate *priv = self->priv;
    GPasteSettings *settings = priv->settings;
    GPasteSettingsUiPanel *panel = g_paste_settings_ui_panel_new ();

    gchar *backup_name = g_strconcat (g_paste_settings_get_history_name (settings), "_backup", NULL);
    priv->backup_entry = g_paste_settings_ui_panel_add_text_confirm_setting (panel,
                                                                             _("Backup history as: "),
                                                                             backup_name,
                                                                             dummy_callback,
                                                                             NULL,
                                                                             /* translators: This is the name of a multi-history management action */
                                                                             _("Backup"),
                                                                             backup_callback,
                                                                             self);
    g_free (backup_name);

    /* translators: This is the text displayed on the button used to perform a multi-history management action */
    priv->targets = g_paste_settings_ui_panel_add_multi_action_setting (panel,
                                                                        (gchar ** const *) priv->actions,
                                                                        _("Ok"),
                                                                        targets_callback,
                                                                        self);

    g_paste_settings_ui_notebook_refill_histories (self);

    return panel;
}

/**
 * g_paste_settings_ui_notebook_fill:
 * @self: a #GPasteSettingsUiNotebook instance
 *
 * Fill the #GPasteSettingsUiNotebook with default panels
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_ui_notebook_fill (GPasteSettingsUiNotebook *self)
{
    g_paste_settings_ui_notebook_add_panel (self, _("General behaviour"), g_paste_settings_ui_notebook_make_behaviour_panel (self));
    g_paste_settings_ui_notebook_add_panel (self, _("History settings"), g_paste_settings_ui_notebook_make_history_settings_panel (self));
    g_paste_settings_ui_notebook_add_panel (self, _("Keyboard shortcuts"), g_paste_settings_ui_notebook_make_keybindings_panel (self));
    g_paste_settings_ui_notebook_add_panel (self, _("Histories"), g_paste_settings_ui_notebook_make_histories_panel (self));
}

static void
g_paste_settings_ui_notebook_settings_changed (GSettings   *gsettings G_GNUC_UNUSED,
                                             const gchar *key,
                                             gpointer     user_data)
{
    GPasteSettingsUiNotebookPrivate *priv = G_PASTE_SETTINGS_UI_NOTEBOOK (user_data)->priv;
    GPasteSettings *settings = priv->settings;

    if (g_strcmp0 (key, ELEMENT_SIZE_KEY) == 0)
        gtk_spin_button_set_value (priv->element_size_button, g_paste_settings_get_element_size (settings));
    else if (g_strcmp0 (key, FIFO_KEY) == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->fifo_button), g_paste_settings_get_fifo (settings));
    else if (g_strcmp0 (key, HISTORY_NAME_KEY) == 0)
    {
        gchar *text = g_strconcat (g_paste_settings_get_history_name (settings), "_backup", NULL);
        gtk_entry_set_text (priv->backup_entry, text);
        g_free (text);
    }
    else if (g_strcmp0 (key, IMAGES_SUPPORT_KEY) == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->images_support_button), g_paste_settings_get_images_support (settings));
    else if (g_strcmp0 (key, MAX_DISPLAYED_HISTORY_SIZE_KEY) == 0)
        gtk_spin_button_set_value (priv->max_displayed_history_size_button, g_paste_settings_get_max_displayed_history_size (settings));
    else if (g_strcmp0 (key, MAX_HISTORY_SIZE_KEY) == 0)
        gtk_spin_button_set_value (priv->max_history_size_button, g_paste_settings_get_max_history_size (settings));
    else if (g_strcmp0 (key, MAX_TEXT_ITEM_SIZE_KEY) == 0)
        gtk_spin_button_set_value (priv->max_text_item_size_button, g_paste_settings_get_max_text_item_size (settings));
    else if (g_strcmp0 (key, MIN_TEXT_ITEM_SIZE_KEY) == 0)
        gtk_spin_button_set_value (priv->min_text_item_size_button, g_paste_settings_get_min_text_item_size (settings));
    else if (g_strcmp0 (key, PASTE_AND_POP_KEY) == 0)
        gtk_entry_set_text (priv->paste_and_pop_entry, g_paste_settings_get_paste_and_pop (settings));
    else if (g_strcmp0 (key, PRIMARY_TO_HISTORY_KEY ) == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->primary_to_history_button), g_paste_settings_get_primary_to_history (settings));
    else if (g_strcmp0 (key, SAVE_HISTORY_KEY) == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->save_history_button), g_paste_settings_get_save_history (settings));
    else if (g_strcmp0 (key, SHOW_HISTORY_KEY) == 0)
        gtk_entry_set_text (priv->show_history_entry, g_paste_settings_get_show_history (settings));
    else if (g_strcmp0 (key, SYNC_PRIMARY_TO_CLIPBOARD_KEY) == 0)
        gtk_entry_set_text (priv->sync_primary_to_clipboard_entry, g_paste_settings_get_sync_primary_to_clipboard (settings));
    else if (g_strcmp0 (key, SYNCHRONIZE_CLIPBOARDS_KEY) == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->synchronize_clipboards_button), g_paste_settings_get_synchronize_clipboards (settings));
    else if (g_strcmp0 (key, TRACK_CHANGES_KEY) == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->track_changes_button), g_paste_settings_get_track_changes (settings));
#ifdef ENABLE_EXTENSION
    else if (g_strcmp0 (key, TRACK_EXTENSION_STATE_KEY) == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->track_extension_state_button), g_paste_settings_get_track_extension_state (settings));
#endif
    else if (g_strcmp0 (key, TRIM_ITEMS_KEY) == 0)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->trim_items_button), g_paste_settings_get_trim_items (settings));
}

static void
g_paste_settings_ui_notebook_dispose (GObject *object)
{
    GPasteSettingsUiNotebookPrivate *priv = G_PASTE_SETTINGS_UI_NOTEBOOK (object)->priv;
    GPasteSettings *settings = priv->settings;

    if (settings) /* first dispose call */
    {
        g_signal_handler_disconnect (settings, priv->settings_signal);
        g_object_unref (settings);
        g_object_unref (priv->client);
        priv->settings = NULL;
    }

    G_OBJECT_CLASS (g_paste_settings_ui_notebook_parent_class)->dispose (object);
}

static void
g_paste_settings_ui_notebook_finalize (GObject *object)
{
    gchar ***actions = G_PASTE_SETTINGS_UI_NOTEBOOK (object)->priv->actions;

    for (guint i = 0; actions[i]; ++i)
        g_free ((gchar **)actions[i]);
    g_free ((gchar ***)actions);

    G_OBJECT_CLASS (g_paste_settings_ui_notebook_parent_class)->finalize (object);
}

static void
g_paste_settings_ui_notebook_class_init (GPasteSettingsUiNotebookClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_settings_ui_notebook_dispose;
    object_class->finalize = g_paste_settings_ui_notebook_finalize;
}

static void
g_paste_settings_ui_notebook_init (GPasteSettingsUiNotebook *self)
{
    GPasteSettingsUiNotebookPrivate *priv = self->priv = g_paste_settings_ui_notebook_get_instance_private (self);

    priv->client = g_paste_client_new ();
    priv->settings = g_paste_settings_new ();
    priv->settings_signal = g_signal_connect (priv->settings,
                                              "changed",
                                              G_CALLBACK (g_paste_settings_ui_notebook_settings_changed),
                                              self);

    gchar ***actions = priv->actions = (gchar ***) g_malloc (3 * sizeof (gchar **));

    gchar **action = actions[0] = (gchar **) g_malloc (2 * sizeof (gchar *));
    action[0] = (gchar *) "switch";
    /* translators: This is the name of a multi-history management action */
    action[1] = _("Switch to");

    action = actions[1] = (gchar **) g_malloc (2 * sizeof (gchar *));
    action[0] = (gchar *) "delete";
    /* translators: This is the name of a multi-history management action */
    action[1] = _("Delete");

    actions[2] = NULL;

}

/**
 * g_paste_settings_ui_notebook_new:
 *
 * Create a new instance of #GPasteSettingsUiNotebook
 *
 * Returns: a newly allocated #GPasteSettingsUiNotebook
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteSettingsUiNotebook *
g_paste_settings_ui_notebook_new (void)
{
    return G_PASTE_SETTINGS_UI_NOTEBOOK (gtk_widget_new (G_PASTE_TYPE_SETTINGS_UI_NOTEBOOK, "margin", 12, NULL));
}
