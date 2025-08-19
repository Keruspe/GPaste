/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gtk3/gpaste-gtk-settings-ui-stack.h>

struct _GPasteGtkSettingsUiStack
{
    GtkStack parent_instance;
};

enum
{
    C_SETTINGS,

    C_LAST_SIGNAL
};

typedef struct
{
    GPasteSettings *settings;

    GtkSwitch      *close_on_select_switch;
    GtkSwitch      *open_centered_switch;
    GtkSwitch      *images_support_switch;
    GtkSwitch      *images_preview_switch;
    GtkSpinButton  *images_preview_size_button;
    GtkSwitch      *growing_lines_switch;
    GtkSwitch      *primary_to_history_switch;
    GtkSwitch      *save_history_switch;
    GtkSwitch      *synchronize_clipboards_switch;
    GtkSwitch      *track_changes_switch;
    GtkSwitch      *trim_items_switch;
    GtkSpinButton  *element_size_button;
    GtkSpinButton  *max_displayed_history_size_button;
    GtkSpinButton  *max_history_size_button;
    GtkSpinButton  *max_memory_usage_button;
    GtkSpinButton  *max_text_item_size_button;
    GtkSpinButton  *min_text_item_size_button;
    GtkEntry       *launch_ui_entry;
    GtkEntry       *make_password_entry;
    GtkEntry       *pop_entry;
    GtkEntry       *show_history_entry;
    GtkEntry       *sync_clipboard_to_primary_entry;
    GtkEntry       *sync_primary_to_clipboard_entry;
    GtkEntry       *upload_entry;

    GtkSwitch      *extension_enabled_switch;
    GtkSwitch      *track_extension_state_switch;

    guint64         c_signals[C_LAST_SIGNAL];
} GPasteGtkSettingsUiStackPrivate;

G_PASTE_GTK_DEFINE_TYPE_WITH_PRIVATE (SettingsUiStack, settings_ui_stack, GTK_TYPE_STACK)

#define SETTING_CALLBACK_FULL(setting, type, cast)                            \
    static inline void                                                        \
    setting##_callback (type value,                                           \
                        gpointer user_data)                                   \
    {                                                                         \
        g_paste_settings_set_##setting (G_PASTE_SETTINGS (user_data), value); \
    }

#define SETTING_CALLBACK(setting, type) SETTING_CALLBACK_FULL (setting, type, type)

#define BOOLEAN_CALLBACK(setting) SETTING_CALLBACK (setting, gboolean)
#define STRING_CALLBACK(setting)  SETTING_CALLBACK (setting, const gchar *)

#define UINT64_CALLBACK(setting) SETTING_CALLBACK_FULL (setting, gdouble, uint64)

/**
 * g_paste_gtk_settings_ui_stack_add_panel:
 * @self: a #GPasteGtkSettingsUiStack instance
 * @name: the name of the panel
 * @label: the label to display
 * @panel: (transfer none): the #GPasteGtkSettingsUiPanel to add
 *
 * Add a new panel to the #GPasteGtkSettingsUiStack
 */
G_PASTE_VISIBLE void
g_paste_gtk_settings_ui_stack_add_panel (GPasteGtkSettingsUiStack *self,
                                         const gchar              *name,
                                         const gchar              *label,
                                         GPasteGtkSettingsUiPanel *panel)
{
    g_return_if_fail (G_PASTE_IS_GTK_SETTINGS_UI_STACK (self));

    gtk_stack_add_titled (GTK_STACK (self),
                          GTK_WIDGET (panel),
                          name, label);
}

BOOLEAN_CALLBACK (close_on_select)
BOOLEAN_CALLBACK (open_centered)
BOOLEAN_CALLBACK (extension_enabled)
BOOLEAN_CALLBACK (growing_lines)
BOOLEAN_CALLBACK (images_support)
BOOLEAN_CALLBACK (images_preview)
UINT64_CALLBACK (images_preview_size)
BOOLEAN_CALLBACK (primary_to_history)
BOOLEAN_CALLBACK (save_history)
BOOLEAN_CALLBACK (synchronize_clipboards)
BOOLEAN_CALLBACK (track_changes)
BOOLEAN_CALLBACK (track_extension_state)
BOOLEAN_CALLBACK (trim_items)

