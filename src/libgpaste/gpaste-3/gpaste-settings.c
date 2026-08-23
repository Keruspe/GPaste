// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-util.h>

#define G_SETTINGS_ENABLE_BACKEND 1
#include <gio/gsettingsbackend.h>

struct _GPasteSettings
{
    GObject parent_instance;

    GSettings    *settings;
    GSettings    *shell_settings;
    GSignalGroup *settings_signals;
    GSignalGroup *shell_settings_signals;

    gboolean      close_on_select;
    guint64       element_size;
    gboolean      empty_history_confirmation;
    gboolean      experimental_meta_daemon;
    gboolean      growing_lines;
    gchar        *history_name;
    gboolean      images_support;
    gboolean      images_preview;
    guint64       images_preview_size;
    gchar        *launch_ui;
    gchar        *make_password;
    guint64       max_history_size;
    guint64       max_memory_usage;
    guint64       max_text_item_size;
    guint64       min_text_item_size;
    gchar        *pop;
    gboolean      primary_to_history;
    gboolean      rich_text_support;
    gchar        *show_history;
    GPasteStorage storage_backend;
    guint64       storage_backend_revision;
    gchar        *sync_clipboard_to_primary;
    gchar        *sync_primary_to_clipboard;
    gboolean      synchronize_clipboards;
    gboolean      track_changes;
    gboolean      track_extension_state;
    gboolean      trim_items;
    gchar        *upload;

    gboolean      extension_enabled;
};

G_PASTE_DEFINE_TYPE (Settings, settings, G_TYPE_OBJECT)

enum
{
    REBIND,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/* How a setting's cached value is replaced. GSettings hands out a fresh string,
 * so the dconf path consumes it, while the public setter only borrows its
 * argument -- which it also has to hand to GSettings afterwards. */
#define ASSIGN(dest, src)      (dest) = (src)
#define ASSIGN_TAKE(dest, src) g_set_str_take (&(dest), (src))
#define ASSIGN_DUP(dest, src)  g_set_str (&(dest), (src))

#define SETTING(name, key, type, setting_type, fail, guards, assign_owned, assign_borrowed)            \
    G_PASTE_VISIBLE type                                                                               \
    g_paste_settings_get_##name (GPasteSettings *self)                                                 \
    {                                                                                                  \
        g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), fail);                                       \
        return self->name;                                                                             \
    }                                                                                                  \
    static void                                                                                        \
    g_paste_settings_private_set_##name##_from_dconf (GPasteSettings *self)                            \
    {                                                                                                  \
        assign_owned (self->name, g_settings_get_##setting_type (self->settings,                       \
                                                                 G_PASTE_##key##_SETTING));            \
    }                                                                                                  \
    G_PASTE_VISIBLE void                                                                               \
    g_paste_settings_set_##name (GPasteSettings *self,                                                 \
                                 type            value)                                                \
    {                                                                                                  \
        g_return_if_fail (G_PASTE_IS_SETTINGS (self));                                                 \
        guards                                                                                         \
        assign_borrowed (self->name, value);                                                           \
        g_settings_set_##setting_type (self->settings, G_PASTE_##key##_SETTING, value);                \
    }

#define TRIVIAL_SETTING(name, key, type, setting_type, fail) \
    SETTING (name, key, type, setting_type, fail, {}, ASSIGN, ASSIGN)

#define BOOLEAN_SETTING(name, key) TRIVIAL_SETTING (name, key, gboolean, boolean, FALSE)
#define UNSIGNED_SETTING(name, key) TRIVIAL_SETTING (name, key, guint64, uint64, 0)
#define ENUM_SETTING(name, key, type) TRIVIAL_SETTING (name, key, type, enum, 0)

#define STRING_SETTING(name, key) SETTING (name, key, const gchar *, string, NULL,                \
                                           g_return_if_fail (value);                              \
                                           g_return_if_fail (g_utf8_validate (value, -1, NULL));, \
                                           ASSIGN_TAKE, ASSIGN_DUP)

/* "rebind" is the only signal this class carries; every other change
 * notification is the property one. */
