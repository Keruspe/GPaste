/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gsettings-keys.h>
#include <gpaste-util.h>

#define G_SETTINGS_ENABLE_BACKEND 1
#include <gio/gsettingsbackend.h>

struct _GPasteSettings
{
    GObject parent_instance;
};

enum
{
    C_CHANGED,
    C_SHELL_CHANGED,

    C_LAST_SIGNAL
};

typedef struct
{
    GSettings *settings;
    GSettings *shell_settings;

    gboolean   close_on_select;
    guint64    element_size;
    gboolean   empty_history_confirmation;
    gboolean   growing_lines;
    gchar     *history_name;
    gboolean   images_support;
    gchar     *launch_ui;
    gchar     *make_password;
    guint64    max_displayed_history_size;
    guint64    max_history_size;
    guint64    max_memory_usage;
    guint64    max_text_item_size;
    guint64    min_text_item_size;
    gchar     *pop;
    gboolean   primary_to_history;
    gboolean   rich_text_support;
    gboolean   save_history;
    gchar     *show_history;
    gchar     *sync_clipboard_to_primary;
    gchar     *sync_primary_to_clipboard;
    gboolean   synchronize_clipboards;
    gboolean   track_changes;
    gboolean   track_extension_state;
    gboolean   trim_items;
    gchar     *upload;

    gboolean   extension_enabled;

    guint64    c_signals[C_LAST_SIGNAL];
} GPasteSettingsPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (Settings, settings, G_TYPE_OBJECT)

enum
{
    CHANGED,
    REBIND,
    TRACK,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

#define SETTING(name, key, type, setting_type, fail, guards, clear_func, dup_func)                     \
    G_PASTE_VISIBLE type                                                                               \
    g_paste_settings_get_##name (const GPasteSettings *self)                                           \
    {                                                                                                  \
        g_return_val_if_fail (_G_PASTE_IS_SETTINGS ((gpointer) self), fail);                           \
        const GPasteSettingsPrivate *priv = _g_paste_settings_get_instance_private (self);             \
        return priv->name;                                                                             \
    }                                                                                                  \
    G_PASTE_VISIBLE void                                                                               \
    g_paste_settings_reset_##name (GPasteSettings *self)                                               \
    {                                                                                                  \
        g_return_if_fail (_G_PASTE_IS_SETTINGS (self));                                                \
        const GPasteSettingsPrivate *priv = _g_paste_settings_get_instance_private (self);             \
        g_settings_reset (priv->settings, G_PASTE_##key##_SETTING);                                    \
    }                                                                                                  \
    static void                                                                                        \
    g_paste_settings_private_set_##name##_from_dconf (GPasteSettingsPrivate *priv)                     \
    {                                                                                                  \
        priv->name = g_settings_get_##setting_type (priv->settings, G_PASTE_##key##_SETTING);          \
    }                                                                                                  \
    G_PASTE_VISIBLE void                                                                               \
    g_paste_settings_set_##name (GPasteSettings *self,                                                 \
                                 type            value)                                                \
    {                                                                                                  \
        g_return_if_fail (_G_PASTE_IS_SETTINGS (self));                                                \
        guards                                                                                         \
        GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private (self);                    \
        clear_func                                                                                     \
        priv->name = dup_func (value);                                                                 \
        g_settings_set_##setting_type (priv->settings, G_PASTE_##key##_SETTING, value);                \
    }

#define TRIVIAL_SETTING(name, key, type, setting_type, fail) \
    SETTING (name, key, type, setting_type, fail, {}, {},)

#define BOOLEAN_SETTING(name, key) TRIVIAL_SETTING (name, key, gboolean, boolean, FALSE)
#define UNSIGNED_SETTING(name, key) TRIVIAL_SETTING (name, key, guint64, uint64, 0)

#define STRING_SETTING(name, key) SETTING (name, key, const gchar *, string, NULL,                \
                                           g_return_if_fail (value);                              \
                                           g_return_if_fail (g_utf8_validate (value, -1, NULL));, \
                                           g_free (priv->name);, g_strdup)