static GPasteGtkSettingsUiPanel *
g_paste_gtk_settings_ui_stack_private_make_behaviour_panel (GPasteGtkSettingsUiStackPrivate *priv)
{
    GPasteSettings *settings = priv->settings;
    GPasteGtkSettingsUiPanel *panel = g_paste_gtk_settings_ui_panel_new ();

    priv->track_changes_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                    _("Track clipboard changes"),
                                                                                    g_paste_settings_get_track_changes (settings),
                                                                                    track_changes_callback,
                                                                                    (GPasteGtkResetCallback) g_paste_settings_reset_track_changes,
                                                                                    settings);
    priv->close_on_select_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                      _("Close UI on select"),
                                                                                      g_paste_settings_get_close_on_select (settings),
                                                                                      close_on_select_callback,
                                                                                      (GPasteGtkResetCallback) g_paste_settings_reset_close_on_select,
                                                                                      settings);
    priv->open_centered_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                    _("Open UI window centered"),
                                                                                    g_paste_settings_get_open_centered (settings),
                                                                                    open_centered_callback,
                                                                                    (GPasteGtkResetCallback) g_paste_settings_reset_open_centered,
                                                                                    settings);

    if (g_paste_util_has_gnome_shell ())
    {
        priv->extension_enabled_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                            _("Enable the gnome-shell extension"),
                                                                                            g_paste_settings_get_extension_enabled (settings),
                                                                                            extension_enabled_callback,
                                                                                            NULL,
                                                                                            settings);
        priv->track_extension_state_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                                _("Sync the daemon state with the extension's one"),
                                                                                                g_paste_settings_get_track_extension_state (settings),
                                                                                                track_extension_state_callback,
                                                                                                (GPasteGtkResetCallback) g_paste_settings_reset_track_extension_state,
                                                                                                settings);
    }

    g_paste_gtk_settings_ui_panel_add_separator (panel);
    priv->primary_to_history_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                         _("Primary selection affects history"),
                                                                                         g_paste_settings_get_primary_to_history (settings),
                                                                                         primary_to_history_callback,
                                                                                         (GPasteGtkResetCallback) g_paste_settings_reset_primary_to_history,
                                                                                         settings);
    priv->synchronize_clipboards_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                             _("Synchronize clipboard with primary selection"),
                                                                                             g_paste_settings_get_synchronize_clipboards (settings),
                                                                                             synchronize_clipboards_callback,
                                                                                             (GPasteGtkResetCallback) g_paste_settings_reset_synchronize_clipboards,
                                                                                             settings);
    g_paste_gtk_settings_ui_panel_add_separator (panel);
    priv->images_support_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                     _("Images support"),
                                                                                     g_paste_settings_get_images_support (settings),
                                                                                     images_support_callback,
                                                                                     (GPasteGtkResetCallback) g_paste_settings_reset_images_support,
                                                                                     settings);

    /* Ajout des options pour les prévisualisations d'images */
    priv->images_preview_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                     _("Image previews"),
                                                                                     g_paste_settings_get_images_preview (settings),
                                                                                     images_preview_callback,
                                                                                     (GPasteGtkResetCallback) g_paste_settings_reset_images_preview,
                                                                                     settings);

    priv->images_preview_size_button = g_paste_gtk_settings_ui_panel_add_range_setting (panel,
                                                                                     _("Preview size"),
                                                                                     (gdouble) g_paste_settings_get_images_preview_size (settings),
                                                                                     50.0,   /* min */
                                                                                     400.0,  /* max */
                                                                                     10.0,   /* step */
                                                                                     images_preview_size_callback,
                                                                                     (GPasteGtkResetCallback) g_paste_settings_reset_images_preview_size,
                                                                                     settings);
    g_paste_gtk_settings_ui_panel_add_separator (panel);

    priv->trim_items_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                  _("Trim items"),
                                                                                 g_paste_settings_get_trim_items (settings),
                                                                                 trim_items_callback,
                                                                                 (GPasteGtkResetCallback) g_paste_settings_reset_trim_items,
                                                                                 settings);
    priv->growing_lines_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                     _("Detect growing lines"),
                                                                                    g_paste_settings_get_growing_lines (settings),
                                                                                    growing_lines_callback,
                                                                                    (GPasteGtkResetCallback) g_paste_settings_reset_growing_lines,
                                                                                    settings);
    g_paste_gtk_settings_ui_panel_add_separator (panel);
    priv->save_history_switch = g_paste_gtk_settings_ui_panel_add_boolean_setting (panel,
                                                                                   _("Save history"),
                                                                                   g_paste_settings_get_save_history (settings),
                                                                                   save_history_callback,
                                                                                   (GPasteGtkResetCallback) g_paste_settings_reset_save_history,
                                                                                   settings);

    return panel;
}

