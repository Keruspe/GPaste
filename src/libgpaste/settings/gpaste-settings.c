/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-settings-private.h"

#include <gio/gio.h>

struct _GPasteSettingsPrivate
{
    GSettings *settings;
    GSettings *shell_settings;

    guint32    element_size;
    gboolean   growing_lines;
    gchar     *history_name;
    gboolean   images_support;
    gchar     *make_password;
    guint32    max_displayed_history_size;
    guint32    max_history_size;
    guint32    max_memory_usage;
    guint32    max_text_item_size;
    guint32    min_text_item_size;
    gchar     *pop;
    gboolean   primary_to_history;
    gboolean   save_history;
    gchar     *show_history;
    gchar     *sync_clipboard_to_primary;
    gchar     *sync_primary_to_clipboard;
    gboolean   synchronize_clipboards;
    gboolean   track_changes;
    gboolean   track_extension_state;
    gboolean   trim_items;

    gboolean   extension_enabled;

    gulong     changed_signal;
    gulong     shell_changed_signal;
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteSettings, g_paste_settings, G_TYPE_OBJECT)

enum
{
    CHANGED,
    REBIND,
    TRACK,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define SETTING(name, key, type, setting_type, fail, guards, clear_func, dup_func)                     \
    G_PASTE_VISIBLE type                                                                               \
    g_paste_settings_get_##name (const GPasteSettings *self)                                           \
    {                                                                                                  \
        g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), fail);                                       \
        GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private ((GPasteSettings *) self); \
        return priv->name;                                                                             \
    }                                                                                                  \
    G_PASTE_VISIBLE void                                                                               \
    g_paste_settings_reset_##name (GPasteSettings *self)                                               \
    {                                                                                                  \
        g_return_if_fail (G_PASTE_IS_SETTINGS (self));                                                 \
        GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private ((GPasteSettings *) self); \
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
        g_return_if_fail (G_PASTE_IS_SETTINGS (self));                                                 \
        guards                                                                                         \
        GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private (self);                    \
        clear_func                                                                                     \
        priv->name = dup_func (value);                                                                 \
        g_settings_set_##setting_type (priv->settings, G_PASTE_##key##_SETTING, value);                \
    }

#define TRIVIAL_SETTING(name, key, type, setting_type, fail) \
    SETTING (name, key, type, setting_type, fail, {}, {},)

#define BOOLEAN_SETTING(name, key) TRIVIAL_SETTING (name, key, gboolean, boolean, FALSE)
#define UNSIGNED_SETTING(name, key) TRIVIAL_SETTING (name, key, guint32, uint, 0)

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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_element_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum displayed size of an item
 *
 * Change the "element-size" setting
 *
 * Returns:
 */
UNSIGNED_SETTING (element_size, ELEMENT_SIZE)

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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_growing_lines:
 * @self: a #GPasteSettings instance
 * @value: whether to detect or not growing lines
 *
 * Change the "growing-lines" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_history_name:
 * @self: a #GPasteSettings instance
 * @value: the new history name
 *
 * Change the "history-name" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_images_support:
 * @self: a #GPasteSettings instance
 * @value: the new history name
 *
 * Change the "images-support" setting
 *
 * Returns:
 */
BOOLEAN_SETTING (images_support, IMAGES_SUPPORT)

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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_make_password:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "make-password" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_max_displayed_history_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum number of items to display
 *
 * Change the "max-displayed-history-size" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_max_history_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum number of items the history can contain
 *
 * Change the "max-history-size" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_max_memory_usage:
 * @self: a #GPasteSettings instance
 * @value: the maximum amout of memory we can use
 *
 * Change the "max-memory-usage" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_max_text_item_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum size for a textual item to be handled
 *
 * Change the "max-text-item-size" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_min_text_item_size:
 * @self: a #GPasteSettings instance
 * @value: the minimum size for a textual item to be handled
 *
 * Change the "min-text-item-size" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_pop:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "pop" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_primary_to_history:
 * @self: a #GPasteSettings instance
 * @value: whether to track or not the primary selection changes as clipboard ones
 *
 * Change the "primary-to-history" setting
 *
 * Returns:
 */
BOOLEAN_SETTING (primary_to_history, PRIMARY_TO_HISTORY)

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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_save_history:
 * @self: a #GPasteSettings instance
 * @value: whether to save or not the history
 *
 * Change the "save-history" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_show_history:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "show-history" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_sync_clipboard_to_primary:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "sync-clipboard-to-primary" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_sync_primary_to_clipboard:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "sync-primary-to-clipboard" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_synchronize_clipboards:
 * @self: a #GPasteSettings instance
 * @value: whether to synchronize the clipboard and the primary selection or not
 *
 * Change the "synchronize-clipboards" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_track_changes:
 * @self: a #GPasteSettings instance
 * @value: whether to track or not the clipboard changes
 *
 * Change the "track-changes" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_track_extension_state:
 * @self: a #GPasteSettings instance
 * @value: whether to stop tracking or not the clipboard changes when an applet exits
 *
 * Change the "track-extension-state" setting
 *
 * Returns:
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
 *
 * Returns:
 */