#define NEW_SIGNAL_FULL(name, type, MTYPE, arg_type) \
    g_signal_new (name,                              \
                  G_PASTE_TYPE_SETTINGS,             \
                  type,                              \
                  0, /* class offset */              \
                  NULL, /* accumulator */            \
                  NULL, /* accumulator data */       \
                  g_cclosure_marshal_VOID__##MTYPE,  \
                  G_TYPE_NONE,                       \
                  1, /* number of params */          \
                  G_TYPE_##arg_type)
#define NEW_SIGNAL(name, arg_type) NEW_SIGNAL_FULL (name, G_SIGNAL_RUN_LAST, arg_type, arg_type)
#define NEW_SIGNAL_DETAILED(name, arg_type) NEW_SIGNAL_FULL (name, G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, arg_type, arg_type)
#define NEW_SIGNAL_DETAILED_STATIC(name, arg_type) NEW_SIGNAL_FULL (name, G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, arg_type, arg_type | G_SIGNAL_TYPE_STATIC_SCOPE)

/**
 * g_paste_settings_get_close_on_select:
 * @self: a #GPasteSettings instance
 *
 * Get the "close-on-select" setting
 *
 * Returns: the value of the "close-on-select" setting
 */
/**
 * g_paste_settings_reset_close_on_select:
 * @self: a #GPasteSettings instance
 *
 * Reset the "close-on-select" setting
 */
/**
 * g_paste_settings_set_close_on_select:
 * @self: a #GPasteSettings instance
 * @value: the new history name
 *
 * Change the "close-on-select" setting
 */
BOOLEAN_SETTING (close_on_select, CLOSE_ON_SELECT)

/**
 * g_paste_settings_get_element_size:
 * @self: a #GPasteSettings instance
 *
 * Get the "element-size" setting
 *
 * Returns: the value of the "element-size" setting
 */
/**
 * g_paste_settings_reset_element_size:
 * @self: a #GPasteSettings instance
 *
 * Reset the "element-size" setting
 */
/**
 * g_paste_settings_set_element_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum displayed size of an item
 *
 * Change the "element-size" setting
 */
UNSIGNED_SETTING (element_size, ELEMENT_SIZE)

/**
 * g_paste_settings_get_empty_history_confirmation:
 * @self: a #GPasteSettings instance
 *
 * Get the "empty-history-confirmation" setting
 *
 * Returns: the value of the "empty-history-confirmation" setting
 */
/**
 * g_paste_settings_reset_empty_history_confirmation:
 * @self: a #GPasteSettings instance
 *
 * Reset the "empty-history-confirmation" setting
 */
/**
 * g_paste_settings_set_empty_history_confirmation:
 * @self: a #GPasteSettings instance
 * @value: whether to prompt for confirmation when emptying an history
 *
 * Change the "empty-history-confirmation" setting
 */
BOOLEAN_SETTING (empty_history_confirmation, EMPTY_HISTORY_CONFIRMATION)

/**
 * g_paste_settings_get_growing_lines:
 * @self: a #GPasteSettings instance
 *
 * Get the "growing-lines" setting
 *
 * Returns: the value of the "growing-lines" setting
 */
/**
 * g_paste_settings_reset_growing_lines:
 * @self: a #GPasteSettings instance
 *
 * Reset the "growing-lines" setting
 */
/**
 * g_paste_settings_set_growing_lines:
 * @self: a #GPasteSettings instance
 * @value: whether to detect or not growing lines
 *
 * Change the "growing-lines" setting
 */
BOOLEAN_SETTING (growing_lines, GROWING_LINES)

/**
 * g_paste_settings_get_history_name:
 * @self: a #GPasteSettings instance
 *
 * Get the "history-name" setting
 *
 * Returns: the value of the "history-name" setting
 */
/**
 * g_paste_settings_reset_history_name:
 * @self: a #GPasteSettings instance
 *
 * Reset the "history-name" setting
 */
/**
 * g_paste_settings_set_history_name:
 * @self: a #GPasteSettings instance
 * @value: the new history name
 *
 * Change the "history-name" setting
 */
STRING_SETTING (history_name, HISTORY_NAME)

/**
 * g_paste_settings_get_images_support:
 * @self: a #GPasteSettings instance
 *
 * Get the "images-support" setting
 *
 * Returns: the value of the "images-support" setting
 */
/**
 * g_paste_settings_reset_images_support:
 * @self: a #GPasteSettings instance
 *
 * Reset the "images-support" setting
 */
/**
 * g_paste_settings_set_images_support:
 * @self: a #GPasteSettings instance
 * @value: the new history name
 *
 * Change the "images-support" setting
 */
BOOLEAN_SETTING (images_support, IMAGES_SUPPORT)

/**
 * g_paste_settings_get_launch_ui:
 * @self: a #GPasteSettings instance
 *
 * Get the "launch-ui" setting
 *
 * Returns: the value of the "launch-ui" setting
 */
/**
 * g_paste_settings_reset_launch_ui:
 * @self: a #GPasteSettings instance
 *
 * Reset the "launch-ui" setting
 */
/**
 * g_paste_settings_set_launch_ui:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "launch-ui" setting
 */
STRING_SETTING (launch_ui, LAUNCH_UI)

/**
 * g_paste_settings_get_make_password:
 * @self: a #GPasteSettings instance
 *
 * Get the "make-password" setting
 *
 * Returns: the value of the "make-password" setting
 */
/**
 * g_paste_settings_reset_make_password:
 * @self: a #GPasteSettings instance
 *
 * Reset the "make-password" setting
 */
/**
 * g_paste_settings_set_make_password:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "make-password" setting
 */
STRING_SETTING (make_password, MAKE_PASSWORD)

/**
 * g_paste_settings_get_max_displayed_history_size:
 * @self: a #GPasteSettings instance
 *
 * Get the "max-displayed-history-size" setting
 *
 * Returns: the value of the "max-displayed-history-size" setting
 */
/**
 * g_paste_settings_reset_max_displayed_history_size:
 * @self: a #GPasteSettings instance
 *
 * Reset the "max-displayed-history-size" setting
 */
/**
 * g_paste_settings_set_max_displayed_history_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum number of items to display
 *
 * Change the "max-displayed-history-size" setting
 */
UNSIGNED_SETTING (max_displayed_history_size, MAX_DISPLAYED_HISTORY_SIZE)

/**
 * g_paste_settings_get_max_history_size:
 * @self: a #GPasteSettings instance
 *
 * Get the "max-history-size" setting
 *
 * Returns: the value of the "max-history-size" setting
 */
/**
 * g_paste_settings_reset_max_history_size:
 * @self: a #GPasteSettings instance
 *
 * Reset the "max-history-size" setting
 */
/**
 * g_paste_settings_set_max_history_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum number of items the history can contain
 *
 * Change the "max-history-size" setting
 */
UNSIGNED_SETTING (max_history_size, MAX_HISTORY_SIZE)

/**
 * g_paste_settings_get_max_memory_usage:
 * @self: a #GPasteSettings instance
 *
 * Get the "max-memory-usage" setting
 *
 * Returns: the value of the "max-memory-usage" setting
 */
/**
 * g_paste_settings_reset_max_memory_usage:
 * @self: a #GPasteSettings instance
 *
 * Reset the "max-memory-usage" setting
 */
/**
 * g_paste_settings_set_max_memory_usage:
 * @self: a #GPasteSettings instance
 * @value: the maximum amount of memory we can use
 *
 * Change the "max-memory-usage" setting
 */
UNSIGNED_SETTING (max_memory_usage, MAX_MEMORY_USAGE)

/**
 * g_paste_settings_get_max_text_item_size:
 * @self: a #GPasteSettings instance
 *
 * Get the "max-text-item-size" setting
 *
 * Returns: the value of the "max-text-item-size" setting
 */
/**
 * g_paste_settings_reset_max_text_item_size:
 * @self: a #GPasteSettings instance
 *
 * Reset the "max-text-item-size" setting
 */
/**
 * g_paste_settings_set_max_text_item_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum size for a textual item to be handled
 *
 * Change the "max-text-item-size" setting
 */
UNSIGNED_SETTING (max_text_item_size, MAX_TEXT_ITEM_SIZE)

/**
 * g_paste_settings_get_min_text_item_size:
 * @self: a #GPasteSettings instance
 *
 * Get the "min-text-item-size" setting
 *
 * Returns: the value of the "min-text-item-size" setting
 */
/**
 * g_paste_settings_reset_min_text_item_size:
 * @self: a #GPasteSettings instance
 *
 * Reset the "min-text-item-size" setting
 */
/**
 * g_paste_settings_set_min_text_item_size:
 * @self: a #GPasteSettings instance
 * @value: the minimum size for a textual item to be handled
 *
 * Change the "min-text-item-size" setting
 */
UNSIGNED_SETTING (min_text_item_size, MIN_TEXT_ITEM_SIZE)

/**
 * g_paste_settings_get_pop:
 * @self: a #GPasteSettings instance
 *
 * Get the "pop" setting
 *
 * Returns: the value of the "pop" setting
 */
/**
 * g_paste_settings_reset_pop:
 * @self: a #GPasteSettings instance
 *
 * Reset the "pop" setting
 */
/**
 * g_paste_settings_set_pop:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "pop" setting
 */
STRING_SETTING (pop, POP)

/**
 * g_paste_settings_get_primary_to_history:
 * @self: a #GPasteSettings instance
 *
 * Get the "primary-to-history" setting
 *
 * Returns: the value of the "primary-to-history" setting
 */
/**
 * g_paste_settings_reset_primary_to_history:
 * @self: a #GPasteSettings instance
 *
 * Reset the "primary-to-history" setting
 */
/**
 * g_paste_settings_set_primary_to_history:
 * @self: a #GPasteSettings instance
 * @value: whether to track or not the primary selection changes as clipboard ones
 *
 * Change the "primary-to-history" setting
 */
BOOLEAN_SETTING (primary_to_history, PRIMARY_TO_HISTORY)

/**
 * g_paste_settings_get_rich_text_support:
 * @self: a #GPasteSettings instance
 *
 * Get the "rich-text-support" setting
 *
 * Returns: the value of the "rich-text-support" setting
 */
/**
 * g_paste_settings_reset_rich_text_support:
 * @self: a #GPasteSettings instance
 *
 * Reset the "rich-text-support" setting
 */
/**
 * g_paste_settings_set_rich_text_support:
 * @self: a #GPasteSettings instance
 * @value: the new history name
 *
 * Change the "rich-text-support" setting
 */
BOOLEAN_SETTING (rich_text_support, RICH_TEXT_SUPPORT)

/**
 * g_paste_settings_get_save_history:
 * @self: a #GPasteSettings instance
 *
 * Get the "save-history" setting
 *
 * Returns: the value of the "save-history" setting
 */
/**
 * g_paste_settings_reset_save_history:
 * @self: a #GPasteSettings instance
 *
 * Reset the "save-history" setting
 */
/**
 * g_paste_settings_set_save_history:
 * @self: a #GPasteSettings instance
 * @value: whether to save or not the history
 *
 * Change the "save-history" setting
 */
BOOLEAN_SETTING (save_history, SAVE_HISTORY)

/**
 * g_paste_settings_get_show_history:
 * @self: a #GPasteSettings instance
 *
 * Get the "show-history" setting
 *
 * Returns: the value of the "show-history" setting
 */
/**
 * g_paste_settings_reset_show_history:
 * @self: a #GPasteSettings instance
 *
 * Reset the "show-history" setting
 */
/**
 * g_paste_settings_set_show_history:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "show-history" setting
 */
STRING_SETTING (show_history, SHOW_HISTORY)

/**
 * g_paste_settings_get_sync_clipboard_to_primary:
 * @self: a #GPasteSettings instance
 *
 * Get the "sync-clipboard-to-primary" setting
 *
 * Returns: the value of the "sync-clipboard-to-primary" setting
 */
/**
 * g_paste_settings_reset_sync_clipboard_to_primary:
 * @self: a #GPasteSettings instance
 *
 * Reset the "sync-clipboard-to-primary" setting
 */
/**
 * g_paste_settings_set_sync_clipboard_to_primary:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "sync-clipboard-to-primary" setting
 */
STRING_SETTING (sync_clipboard_to_primary, SYNC_CLIPBOARD_TO_PRIMARY)

/**
 * g_paste_settings_get_sync_primary_to_clipboard:
 * @self: a #GPasteSettings instance
 *
 * Get the "sync-primary-to-clipboard" setting
 *
 * Returns: the value of the "sync-primary-to-clipboard" setting
 */
/**
 * g_paste_settings_reset_sync_primary_to_clipboard:
 * @self: a #GPasteSettings instance
 *
 * Reset the "sync-primary-to-clipboard" setting
 */
/**
 * g_paste_settings_set_sync_primary_to_clipboard:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "sync-primary-to-clipboard" setting
 */
STRING_SETTING (sync_primary_to_clipboard, SYNC_PRIMARY_TO_CLIPBOARD)

/**
 * g_paste_settings_get_synchronize_clipboards:
 * @self: a #GPasteSettings instance
 *
 * Get the "synchronize-clipboards" setting
 *
 * Returns: the value of the "synchronize-clipboards" setting
 */
/**
 * g_paste_settings_reset_synchronize_clipboards:
 * @self: a #GPasteSettings instance
 *
 * Reset the "synchronize-clipboards" setting
 */
/**
 * g_paste_settings_set_synchronize_clipboards:
 * @self: a #GPasteSettings instance
 * @value: whether to synchronize the clipboard and the primary selection or not
 *
 * Change the "synchronize-clipboards" setting
 */
BOOLEAN_SETTING (synchronize_clipboards, SYNCHRONIZE_CLIPBOARDS)

/**
 * g_paste_settings_get_track_changes:
 * @self: a #GPasteSettings instance
 *
 * Get the "track-changes" setting
 *
 * Returns: the value of the "track-changes" setting
 */
/**
 * g_paste_settings_reset_track_changes:
 * @self: a #GPasteSettings instance
 *
 * Reset the "track-changes" setting
 */
/**
 * g_paste_settings_set_track_changes:
 * @self: a #GPasteSettings instance
 * @value: whether to track or not the clipboard changes
 *
 * Change the "track-changes" setting
 */
BOOLEAN_SETTING (track_changes, TRACK_CHANGES)

/**
 * g_paste_settings_get_track_extension_state:
 * @self: a #GPasteSettings instance
 *
 * Get the "track-extension-state" setting
 *
 * Returns: the value of the "track-extension-state" setting
 */
/**
 * g_paste_settings_reset_track_extension_state:
 * @self: a #GPasteSettings instance
 *
 * Reset the "track-extension-state" setting
 */
/**
 * g_paste_settings_set_track_extension_state:
 * @self: a #GPasteSettings instance
 * @value: whether to stop tracking or not the clipboard changes when an applet exits
 *
 * Change the "track-extension-state" setting
 */
BOOLEAN_SETTING (track_extension_state, TRACK_EXTENSION_STATE)

/**
 * g_paste_settings_get_trim_items:
 * @self: a #GPasteSettings instance
 *
 * Get the "trim-items" setting
 *
 * Returns: the value of the "trim-items" setting
 */
/**
 * g_paste_settings_reset_trim_items:
 * @self: a #GPasteSettings instance
 *
 * Reset the "trim-items" setting
 */
/**
 * g_paste_settings_set_trim_items:
 * @self: a #GPasteSettings instance
 * @value: whether to trim or not textual items
 *
 * Change the "trim-items" setting
 */
BOOLEAN_SETTING (trim_items, TRIM_ITEMS)

/**
 * g_paste_settings_get_upload:
 * @self: a #GPasteSettings instance
 *
 * Get the "upload" setting
 *
 * Returns: the value of the "upload" setting
 */
/**
 * g_paste_settings_reset_upload:
 * @self: a #GPasteSettings instance
 *
 * Reset the "upload" setting
 */
/**
 * g_paste_settings_set_upload:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "upload" setting
 */
STRING_SETTING (upload, UPLOAD)

/**
 * g_paste_settings_get_extension_enabled:
 * @self: a #GPasteSettings instance
 *
 * Get the "extension-enabled" special setting
 *
 * Returns: Whether the gnome-shell extension is enabled or not
 */
G_PASTE_VISIBLE gboolean
g_paste_settings_get_extension_enabled (const GPasteSettings *self)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS ((gpointer) self), FALSE);
    const GPasteSettingsPrivate *priv = _g_paste_settings_get_instance_private (self);
    return priv->extension_enabled;
}