#define NEW_SIGNAL_DETAILED(name, arg_type)              \
    g_signal_new (name,                                  \
                  G_PASTE_TYPE_SETTINGS,                 \
                  G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED, \
                  0, /* class offset */                  \
                  NULL, /* accumulator */                \
                  NULL, /* accumulator data */           \
                  g_cclosure_marshal_VOID__##arg_type,   \
                  G_TYPE_NONE,                           \
                  1, /* number of params */              \
                  G_TYPE_##arg_type)

/**
 * g_paste_settings_get_close_on_select:
 * @self: a #GPasteSettings instance
 *
 * Get the "close-on-select" setting
 *
 * Returns: the value of the "close-on-select" setting
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
 * g_paste_settings_set_empty_history_confirmation:
 * @self: a #GPasteSettings instance
 * @value: whether to prompt for confirmation when emptying a history
 *
 * Change the "empty-history-confirmation" setting
 */
BOOLEAN_SETTING (empty_history_confirmation, EMPTY_HISTORY_CONFIRMATION)

/**
 * g_paste_settings_get_experimental_meta_daemon:
 * @self: a #GPasteSettings instance
 *
 * Get the "experimental-meta-daemon" setting
 *
 * Returns: the value of the "experimental-meta-daemon" setting
 */
/**
 * g_paste_settings_set_experimental_meta_daemon:
 * @self: a #GPasteSettings instance
 * @value: whether the gnome-shell extension runs the experimental in-shell daemon
 *
 * Change the "experimental-meta-daemon" setting
 */
BOOLEAN_SETTING (experimental_meta_daemon, EXPERIMENTAL_META_DAEMON)

/**
 * g_paste_settings_get_growing_lines:
 * @self: a #GPasteSettings instance
 *
 * Get the "growing-lines" setting
 *
 * Returns: the value of the "growing-lines" setting
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
 * g_paste_settings_set_images_support:
 * @self: a #GPasteSettings instance
 * @value: the new history name
 *
 * Change the "images-support" setting
 */
BOOLEAN_SETTING (images_support, IMAGES_SUPPORT)

/**
 * g_paste_settings_get_images_preview:
 * @self: a #GPasteSettings instance
 *
 * Get the "images-preview" setting
 *
 * Returns: the value of the "images-preview" setting
 */
/**
 * g_paste_settings_set_images_preview:
 * @self: a #GPasteSettings instance
 * @value: whether to enable or not image previews
 *
 * Change the "images-preview" setting
 */
BOOLEAN_SETTING (images_preview, IMAGES_PREVIEW)

/**
 * g_paste_settings_get_images_preview_size:
 * @self: a #GPasteSettings instance
 *
 * Get the "images-preview-size" setting
 *
 * Returns: the value of the "images-preview-size" setting
 */
/**
 * g_paste_settings_set_images_preview_size:
 * @self: a #GPasteSettings instance
 * @value: the size of image previews in pixels
 *
 * Change the "images-preview-size" setting
 */
UNSIGNED_SETTING (images_preview_size, IMAGES_PREVIEW_SIZE)

/**
 * g_paste_settings_get_launch_ui:
 * @self: a #GPasteSettings instance
 *
 * Get the "launch-ui" setting
 *
 * Returns: the value of the "launch-ui" setting
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
 * g_paste_settings_set_make_password:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the "make-password" setting
 */
STRING_SETTING (make_password, MAKE_PASSWORD)

/**
 * g_paste_settings_get_max_history_size:
 * @self: a #GPasteSettings instance
 *
 * Get the "max-history-size" setting
 *
 * Returns: the value of the "max-history-size" setting
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
 * g_paste_settings_set_rich_text_support:
 * @self: a #GPasteSettings instance
 * @value: the new history name
 *
 * Change the "rich-text-support" setting
 */
BOOLEAN_SETTING (rich_text_support, RICH_TEXT_SUPPORT)

/**
 * g_paste_settings_get_show_history:
 * @self: a #GPasteSettings instance
 *
 * Get the "show-history" setting
 *
 * Returns: the value of the "show-history" setting
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
 * g_paste_settings_get_storage_backend:
 * @self: a #GPasteSettings instance
 *
 * Get the "storage-backend" setting
 *
 * Returns: the value of the "storage-backend" setting (a #GPasteStorage)
 */
/**
 * g_paste_settings_set_storage_backend:
 * @self: a #GPasteSettings instance
 * @value: the storage backend to use (a #GPasteStorage)
 *
 * Change the "storage-backend" setting
 */
ENUM_SETTING (storage_backend, STORAGE_BACKEND, GPasteStorage)

/**
 * g_paste_settings_get_storage_backend_revision:
 * @self: a #GPasteSettings instance
 *
 * Get the "storage-backend-revision" setting
 *
 * Returns: the value of the "storage-backend-revision" setting
 */
/**
 * g_paste_settings_set_storage_backend_revision:
 * @self: a #GPasteSettings instance
 * @value: the last processed storage backend revision
 *
 * Change the "storage-backend-revision" setting
 */
UNSIGNED_SETTING (storage_backend_revision, STORAGE_BACKEND_REVISION)

/**
 * g_paste_settings_get_sync_clipboard_to_primary:
 * @self: a #GPasteSettings instance
 *
 * Get the "sync-clipboard-to-primary" setting
 *
 * Returns: the value of the "sync-clipboard-to-primary" setting
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
 * g_paste_settings_set_track_extension_state:
 * @self: a #GPasteSettings instance
 * @value: whether to stop tracking or not the clipboard changes when the GNOME Shell extension is disabled
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
g_paste_settings_get_extension_enabled (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), FALSE);
    return self->extension_enabled;
}

static inline gchar **
g_paste_settings_private_get_enabled_extensions (GPasteSettings *self)
{
    return (self->shell_settings) ? g_settings_get_strv (self->shell_settings, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING) : NULL;
}

static void
g_paste_settings_private_set_extension_enabled_from_dconf (GPasteSettings *self)
{
    g_auto (GStrv) extensions = g_paste_settings_private_get_enabled_extensions (self);
    for (GStrv e = extensions; *e; ++e)
    {
        if (g_paste_str_equal (*e, G_PASTE_EXTENSION_NAME))
        {
            self->extension_enabled = TRUE;
            return;
        }
    }
    self->extension_enabled = FALSE;
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
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    g_auto (GStrv) extensions = NULL;

    if (!self->shell_settings || (value == self->extension_enabled))
        return;

    extensions = g_paste_settings_private_get_enabled_extensions (self);
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

    self->extension_enabled = value;
    g_settings_set_strv (self->shell_settings, G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING, (const gchar * const *) extensions);
}

static void
g_paste_settings_shell_settings_changed (GSettings   *settings G_GNUC_UNUSED,
                                         const gchar *key      G_GNUC_UNUSED,
                                         gpointer     user_data)
{
    GPasteSettings *self = G_PASTE_SETTINGS (user_data);

    g_paste_settings_private_set_extension_enabled_from_dconf (self);
    g_object_notify (G_OBJECT (self), G_PASTE_EXTENSION_ENABLED_SETTING);
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

/* The settings cached from dconf, each paired with the loader the SETTING macro
 * generated for it. Both g_paste_settings_settings_changed and the initial load
 * in g_paste_settings_init are driven off this single table, so neither can drift
 * from the accessors above. (extension-enabled lives in the separate shell
 * settings and is handled on its own.) */
typedef struct
{
    const gchar *key;
    void       (*from_dconf) (GPasteSettings *self);
    gboolean     rebind; /* keybinding settings re-grab their shortcut on change */
} GPasteSettingEntry;

#define SETTING_ENTRY(KEY, name)    { G_PASTE_##KEY##_SETTING, g_paste_settings_private_set_##name##_from_dconf, FALSE }
#define KEYBINDING_ENTRY(KEY, name) { G_PASTE_##KEY##_SETTING, g_paste_settings_private_set_##name##_from_dconf, TRUE }

static const GPasteSettingEntry setting_entries[] = {
    SETTING_ENTRY (CLOSE_ON_SELECT, close_on_select),
    SETTING_ENTRY (ELEMENT_SIZE, element_size),
    SETTING_ENTRY (EMPTY_HISTORY_CONFIRMATION, empty_history_confirmation),
    SETTING_ENTRY (EXPERIMENTAL_META_DAEMON, experimental_meta_daemon),
    SETTING_ENTRY (GROWING_LINES, growing_lines),
    SETTING_ENTRY (HISTORY_NAME, history_name),
    SETTING_ENTRY (IMAGES_SUPPORT, images_support),
    SETTING_ENTRY (IMAGES_PREVIEW, images_preview),
    SETTING_ENTRY (IMAGES_PREVIEW_SIZE, images_preview_size),
    KEYBINDING_ENTRY (LAUNCH_UI, launch_ui),
    KEYBINDING_ENTRY (MAKE_PASSWORD, make_password),
    SETTING_ENTRY (MAX_HISTORY_SIZE, max_history_size),
    SETTING_ENTRY (MAX_MEMORY_USAGE, max_memory_usage),
    SETTING_ENTRY (MAX_TEXT_ITEM_SIZE, max_text_item_size),
    SETTING_ENTRY (MIN_TEXT_ITEM_SIZE, min_text_item_size),
    KEYBINDING_ENTRY (POP, pop),
    SETTING_ENTRY (PRIMARY_TO_HISTORY, primary_to_history),
    SETTING_ENTRY (RICH_TEXT_SUPPORT, rich_text_support),
    KEYBINDING_ENTRY (SHOW_HISTORY, show_history),
    SETTING_ENTRY (STORAGE_BACKEND, storage_backend),
    SETTING_ENTRY (STORAGE_BACKEND_REVISION, storage_backend_revision),
    KEYBINDING_ENTRY (SYNC_CLIPBOARD_TO_PRIMARY, sync_clipboard_to_primary),
    KEYBINDING_ENTRY (SYNC_PRIMARY_TO_CLIPBOARD, sync_primary_to_clipboard),
    SETTING_ENTRY (SYNCHRONIZE_CLIPBOARDS, synchronize_clipboards),
    SETTING_ENTRY (TRACK_CHANGES, track_changes),
    SETTING_ENTRY (TRACK_EXTENSION_STATE, track_extension_state),
    SETTING_ENTRY (TRIM_ITEMS, trim_items),
    KEYBINDING_ENTRY (UPLOAD, upload),
};

#undef SETTING_ENTRY
#undef KEYBINDING_ENTRY

static void
g_paste_settings_settings_changed (GSettings   *settings G_GNUC_UNUSED,
                                   const gchar *key,
                                   gpointer     user_data)
{
    GPasteSettings *self = G_PASTE_SETTINGS (user_data);

    for (gsize i = 0; i < G_N_ELEMENTS (setting_entries); ++i)
    {
        const GPasteSettingEntry *entry = &setting_entries[i];

        if (!g_paste_str_equal (key, entry->key))
            continue;

        entry->from_dconf (self);
        /* Property name == GSettings key, so "notify::<key>" is the one change
         * notification every listener needs: bound widgets refresh from here,
         * and so does everything else watching a setting. */
        g_object_notify (G_OBJECT (self), entry->key);
        if (entry->rebind)
            g_paste_settings_rebind (self, key);
        break;
    }
}

/* --- Bindable GObject properties -------------------------------------------
 *
 * Every setting is exposed as a GObject property named exactly like its
 * GSettings key (e.g. "track-changes"), so UI widgets can bind to it with
 * g_object_bind_property() rather than wiring getters/setters by hand. The
 * property accessors delegate to the typed get/set above (keeping their
 * validation), and external changes notify through the "changed" handler. */
#define G_PASTE_SETTINGS_FOR_EACH_PROP(BOOL, UINT, STR, ENUM)                             \
    BOOL (close_on_select,            CLOSE_ON_SELECT)                                    \
    UINT (element_size,               ELEMENT_SIZE)                                       \
    BOOL (empty_history_confirmation, EMPTY_HISTORY_CONFIRMATION)                         \
    BOOL (experimental_meta_daemon,   EXPERIMENTAL_META_DAEMON)                           \
    BOOL (growing_lines,              GROWING_LINES)                                      \
    STR  (history_name,               HISTORY_NAME)                                       \
    BOOL (images_support,             IMAGES_SUPPORT)                                     \
    BOOL (images_preview,             IMAGES_PREVIEW)                                     \
    UINT (images_preview_size,        IMAGES_PREVIEW_SIZE)                                \
    STR  (launch_ui,                  LAUNCH_UI)                                          \
    STR  (make_password,              MAKE_PASSWORD)                                      \
    UINT (max_history_size,           MAX_HISTORY_SIZE)                                   \
    UINT (max_memory_usage,           MAX_MEMORY_USAGE)                                   \
    UINT (max_text_item_size,         MAX_TEXT_ITEM_SIZE)                                 \
    UINT (min_text_item_size,         MIN_TEXT_ITEM_SIZE)                                 \
    STR  (pop,                        POP)                                                \
    BOOL (primary_to_history,         PRIMARY_TO_HISTORY)                                 \
    BOOL (rich_text_support,          RICH_TEXT_SUPPORT)                                  \
    STR  (show_history,               SHOW_HISTORY)                                       \
    ENUM (storage_backend,            STORAGE_BACKEND,              G_PASTE_TYPE_STORAGE) \
    UINT (storage_backend_revision,   STORAGE_BACKEND_REVISION)                           \
    STR  (sync_clipboard_to_primary,  SYNC_CLIPBOARD_TO_PRIMARY)                          \
    STR  (sync_primary_to_clipboard,  SYNC_PRIMARY_TO_CLIPBOARD)                          \
    BOOL (synchronize_clipboards,     SYNCHRONIZE_CLIPBOARDS)                             \
    BOOL (track_changes,              TRACK_CHANGES)                                      \
    BOOL (track_extension_state,      TRACK_EXTENSION_STATE)                              \
    BOOL (trim_items,                 TRIM_ITEMS)                                         \
    STR  (upload,                     UPLOAD)

enum
{
    PROP_0,
#define _PROP_ID(name, KEY) PROP_##name,
#define _PROP_ID_ENUM(name, KEY, TYPE) PROP_##name,
    G_PASTE_SETTINGS_FOR_EACH_PROP (_PROP_ID, _PROP_ID, _PROP_ID, _PROP_ID_ENUM)
#undef _PROP_ID_ENUM
#undef _PROP_ID
    /* Derived from the shell schema rather than a plain key; handled apart. */
    PROP_extension_enabled,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
g_paste_settings_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    GPasteSettings *self = G_PASTE_SETTINGS (object);

    switch (prop_id)
    {
#define _GET_BOOL(name, KEY) case PROP_##name: g_value_set_boolean (value, g_paste_settings_get_##name (self)); break;
#define _GET_UINT(name, KEY) case PROP_##name: g_value_set_uint64 (value, g_paste_settings_get_##name (self)); break;
#define _GET_STR(name, KEY)  case PROP_##name: g_value_set_string (value, g_paste_settings_get_##name (self)); break;
#define _GET_ENUM(name, KEY, TYPE) case PROP_##name: g_value_set_enum (value, g_paste_settings_get_##name (self)); break;
    G_PASTE_SETTINGS_FOR_EACH_PROP (_GET_BOOL, _GET_UINT, _GET_STR, _GET_ENUM)
#undef _GET_ENUM
#undef _GET_BOOL
#undef _GET_UINT
#undef _GET_STR
    case PROP_extension_enabled:
        g_value_set_boolean (value, g_paste_settings_get_extension_enabled (self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
g_paste_settings_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    GPasteSettings *self = G_PASTE_SETTINGS (object);

    switch (prop_id)
    {
#define _SET_BOOL(name, KEY) case PROP_##name: g_paste_settings_set_##name (self, g_value_get_boolean (value)); break;
#define _SET_UINT(name, KEY) case PROP_##name: g_paste_settings_set_##name (self, g_value_get_uint64 (value)); break;
#define _SET_STR(name, KEY)  case PROP_##name: g_paste_settings_set_##name (self, g_value_get_string (value)); break;
#define _SET_ENUM(name, KEY, TYPE) case PROP_##name: g_paste_settings_set_##name (self, g_value_get_enum (value)); break;
    G_PASTE_SETTINGS_FOR_EACH_PROP (_SET_BOOL, _SET_UINT, _SET_STR, _SET_ENUM)
#undef _SET_ENUM
#undef _SET_BOOL
#undef _SET_UINT
#undef _SET_STR
    case PROP_extension_enabled:
        g_paste_settings_set_extension_enabled (self, g_value_get_boolean (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

/**
 * g_paste_settings_reset:
 * @self: a #GPasteSettings instance
 * @key: the GSettings key (also the property name) to reset
 *
 * Reset the given key to its default value. The change propagates through the
 * usual "changed"/notify path, so bound widgets update automatically.
 */
G_PASTE_VISIBLE void
g_paste_settings_reset (GPasteSettings *self,
                        const gchar    *key)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));
    g_return_if_fail (key);

    g_settings_reset (self->settings, key);
}

/**
 * g_paste_settings_is_default:
 * @self: a #GPasteSettings instance
 * @key: the GSettings key to test
 *
 * Check whether @key is still at its default value (i.e. the user has not
 * overridden it), so callers can e.g. disable a per-key reset action.
 *
 * Returns: %TRUE if @key has no user-set value
 */
G_PASTE_VISIBLE gboolean
g_paste_settings_is_default (GPasteSettings *self,
                             const gchar    *key)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), TRUE);
    g_return_val_if_fail (key, TRUE);

    g_autoptr (GVariant) user_value = g_settings_get_user_value (self->settings, key);

    return user_value == NULL;
}

/**
 * g_paste_settings_sync:
 * @self: a #GPasteSettings instance
 *
 * Flush every pending settings write to the backend and block until it lands.
 * GSettings writes are delivered asynchronously, so a short-lived process (e.g.
 * gpaste-client) must call this before it acts on the change or exits — without
 * it a write can still be in flight, and another process reading the key (or the
 * same process re-executing) would see the old value. Flushes the whole process'
 * GSettings traffic, not just this instance's.
 */
G_PASTE_VISIBLE void
g_paste_settings_sync (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    g_settings_sync ();
}

/**
 * g_paste_settings_reload:
 * @self: a #GPasteSettings instance
 *
 * Re-read every key from the backend, as if the instance had just been created.
 *
 * The cached values are normally refreshed from GSettings' "changed"
 * notification, which is delivered by the dconf service on its own schedule: a
 * process told *by a third party* that a key changed (the daemon being asked to
 * re-execute right after `gpaste-client migrate` rewrote the backend revision)
 * has no ordering guarantee that the notification landed first. Reloading before acting
 * on such a key reads the value that is actually stored.
 */
G_PASTE_VISIBLE void
g_paste_settings_reload (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    for (gsize i = 0; i < G_N_ELEMENTS (setting_entries); ++i)
        setting_entries[i].from_dconf (self);
}

static void
g_paste_settings_dispose (GObject *object)
{
    GPasteSettings *self = G_PASTE_SETTINGS (object);

    g_clear_object (&self->settings_signals);
    g_clear_object (&self->settings);
    g_clear_object (&self->shell_settings_signals);
    g_clear_object (&self->shell_settings);

    G_OBJECT_CLASS (g_paste_settings_parent_class)->dispose (object);
}

static void
g_paste_settings_finalize (GObject *object)
{
    GPasteSettings *self = G_PASTE_SETTINGS (object);

    g_free (self->history_name);
    g_free (self->launch_ui);
    g_free (self->make_password);
    g_free (self->pop);
    g_free (self->show_history);
    g_free (self->sync_clipboard_to_primary);
    g_free (self->sync_primary_to_clipboard);
    g_free (self->upload);

    G_OBJECT_CLASS (g_paste_settings_parent_class)->finalize (object);
}

static void
g_paste_settings_class_init (GPasteSettingsClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_settings_dispose;
    object_class->finalize = g_paste_settings_finalize;
    object_class->get_property = g_paste_settings_get_property;
    object_class->set_property = g_paste_settings_set_property;

    /* One bindable property per setting, named after its GSettings key.
     * G_PARAM_EXPLICIT_NOTIFY: the only notify comes from the "changed" handler,
     * once the stored value has actually been refreshed. */
#define _INSTALL_BOOL(name, KEY) properties[PROP_##name] = g_param_spec_boolean (G_PASTE_##KEY##_SETTING, NULL, NULL, FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
#define _INSTALL_UINT(name, KEY) properties[PROP_##name] = g_param_spec_uint64 (G_PASTE_##KEY##_SETTING, NULL, NULL, 0, G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
#define _INSTALL_STR(name, KEY)  properties[PROP_##name] = g_param_spec_string (G_PASTE_##KEY##_SETTING, NULL, NULL, NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
#define _INSTALL_ENUM(name, KEY, TYPE) properties[PROP_##name] = g_param_spec_enum (G_PASTE_##KEY##_SETTING, NULL, NULL, TYPE, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
    G_PASTE_SETTINGS_FOR_EACH_PROP (_INSTALL_BOOL, _INSTALL_UINT, _INSTALL_STR, _INSTALL_ENUM)
#undef _INSTALL_ENUM
#undef _INSTALL_BOOL
#undef _INSTALL_UINT
#undef _INSTALL_STR
    properties[PROP_extension_enabled] = g_param_spec_boolean (G_PASTE_EXTENSION_ENABLED_SETTING, NULL, NULL, FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
    g_object_class_install_properties (object_class, N_PROPERTIES, properties);

    /**
     * GPasteSettings::rebind:
     * @settings: the object on which the signal was emitted
     * @key: the name of the key that changed
     *
     * The "rebind" signal is emitted when a keybinding key has potentially
     * changed and its binding has to be registered again. This is *not* a
     * duplicate of "notify::x": a rebind is an action to take, not a value that
     * changed, and only the keybinding settings carry it.
     *
     * Every other change notification is the property one. Each setting is a
     * GObject property named exactly like its GSettings key, so "notify::x" is
     * how you observe key "x".
     *
     * This signal supports detailed connections.  You can connect to the
     * detailed signal "rebind::x" in order to only receive callbacks
     * when key "x" changes.
     */
    signals[REBIND]  = NEW_SIGNAL_DETAILED        ("rebind" , STRING);
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
        return g_settings_new (G_PASTE_SETTINGS_NAME);
}

static void
g_paste_settings_init (GPasteSettings *self)
{
    GSettings *settings = self->settings = create_g_settings ();

    self->history_name = NULL;
    self->launch_ui = NULL;
    self->make_password = NULL;
    self->pop = NULL;
    self->show_history = NULL;
    self->sync_clipboard_to_primary = NULL;
    self->sync_primary_to_clipboard = NULL;
    self->upload = NULL;

    GSignalGroup *settings_signals = self->settings_signals = g_signal_group_new (G_TYPE_SETTINGS);
    g_signal_group_connect (settings_signals, "changed", G_CALLBACK (g_paste_settings_settings_changed), self);
    g_signal_group_set_target (settings_signals, settings);

    g_paste_settings_reload (self);

    self->shell_settings = NULL;
    self->extension_enabled = FALSE;

    GSignalGroup *shell_settings_signals = self->shell_settings_signals = g_signal_group_new (G_TYPE_SETTINGS);
    g_signal_group_connect (shell_settings_signals,
                            "changed::" G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING,
                            G_CALLBACK (g_paste_settings_shell_settings_changed),
                            self);

    if (g_paste_util_has_gnome_shell ())
    {
        self->shell_settings = g_settings_new (G_PASTE_SHELL_SETTINGS_NAME);
        g_signal_group_set_target (shell_settings_signals, self->shell_settings);
        g_paste_settings_private_set_extension_enabled_from_dconf (self);
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