/**
 * g_paste_settings_set_trim_items:
 * @self: a #GPasteSettings instance
 * @value: whether to trim or not textual items
 *
 * Change the "trim-items" setting
 *
 * Returns:
 */
BOOLEAN_SETTING (trim_items, TRIM_ITEMS)

#if G_PASTE_CONFIG_ENABLE_EXTENSION
#define EXTENSION_NAME "GPaste@gnome-shell-extensions.gnome.org"
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
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), FALSE);
    GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private ((GPasteSettings *) self);
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
    G_PASTE_CLEANUP_STRFREEV gchar **extensions = g_paste_settings_private_get_enabled_extensions (priv);
    for (gchar **e = extensions; *e; ++e)
    {
        if (!g_strcmp0 (*e, EXTENSION_NAME))
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
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_extension_enabled (GPasteSettings *self,
                                        gboolean        value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));
    GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private ((GPasteSettings *) self);
    G_PASTE_CLEANUP_STRFREEV gchar **extensions = NULL;
    if (!priv->shell_settings || (value == priv->extension_enabled))
        return;

    extensions = g_paste_settings_private_get_enabled_extensions (priv);
    gsize nb = g_strv_length (extensions);
    if (value)
    {
        extensions = g_realloc (extensions, (nb + 2) * sizeof (gchar *));
        extensions[nb] = g_strdup (EXTENSION_NAME);
        extensions[nb+1] = NULL;
    }
    else
    {
        gboolean found = FALSE;
        for (gsize i = 0; i < nb; ++i)
        {
            if (!found && !g_strcmp0 (extensions[i], EXTENSION_NAME))
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
#endif

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

    if (!g_strcmp0 (key, G_PASTE_ELEMENT_SIZE_SETTING))
        g_paste_settings_private_set_element_size_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_GROWING_LINES_SETTING))
        g_paste_settings_private_set_growing_lines_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_HISTORY_NAME_SETTING))
        g_paste_settings_private_set_history_name_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_IMAGES_SUPPORT_SETTING))
        g_paste_settings_private_set_images_support_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_MAKE_PASSWORD_SETTING))
    {
        g_paste_settings_private_set_make_password_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_MAKE_PASSWORD_SETTING);
    }
    else if (!g_strcmp0 (key, G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING))
        g_paste_settings_private_set_max_displayed_history_size_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_MAX_HISTORY_SIZE_SETTING))
        g_paste_settings_private_set_max_history_size_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_MAX_MEMORY_USAGE_SETTING))
        g_paste_settings_private_set_max_memory_usage_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_MAX_TEXT_ITEM_SIZE_SETTING))
        g_paste_settings_private_set_max_text_item_size_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_MIN_TEXT_ITEM_SIZE_SETTING))
        g_paste_settings_private_set_min_text_item_size_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_POP_SETTING))
    {
        g_paste_settings_private_set_pop_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_POP_SETTING);
    }
    else if (!g_strcmp0 (key, G_PASTE_PRIMARY_TO_HISTORY_SETTING ))
        g_paste_settings_private_set_primary_to_history_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_SAVE_HISTORY_SETTING))
        g_paste_settings_private_set_save_history_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_SHOW_HISTORY_SETTING))
    {
        g_paste_settings_private_set_show_history_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_SHOW_HISTORY_SETTING);
    }
    else if (!g_strcmp0 (key, G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING))
    {
        g_paste_settings_private_set_sync_clipboard_to_primary_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING);
    }
    else if (!g_strcmp0 (key, G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING))
    {
        g_paste_settings_private_set_sync_primary_to_clipboard_from_dconf (priv);
        g_paste_settings_rebind (self, G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING);
    }
    else if (!g_strcmp0 (key, G_PASTE_SYNCHRONIZE_CLIPBOARDS_SETTING))
        g_paste_settings_private_set_synchronize_clipboards_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_TRACK_CHANGES_SETTING))
    {
        g_paste_settings_private_set_track_changes_from_dconf (priv);
        g_signal_emit (self,
                       signals[TRACK],
                       0, /* detail */
                       priv->track_changes,
                       NULL);
    }
    else if (!g_strcmp0 (key, G_PASTE_TRACK_EXTENSION_STATE_SETTING))
        g_paste_settings_private_set_track_extension_state_from_dconf (priv);
    else if (!g_strcmp0 (key, G_PASTE_TRIM_ITEMS_SETTING))
        g_paste_settings_private_set_trim_items_from_dconf (priv);

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

    if (settings)
    {
        g_signal_handler_disconnect (settings, priv->changed_signal);
        g_clear_object (&priv->settings);
    }