UINT64_CALLBACK (element_size)
UINT64_CALLBACK (max_displayed_history_size)
UINT64_CALLBACK (max_history_size)
UINT64_CALLBACK (max_memory_usage)
UINT64_CALLBACK (max_text_item_size)
UINT64_CALLBACK (min_text_item_size)

static GPasteGtkSettingsUiPanel *
g_paste_gtk_settings_ui_stack_private_make_history_settings_panel (GPasteGtkSettingsUiStackPrivate *priv)
{
    GPasteSettings *settings = priv->settings;
    GPasteGtkSettingsUiPanel *panel = g_paste_gtk_settings_ui_panel_new ();

    priv->element_size_button = g_paste_gtk_settings_ui_panel_add_range_setting (panel,
                                                                                 _("Max element size when displaying"),
                                                                                 (gdouble) g_paste_settings_get_element_size (settings),
                                                                                 0, 511, 5,
                                                                                 element_size_callback,
                                                                                 (GPasteGtkResetCallback) g_paste_settings_reset_element_size,
                                                                                 settings);
    priv->max_displayed_history_size_button = g_paste_gtk_settings_ui_panel_add_range_setting (panel,
                                                                                               _("Max displayed history size"),
                                                                                               (gdouble) g_paste_settings_get_max_displayed_history_size (settings),
                                                                                               10, 255, 5,
                                                                                               max_displayed_history_size_callback,
                                                                                               (GPasteGtkResetCallback) g_paste_settings_reset_max_displayed_history_size,
                                                                                               settings);
    priv->max_history_size_button = g_paste_gtk_settings_ui_panel_add_range_setting (panel,
                                                                                     _("Max history size"),
                                                                                     (gdouble) g_paste_settings_get_max_history_size (settings),
                                                                                     100, 65535, 5,
                                                                                     max_history_size_callback,
                                                                                     (GPasteGtkResetCallback) g_paste_settings_reset_max_history_size,
                                                                                     settings);
    priv->max_memory_usage_button = g_paste_gtk_settings_ui_panel_add_range_setting (panel,
                                                                                     _("Max memory usage (MB)"),
                                                                                     (gdouble) g_paste_settings_get_max_memory_usage (settings),
                                                                                     5, 16383, 5,
                                                                                     max_memory_usage_callback,
                                                                                     (GPasteGtkResetCallback) g_paste_settings_reset_max_memory_usage,
                                                                                     settings);
    priv->max_text_item_size_button = g_paste_gtk_settings_ui_panel_add_range_setting (panel,
                                                                                       _("Max text item length"),
                                                                                       (gdouble) g_paste_settings_get_max_text_item_size (settings),
                                                                                       1, (gdouble) G_MAXUINT64, 1,
                                                                                       max_text_item_size_callback,
                                                                                       (GPasteGtkResetCallback) g_paste_settings_reset_max_text_item_size,
                                                                                       settings);
    priv->min_text_item_size_button = g_paste_gtk_settings_ui_panel_add_range_setting (panel,
                                                                                       _("Min text item length"),
                                                                                       (gdouble) g_paste_settings_get_min_text_item_size (settings),
                                                                                       1, 65535, 1,
                                                                                       min_text_item_size_callback,
                                                                                       (GPasteGtkResetCallback) g_paste_settings_reset_min_text_item_size,
                                                                                       settings);

    return panel;
}