static inline gchar **
g_paste_settings_private_get_enabled_extensions (GPasteSettingsPrivate *priv)
{
    return (priv->shell_settings) ? g_settings_get_strv (priv->shell_settings, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING) : NULL;
}

static void
g_paste_settings_private_set_extension_enabled_from_dconf (GPasteSettingsPrivate *priv)
{
    g_auto (GStrv) extensions = g_paste_settings_private_get_enabled_extensions (priv);
    for (GStrv e = extensions; *e; ++e)
    {
        if (g_paste_str_equal (*e, G_PASTE_EXTENSION_NAME))
        {
            priv->extension_enabled = TRUE;
            return;
        }
    }
    priv->extension_enabled = FALSE;
}

/**
 * g_paste_settings_set_extension_enabled:
 * @self: a #GPasteSettings instance
 * @value: whether to enable or not the gnome-shell extension
 *
 * Change the "extension-enabled" special setting
 */
G_PASTE_VISIBLE void
g_paste_settings_set_extension_enabled (GPasteSettings *self,
                                        gboolean        value)
{
    g_return_if_fail (_G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private (self);
    g_auto (GStrv) extensions = NULL;

    if (!priv->shell_settings || (value == priv->extension_enabled))
        return;

    extensions = g_paste_settings_private_get_enabled_extensions (priv);
    guint64 nb = g_strv_length (extensions);
    if (value)
    {
        extensions = g_realloc (extensions, (nb + 2) * sizeof (gchar *));
        extensions[nb] = g_strdup (G_PASTE_EXTENSION_NAME);
        extensions[nb+1] = NULL;
    }
    else
    {
        gboolean found = FALSE;
        for (guint64 i = 0; i < nb; ++i)
        {
            if (!found && g_paste_str_equal (extensions[i], G_PASTE_EXTENSION_NAME))
            {
                found = TRUE;
                g_free (extensions[i]);
            }
            if (found)
                extensions[i] = extensions[i+1];
        } 
    }

    priv->extension_enabled = value;
    g_settings_set_strv (priv->shell_settings, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING, (const gchar * const *) extensions);
}

static void
g_paste_settings_shell_settings_changed (GSettings   *settings G_GNUC_UNUSED,
                                         const gchar *key      G_GNUC_UNUSED,
                                         gpointer     user_data)
{
    GPasteSettings *self = G_PASTE_SETTINGS (user_data);
    GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private (self);

    g_paste_settings_private_set_extension_enabled_from_dconf (priv);

    /* Forward the signal */
    g_signal_emit (self,
                   signals[CHANGED],
                   g_quark_from_string (G_PASTE_EXTENSION_ENABLED_SETTING),
                   G_PASTE_EXTENSION_ENABLED_SETTING,
                   NULL);
}

static void
g_paste_settings_rebind (GPasteSettings *self,
                         const gchar    *key)
{
    g_signal_emit (self,
                   signals[REBIND],
                   g_quark_from_string (key),
                   NULL);
}

static void
g_paste_settings_settings_changed (GSettings   *settings G_GNUC_UNUSED,
                                   const gchar *key,
                                   gpointer     user_data)
{
    GPasteSettings *self = G_PASTE_SETTINGS (user_data);
    GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private (self);

    if (g_paste_str_equal (key, G_PASTE_CLOSE_ON_SELECT_SETTING))
        g_paste_settings_private_set_close_on_select_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_ELEMENT_SIZE_SETTING))
        g_paste_settings_private_set_element_size_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_EMPTY_HISTORY_CONFIRMATION_SETTING))
        g_paste_settings_private_set_empty_history_confirmation_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_GROWING_LINES_SETTING))
        g_paste_settings_private_set_growing_lines_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_HISTORY_NAME_SETTING))
        g_paste_settings_private_set_history_name_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_IMAGES_SUPPORT_SETTING))
        g_paste_settings_private_set_images_support_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_LAUNCH_UI_SETTING))
    {
        g_paste_settings_private_set_launch_ui_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_LAUNCH_UI_SETTING);
    }
    else if (g_paste_str_equal (key, G_PASTE_MAKE_PASSWORD_SETTING))
    {
        g_paste_settings_private_set_make_password_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_MAKE_PASSWORD_SETTING);
    }
    else if (g_paste_str_equal (key, G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING))
        g_paste_settings_private_set_max_displayed_history_size_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_MAX_HISTORY_SIZE_SETTING))
        g_paste_settings_private_set_max_history_size_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_MAX_MEMORY_USAGE_SETTING))
        g_paste_settings_private_set_max_memory_usage_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_MAX_TEXT_ITEM_SIZE_SETTING))
        g_paste_settings_private_set_max_text_item_size_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_MIN_TEXT_ITEM_SIZE_SETTING))
        g_paste_settings_private_set_min_text_item_size_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_POP_SETTING))
    {
        g_paste_settings_private_set_pop_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_POP_SETTING);
    }
    else if (g_paste_str_equal (key, G_PASTE_PRIMARY_TO_HISTORY_SETTING ))
        g_paste_settings_private_set_primary_to_history_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_RICH_TEXT_SUPPORT_SETTING))
        g_paste_settings_private_set_rich_text_support_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_SAVE_HISTORY_SETTING))
        g_paste_settings_private_set_save_history_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_SHOW_HISTORY_SETTING))
    {
        g_paste_settings_private_set_show_history_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_SHOW_HISTORY_SETTING);
    }
    else if (g_paste_str_equal (key, G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING))
    {
        g_paste_settings_private_set_sync_clipboard_to_primary_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING);
    }
    else if (g_paste_str_equal (key, G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING))
    {
        g_paste_settings_private_set_sync_primary_to_clipboard_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING);
    }
    else if (g_paste_str_equal (key, G_PASTE_SYNCHRONIZE_CLIPBOARDS_SETTING))
        g_paste_settings_private_set_synchronize_clipboards_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_TRACK_CHANGES_SETTING))
    {
        g_paste_settings_private_set_track_changes_from_dconf (priv);
        g_signal_emit (self,
                       signals[TRACK],
                       0, /* detail */
                       priv->track_changes,
                       NULL);
    }
    else if (g_paste_str_equal (key, G_PASTE_TRACK_EXTENSION_STATE_SETTING))
        g_paste_settings_private_set_track_extension_state_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_TRIM_ITEMS_SETTING))
        g_paste_settings_private_set_trim_items_from_dconf (priv);
    else if (g_paste_str_equal (key, G_PASTE_UPLOAD_SETTING))
    {
        g_paste_settings_private_set_upload_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_UPLOAD_SETTING);
    }

    /* Forward the signal */
    g_signal_emit (self,
                   signals[CHANGED],
                   g_quark_from_string (key),
                   key,
                   NULL);
}