#if G_PASTE_CONFIG_ENABLE_EXTENSION
    GSettings *shell_settings = priv->shell_settings;

    if (shell_settings)
    {
        g_signal_handler_disconnect (shell_settings, priv->shell_changed_signal);
        g_clear_object (&priv->shell_settings);
    }
#endif

    G_OBJECT_CLASS (g_paste_settings_parent_class)->dispose (object);
}

static void
g_paste_settings_finalize (GObject *object)
{
    GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private (G_PASTE_SETTINGS (object));

    g_free (priv->history_name);
    g_free (priv->make_password);
    g_free (priv->pop);
    g_free (priv->show_history);
    g_free (priv->sync_clipboard_to_primary);
    g_free (priv->sync_primary_to_clipboard);

    G_OBJECT_CLASS (g_paste_settings_parent_class)->finalize (object);
}

static void
g_paste_settings_class_init (GPasteSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_settings_dispose;
    object_class->finalize = g_paste_settings_finalize;

    signals[CHANGED] = NEW_SIGNAL_DETAILED_STATIC ("changed", STRING);
    signals[REBIND]  = NEW_SIGNAL_DETAILED        ("rebind" , STRING);
    signals[TRACK]   = NEW_SIGNAL                 ("track"  , BOOLEAN);
}

static void
g_paste_settings_init (GPasteSettings *self)
{
    GPasteSettingsPrivate *priv = g_paste_settings_get_instance_private (self);
    GSettings *settings = priv->settings = g_settings_new (G_PASTE_SETTINGS_NAME);

    priv->history_name = NULL;
    priv->make_password = NULL;
    priv->pop = NULL;
    priv->show_history = NULL;
    priv->sync_clipboard_to_primary = NULL;
    priv->sync_primary_to_clipboard = NULL;

    g_paste_settings_private_set_element_size_from_dconf (priv);
    g_paste_settings_private_set_growing_lines_from_dconf (priv);
    g_paste_settings_private_set_history_name_from_dconf (priv);
    g_paste_settings_private_set_images_support_from_dconf (priv);
    g_paste_settings_private_set_make_password_from_dconf (priv);
    g_paste_settings_private_set_max_displayed_history_size_from_dconf (priv);
    g_paste_settings_private_set_max_history_size_from_dconf (priv);
    g_paste_settings_private_set_max_memory_usage_from_dconf (priv);
    g_paste_settings_private_set_max_text_item_size_from_dconf (priv);
    g_paste_settings_private_set_min_text_item_size_from_dconf (priv);
    g_paste_settings_private_set_pop_from_dconf (priv);
    g_paste_settings_private_set_primary_to_history_from_dconf (priv);
    g_paste_settings_private_set_save_history_from_dconf (priv);
    g_paste_settings_private_set_show_history_from_dconf (priv);
    g_paste_settings_private_set_sync_clipboard_to_primary_from_dconf (priv);
    g_paste_settings_private_set_sync_primary_to_clipboard_from_dconf (priv);
    g_paste_settings_private_set_synchronize_clipboards_from_dconf (priv);
    g_paste_settings_private_set_track_changes_from_dconf (priv);
    g_paste_settings_private_set_track_extension_state_from_dconf (priv);
    g_paste_settings_private_set_trim_items_from_dconf (priv);

    priv->changed_signal = g_signal_connect (settings,
                                             "changed",
                                             G_CALLBACK (g_paste_settings_settings_changed),
                                             self);

    priv->shell_settings = NULL;
    priv->extension_enabled = FALSE;

#if G_PASTE_CONFIG_ENABLE_EXTENSION
    GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
    if (!source)
        return;

    G_PASTE_CLEANUP_GSCHEMA_UNREF GSettingsSchema *schema = g_settings_schema_source_lookup (source, G_PASTE_SHELL_SETTINGS_NAME, TRUE);
    if (!schema)
        return;

    priv->shell_settings = g_settings_new (G_PASTE_SHELL_SETTINGS_NAME);

    g_paste_settings_private_set_extension_enabled_from_dconf (priv);

    priv->shell_changed_signal = g_signal_connect (priv->shell_settings,
                                                  "changed::" G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING,
                                                  G_CALLBACK (g_paste_settings_shell_settings_changed),
                                                  self);
#endif
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