STRING_CALLBACK (launch_ui)
STRING_CALLBACK (make_password)
STRING_CALLBACK (pop)
STRING_CALLBACK (show_history)
STRING_CALLBACK (sync_clipboard_to_primary)
STRING_CALLBACK (sync_primary_to_clipboard)
STRING_CALLBACK (upload)

static GPasteGtkSettingsUiPanel *
g_paste_gtk_settings_ui_stack_private_make_keybindings_panel (GPasteGtkSettingsUiStackPrivate *priv)
{
    GPasteSettings *settings = priv->settings;
    GPasteGtkSettingsUiPanel *panel = g_paste_gtk_settings_ui_panel_new ();

    /* translators: Keyboard shortcut to delete the active item from history */
    priv->pop_entry = g_paste_gtk_settings_ui_panel_add_text_setting (panel,
                                                                      _("Delete the active item from history"),
                                                                      g_paste_settings_get_pop (settings),
                                                                      pop_callback,
                                                                      (GPasteGtkResetCallback) g_paste_settings_reset_pop,
                                                                      settings);
    /* translators: Keyboard shortcut to launch the graphical tool */
    priv->launch_ui_entry = g_paste_gtk_settings_ui_panel_add_text_setting (panel,
                                                                            _("Launch the graphical tool"),
                                                                            g_paste_settings_get_launch_ui (settings),
                                                                            launch_ui_callback,
                                                                            (GPasteGtkResetCallback) g_paste_settings_reset_launch_ui,
                                                                            settings);
    /* translators: Keyboard shortcut to mark the active item as being a password */
    priv->make_password_entry = g_paste_gtk_settings_ui_panel_add_text_setting (panel,
                                                                                _("Mark the active item as being a password"),
                                                                                g_paste_settings_get_make_password (settings),
                                                                                make_password_callback,
                                                                                (GPasteGtkResetCallback) g_paste_settings_reset_make_password,
                                                                                settings);
    /* translators: Keyboard shortcut to display the history */
    priv->show_history_entry = g_paste_gtk_settings_ui_panel_add_text_setting (panel,
                                                                               _("Display the history"),
                                                                               g_paste_settings_get_show_history (settings),
                                                                               show_history_callback,
                                                                               (GPasteGtkResetCallback) g_paste_settings_reset_show_history,
                                                                               settings);
    /* translators: Keyboard shortcut to sync the clipboard to the primary selection */
    priv->sync_clipboard_to_primary_entry = g_paste_gtk_settings_ui_panel_add_text_setting (panel,
                                                                                            _("Sync the clipboard to the primary selection"),
                                                                                            g_paste_settings_get_sync_clipboard_to_primary (settings),
                                                                                            sync_clipboard_to_primary_callback,
                                                                                            (GPasteGtkResetCallback) g_paste_settings_reset_sync_clipboard_to_primary,
                                                                                            settings);
    /* translators: Keyboard shortcut to sync the primary selection to the clipboard */
    priv->sync_primary_to_clipboard_entry = g_paste_gtk_settings_ui_panel_add_text_setting (panel,
                                                                                            _("Sync the primary selection to the clipboard"),
                                                                                            g_paste_settings_get_sync_primary_to_clipboard (settings),
                                                                                            sync_primary_to_clipboard_callback,
                                                                                            (GPasteGtkResetCallback) g_paste_settings_reset_sync_primary_to_clipboard,
                                                                                            settings);
    /* translators: Keyboard shortcut to upload the active item from history to a pastebin service */
    priv->upload_entry = g_paste_gtk_settings_ui_panel_add_text_setting (panel,
                                                                         _("Upload the active item to a pastebin service"),
                                                                         g_paste_settings_get_upload (settings),
                                                                         upload_callback,
                                                                         (GPasteGtkResetCallback) g_paste_settings_reset_upload,
                                                                         settings);

    return panel;
}

/**
 * g_paste_gtk_settings_ui_stack_fill:
 * @self: a #GPasteGtkSettingsUiStack instance
 *
 * Fill the #GPasteGtkSettingsUiStack with default panels
 */