static void
g_paste_settings_dispose (GObject *object)
{
    GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private (G_PASTE_SETTINGS (object));
    GSettings *settings = priv->settings;
    GSettings *shell_settings = priv->shell_settings;

    if (settings)
    {
        g_signal_handler_disconnect (settings, priv->c_signals[C_CHANGED]);
        g_clear_object (&priv->settings);
    }

    if (shell_settings)
    {
        g_signal_handler_disconnect (shell_settings, priv->c_signals[C_SHELL_CHANGED]);
        g_clear_object (&priv->shell_settings);
    }

    G_OBJECT_CLASS (g_paste_settings_parent_class)->dispose (object);
}

static void
g_paste_settings_finalize (GObject *object)
{
    const GPasteSettingsPrivate *priv = _g_paste_settings_get_instance_private (G_PASTE_SETTINGS (object));

    g_free (priv->history_name);
    g_free (priv->launch_ui);
    g_free (priv->make_password);
    g_free (priv->pop);
    g_free (priv->show_history);
    g_free (priv->sync_clipboard_to_primary);
    g_free (priv->sync_primary_to_clipboard);
    g_free (priv->upload);

    G_OBJECT_CLASS (g_paste_settings_parent_class)->finalize (object);
}

static void
g_paste_settings_class_init (GPasteSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_settings_dispose;
    object_class->finalize = g_paste_settings_finalize;

    /**
     * GPasteSettings::changed:
     * @settings: the object on which the signal was emitted
     * @key: the name of the key that changed
     *
     * The "changed" signal is emitted when a key has potentially changed.
     * You should call one of the g_paste_settings_get() calls to check the new
     * value.
     *
     * This signal supports detailed connections.  You can connect to the
     * detailed signal "changed::x" in order to only receive callbacks
     * when key "x" changes.
     */
    signals[CHANGED] = NEW_SIGNAL_DETAILED_STATIC ("changed", STRING);

    /**
     * GPasteSettings::rebind:
     * @settings: the object on which the signal was emitted
     * @key: the name of the key that changed
     *
     * The "rebind" signal is emitted when a key has potentially changed.
     * You should call one of the g_paste_settings_get() calls to check the new
     * value.
     *
     * This signal supports detailed connections.  You can connect to the
     * detailed signal "rebind::x" in order to only receive callbacks
     * when key "x" changes.
     */
    signals[REBIND]  = NEW_SIGNAL_DETAILED        ("rebind" , STRING);

    /**
     * GPasteSettings::track:
     * @settings: the object on which the signal was emitted
     * @tracking_state: whether we're now tracking or not
     *
     * The "track" signal is emitted when the daemon starts or stops tracking
     * clipboard changes
     */
    signals[TRACK]   = NEW_SIGNAL                 ("track"  , BOOLEAN);
}

static GSettings *
create_g_settings (void)
{
    g_autofree gchar *config_file_path = g_build_filename (g_get_user_config_dir (), PACKAGE, "settings", NULL);
    g_autoptr (GFile) config_file = g_file_new_for_path (config_file_path);

    if (g_file_query_exists (config_file, NULL /* cancellable */))
    {
        g_autoptr (GSettingsBackend) backend = g_keyfile_settings_backend_new (config_file_path, G_PASTE_SETTINGS_PATH, PACKAGE_NAME);

        return g_settings_new_with_backend (G_PASTE_SETTINGS_NAME, backend);
    }
    else
    {
        return g_settings_new (G_PASTE_SETTINGS_NAME);
    }
}

static void
g_paste_settings_init (GPasteSettings *self)
{
    GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private (self);
    GSettings *settings = priv->settings = create_g_settings ();

    priv->history_name = NULL;
    priv->launch_ui = NULL;
    priv->make_password = NULL;
    priv->pop = NULL;
    priv->show_history = NULL;
    priv->sync_clipboard_to_primary = NULL;
    priv->sync_primary_to_clipboard = NULL;
    priv->upload = NULL;

    priv->c_signals[C_CHANGED] = g_signal_connect (settings,
                                                   "changed",
                                                   G_CALLBACK (g_paste_settings_settings_changed),
                                                   self);

    g_paste_settings_private_set_close_on_select_from_dconf (priv);
    g_paste_settings_private_set_element_size_from_dconf (priv);
    g_paste_settings_private_set_empty_history_confirmation_from_dconf (priv);
    g_paste_settings_private_set_growing_lines_from_dconf (priv);
    g_paste_settings_private_set_history_name_from_dconf (priv);
    g_paste_settings_private_set_images_support_from_dconf (priv);
    g_paste_settings_private_set_launch_ui_from_dconf (priv);
    g_paste_settings_private_set_make_password_from_dconf (priv);
    g_paste_settings_private_set_max_displayed_history_size_from_dconf (priv);
    g_paste_settings_private_set_max_history_size_from_dconf (priv);
    g_paste_settings_private_set_max_memory_usage_from_dconf (priv);
    g_paste_settings_private_set_max_text_item_size_from_dconf (priv);
    g_paste_settings_private_set_min_text_item_size_from_dconf (priv);
    g_paste_settings_private_set_pop_from_dconf (priv);
    g_paste_settings_private_set_primary_to_history_from_dconf (priv);
    g_paste_settings_private_set_rich_text_support_from_dconf (priv);
    g_paste_settings_private_set_save_history_from_dconf (priv);
    g_paste_settings_private_set_show_history_from_dconf (priv);
    g_paste_settings_private_set_sync_clipboard_to_primary_from_dconf (priv);
    g_paste_settings_private_set_sync_primary_to_clipboard_from_dconf (priv);
    g_paste_settings_private_set_synchronize_clipboards_from_dconf (priv);
    g_paste_settings_private_set_track_changes_from_dconf (priv);
    g_paste_settings_private_set_track_extension_state_from_dconf (priv);
    g_paste_settings_private_set_trim_items_from_dconf (priv);
    g_paste_settings_private_set_upload_from_dconf (priv);

    priv->shell_settings = NULL;
    priv->extension_enabled = FALSE;

    if (g_paste_util_has_gnome_shell ())
    {
        priv->shell_settings = g_settings_new (G_PASTE_SHELL_SETTINGS_NAME);

        priv->c_signals[C_SHELL_CHANGED] = g_signal_connect (priv->shell_settings,
                                                             "changed::" G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING,
                                                             G_CALLBACK (g_paste_settings_shell_settings_changed),
                                                             self);

        g_paste_settings_private_set_extension_enabled_from_dconf (priv);
    }
}

/**
 * g_paste_settings_new:
 *
 * Create a new instance of #GPasteSettings
 *
 * Returns: a newly allocated #GPasteSettings
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteSettings *
g_paste_settings_new (void)
{
    return g_object_new (G_PASTE_TYPE_SETTINGS, NULL);
}