G_PASTE_VISIBLE void
g_paste_gtk_settings_ui_stack_fill (GPasteGtkSettingsUiStack *self)
{
    GPasteGtkSettingsUiStackPrivate *priv = g_paste_gtk_settings_ui_stack_get_instance_private (self);

    g_paste_gtk_settings_ui_stack_add_panel (self, "general",   _("General behaviour"),  g_paste_gtk_settings_ui_stack_private_make_behaviour_panel (priv));
    g_paste_gtk_settings_ui_stack_add_panel (self, "history",   _("History settings"),   g_paste_gtk_settings_ui_stack_private_make_history_settings_panel (priv));
    g_paste_gtk_settings_ui_stack_add_panel (self, "keyboard",  _("Keyboard shortcuts"), g_paste_gtk_settings_ui_stack_private_make_keybindings_panel (priv));
}

static void
g_paste_gtk_settings_ui_stack_settings_changed (GPasteSettings *settings,
                                                const gchar    *key,
                                                gpointer        user_data)
{
    GPasteGtkSettingsUiStackPrivate *priv = user_data;

    if (g_paste_str_equal (key, G_PASTE_CLOSE_ON_SELECT_SETTING))
        gtk_switch_set_active (GTK_SWITCH (priv->close_on_select_switch), g_paste_settings_get_close_on_select (settings));
    else if (g_paste_str_equal (key, G_PASTE_OPEN_CENTERED_SETTING))
        gtk_switch_set_active (GTK_SWITCH (priv->open_centered_switch), g_paste_settings_get_open_centered (settings));
    else if (g_paste_str_equal (key, G_PASTE_ELEMENT_SIZE_SETTING))
        gtk_spin_button_set_value (priv->element_size_button, g_paste_settings_get_element_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_GROWING_LINES_SETTING))
        gtk_switch_set_active (GTK_SWITCH (priv->growing_lines_switch), g_paste_settings_get_growing_lines (settings));
    else if (g_paste_str_equal (key, G_PASTE_IMAGES_SUPPORT_SETTING))
        gtk_switch_set_active (GTK_SWITCH (priv->images_support_switch), g_paste_settings_get_images_support (settings));
    else if (g_paste_str_equal (key, G_PASTE_IMAGES_PREVIEW_SETTING))
        gtk_switch_set_active (GTK_SWITCH (priv->images_preview_switch), g_paste_settings_get_images_preview (settings));
    else if (g_paste_str_equal (key, G_PASTE_IMAGES_PREVIEW_SIZE_SETTING))
        gtk_spin_button_set_value (priv->images_preview_size_button, (gdouble) g_paste_settings_get_images_preview_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_LAUNCH_UI_SETTING))
        gtk_entry_set_text (priv->launch_ui_entry, g_paste_settings_get_launch_ui (settings));
    else if (g_paste_str_equal (key, G_PASTE_MAKE_PASSWORD_SETTING))
        gtk_entry_set_text (priv->make_password_entry, g_paste_settings_get_make_password (settings));
    else if (g_paste_str_equal (key, G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING))
        gtk_spin_button_set_value (priv->max_displayed_history_size_button, g_paste_settings_get_max_displayed_history_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_MAX_HISTORY_SIZE_SETTING))
        gtk_spin_button_set_value (priv->max_history_size_button, g_paste_settings_get_max_history_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_MAX_MEMORY_USAGE_SETTING))
        gtk_spin_button_set_value (priv->max_memory_usage_button, g_paste_settings_get_max_memory_usage (settings));
    else if (g_paste_str_equal (key, G_PASTE_MAX_TEXT_ITEM_SIZE_SETTING))
        gtk_spin_button_set_value (priv->max_text_item_size_button, g_paste_settings_get_max_text_item_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_MIN_TEXT_ITEM_SIZE_SETTING))
        gtk_spin_button_set_value (priv->min_text_item_size_button, g_paste_settings_get_min_text_item_size (settings));
    else if (g_paste_str_equal (key, G_PASTE_POP_SETTING))
        gtk_entry_set_text (priv->pop_entry, g_paste_settings_get_pop (settings));
    else if (g_paste_str_equal (key, G_PASTE_PRIMARY_TO_HISTORY_SETTING ))
        gtk_switch_set_active (GTK_SWITCH (priv->primary_to_history_switch), g_paste_settings_get_primary_to_history (settings));
    else if (g_paste_str_equal (key, G_PASTE_SAVE_HISTORY_SETTING))
        gtk_switch_set_active (GTK_SWITCH (priv->save_history_switch), g_paste_settings_get_save_history (settings));
    else if (g_paste_str_equal (key, G_PASTE_SHOW_HISTORY_SETTING))
        gtk_entry_set_text (priv->show_history_entry, g_paste_settings_get_show_history (settings));
    else if (g_paste_str_equal (key, G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING))
        gtk_entry_set_text (priv->sync_clipboard_to_primary_entry, g_paste_settings_get_sync_clipboard_to_primary (settings));
    else if (g_paste_str_equal (key, G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING))
        gtk_entry_set_text (priv->sync_primary_to_clipboard_entry, g_paste_settings_get_sync_primary_to_clipboard (settings));
    else if (g_paste_str_equal (key, G_PASTE_UPLOAD_SETTING))
        gtk_entry_set_text (priv->upload_entry, g_paste_settings_get_upload (settings));
    else if (g_paste_str_equal (key, G_PASTE_SYNCHRONIZE_CLIPBOARDS_SETTING))
        gtk_switch_set_active (GTK_SWITCH (priv->synchronize_clipboards_switch), g_paste_settings_get_synchronize_clipboards (settings));
    else if (g_paste_str_equal (key, G_PASTE_TRACK_CHANGES_SETTING))
        gtk_switch_set_active (GTK_SWITCH (priv->track_changes_switch), g_paste_settings_get_track_changes (settings));
    else if (g_paste_str_equal (key, G_PASTE_TRIM_ITEMS_SETTING))
        gtk_switch_set_active (GTK_SWITCH (priv->trim_items_switch), g_paste_settings_get_trim_items (settings));
    else if (g_paste_util_has_gnome_shell ())
    {
        if (g_paste_str_equal (key, G_PASTE_EXTENSION_ENABLED_SETTING))
            gtk_switch_set_active (GTK_SWITCH (priv->extension_enabled_switch), g_paste_settings_get_extension_enabled (settings));
        else if (g_paste_str_equal (key, G_PASTE_TRACK_EXTENSION_STATE_SETTING))
            gtk_switch_set_active (GTK_SWITCH (priv->track_extension_state_switch), g_paste_settings_get_track_extension_state (settings));
    }
}

static void
g_paste_gtk_settings_ui_stack_dispose (GObject *object)
{
    GPasteGtkSettingsUiStackPrivate *priv = g_paste_gtk_settings_ui_stack_get_instance_private (G_PASTE_GTK_SETTINGS_UI_STACK (object));

    if (priv->settings) /* first dispose call */
    {
        g_signal_handler_disconnect (priv->settings, priv->c_signals[C_SETTINGS]);
        g_clear_object (&priv->settings);
    }

    G_OBJECT_CLASS (g_paste_gtk_settings_ui_stack_parent_class)->dispose (object);
}

static void
g_paste_gtk_settings_ui_stack_class_init (GPasteGtkSettingsUiStackClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_gtk_settings_ui_stack_dispose;
}

static void
g_paste_gtk_settings_ui_stack_init (GPasteGtkSettingsUiStack *self)
{
    GPasteGtkSettingsUiStackPrivate *priv = g_paste_gtk_settings_ui_stack_get_instance_private (self);

    priv->settings = g_paste_settings_new ();
    priv->c_signals[C_SETTINGS] = g_signal_connect (priv->settings,
                                                    "changed",
                                                    G_CALLBACK (g_paste_gtk_settings_ui_stack_settings_changed),
                                                    priv);
}

/**
 * g_paste_gtk_settings_ui_stack_new:
 *
 * Create a new instance of #GPasteGtkSettingsUiStack
 *
 * Returns: (nullable): a newly allocated #GPasteGtkSettingsUiStack
 *                      free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGtkSettingsUiStack *
g_paste_gtk_settings_ui_stack_new (void)
{
    return G_PASTE_GTK_SETTINGS_UI_STACK (gtk_widget_new (G_PASTE_TYPE_GTK_SETTINGS_UI_STACK,
                                                          "margin",      12,
                                                          "homogeneous", TRUE,
                                                          NULL));
}
