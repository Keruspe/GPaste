/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#define G_PASTE_SETTINGS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), G_PASTE_TYPE_SETTINGS, GPasteSettingsPrivate))

#define TRACK_CHANGES_KEY              "track-changes"
#define TRACK_EXTENTION_STATE_KEY      "track-extension-state"
#define PRIMARY_TO_HISTORY_KEY         "primary-to-history"
#define SYNCHRONIZE_CLIPBOARDS_KEY     "synchronize-clipboards"
#define SAVE_HISTORY_KEY               "save-history"
#define TRIM_ITEMS_KEY                 "trim-items"
#define MAX_HISTORY_SIZE_KEY           "max-history-size"
#define MAX_DISPLAYED_HISTORY_SIZE_KEY "max-displayed-history-size"
#define ELEMENT_SIZE_KEY               "element-size"
#define MIN_TEXT_ITEM_SIZE_KEY         "min-text-item-size"
#define MAX_TEXT_ITEM_SIZE_KEY         "max-text-item-size"
#define SHOW_HISTORY_KEY               "show-history"
#define PASTE_AND_POP_KEY              "paste-and-pop"
#define FIFO_KEY              "fifo"

G_DEFINE_TYPE (GPasteSettings, g_paste_settings, G_TYPE_OBJECT)

struct _GPasteSettingsPrivate
{
    GSettings *settings;

    gboolean   track_changes;
    gboolean   track_extension_state;
    gboolean   primary_to_history;
    gboolean   synchronize_clipboards;
    gboolean   save_history;
    gboolean   trim_items;
    gboolean   fifo;
    guint      max_history_size;
    guint      max_displayed_history_size;
    guint      element_size;
    guint      min_text_item_size;
    guint      max_text_item_size;
    gchar     *show_history;
    gchar     *paste_and_pop;
};

enum
{
    CHANGED,
    REBIND,
    TRACK,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/**
 * g_paste_settings_get_track_changes:
 * @self: a #GPasteSettings instance
 *
 * Get the TRACK_CHANGES_KEY setting
 *
 * Returns: the value of the TRACK_CHANGES_KEY setting
 */
G_PASTE_VISIBLE gboolean
g_paste_settings_get_track_changes (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), FALSE);

    return self->priv->track_changes;
}

static void
g_paste_settings_set_track_changes_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->track_changes = g_settings_get_boolean (priv->settings, TRACK_CHANGES_KEY);
}

/**
 * g_paste_settings_set_track_changes:
 * @self: a #GPasteSettings instance
 * @value: whether to track or not the clipboard changes
 *
 * Change the TRACK_CHANGES_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_track_changes (GPasteSettings *self,
                                    gboolean        value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->track_changes = value;
    g_settings_set_boolean (priv->settings, TRACK_CHANGES_KEY, value);
}

/**
 * g_paste_settings_get_track_extension_state:
 * @self: a #GPasteSettings instance
 *
 * Get the TRACK_EXTENTION_STATE_KEY setting
 *
 * Returns: the value of the TRACK_EXTENTION_STATE_KEY setting
 */
G_PASTE_VISIBLE gboolean
g_paste_settings_get_track_extension_state (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), FALSE);

    return self->priv->track_extension_state;
}

static void
g_paste_settings_set_track_extension_state_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->track_extension_state = g_settings_get_boolean (priv->settings, TRACK_EXTENTION_STATE_KEY);
}

/**
 * g_paste_settings_set_track_extension_state:
 * @self: a #GPasteSettings instance
 * @value: whether to stop tracking or not the clipboard changes when an applet exits
 *
 * Change the TRACK_EXTENTION_STATE_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_track_extension_state (GPasteSettings *self,
                                            gboolean        value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->track_extension_state = value;
    g_settings_set_boolean (priv->settings, TRACK_EXTENTION_STATE_KEY, value);
}

/**
 * g_paste_settings_get_primary_to_history:
 * @self: a #GPasteSettings instance
 *
 * Get the PRIMARY_TO_HISTORY_KEY setting
 *
 * Returns: the value of the PRIMARY_TO_HISTORY_KEY setting
 */
G_PASTE_VISIBLE gboolean
g_paste_settings_get_primary_to_history (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), FALSE);

    return self->priv->primary_to_history;
}

static void
g_paste_settings_set_primary_to_history_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->primary_to_history = g_settings_get_boolean (priv->settings, PRIMARY_TO_HISTORY_KEY);
}

/**
 * g_paste_settings_set_primary_to_history:
 * @self: a #GPasteSettings instance
 * @value: whether to track or not the primary selection changes as clipboard ones
 *
 * Change the PRIMARY_TO_HISTORY_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_primary_to_history (GPasteSettings *self,
                                         gboolean        value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->primary_to_history = value;
    g_settings_set_boolean (priv->settings, PRIMARY_TO_HISTORY_KEY, value);
}

/**
 * g_paste_settings_get_synchronize_clipboards:
 * @self: a #GPasteSettings instance
 *
 * Get the SYNCHRONIZE_CLIPBOARDS_KEY setting
 *
 * Returns: the value of the SYNCHRONIZE_CLIPBOARDS_KEY setting
 */
G_PASTE_VISIBLE gboolean
g_paste_settings_get_synchronize_clipboards (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), FALSE);

    return self->priv->synchronize_clipboards;
}

static void
g_paste_settings_set_synchronize_clipboards_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->synchronize_clipboards = g_settings_get_boolean (priv->settings, SYNCHRONIZE_CLIPBOARDS_KEY);
}

/**
 * g_paste_settings_set_synchronize_clipboards:
 * @self: a #GPasteSettings instance
 * @value: whether to synchronize the clipboard and the primary selection or not
 *
 * Change the SYNCHRONIZE_CLIPBOARDS_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_synchronize_clipboards (GPasteSettings *self,
                                             gboolean        value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->synchronize_clipboards = value;
    g_settings_set_boolean (priv->settings, SYNCHRONIZE_CLIPBOARDS_KEY, value);
}

/**
 * g_paste_settings_get_save_history:
 * @self: a #GPasteSettings instance
 *
 * Get the SAVE_HISTORY_KEY setting
 *
 * Returns: the value of the SAVE_HISTORY_KEY setting
 */
G_PASTE_VISIBLE gboolean
g_paste_settings_get_save_history (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), FALSE);

    return self->priv->save_history;
}

static void
g_paste_settings_set_save_history_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->save_history = g_settings_get_boolean (priv->settings, SAVE_HISTORY_KEY);
}

/**
 * g_paste_settings_set_save_history:
 * @self: a #GPasteSettings instance
 * @value: whether to save or not the history
 *
 * Change the SAVE_HISTORY_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_save_history (GPasteSettings *self,
                                   gboolean        value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->save_history = value;
    g_settings_set_boolean (priv->settings, SAVE_HISTORY_KEY, value);
}

/**
 * g_paste_settings_get_trim_items:
 * @self: a #GPasteSettings instance
 *
 * Get the TRIM_ITEMS_KEY setting
 *
 * Returns: the value of the TRIM_ITEMS_KEY setting
 */
G_PASTE_VISIBLE gboolean
g_paste_settings_get_trim_items (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), FALSE);

    return self->priv->trim_items;
}

static void
g_paste_settings_set_trim_items_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->trim_items = g_settings_get_boolean (priv->settings, TRIM_ITEMS_KEY);
}

/**
 * g_paste_settings_set_trim_items:
 * @self: a #GPasteSettings instance
 * @value: whether to trim or not textual items
 *
 * Change the TRIM_ITEMS_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_trim_items (GPasteSettings *self,
                                 gboolean        value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->trim_items = value;
    g_settings_set_boolean (priv->settings, TRIM_ITEMS_KEY, value);
}

/**
 * g_paste_settings_get_fifo:
 * @self: a #GPasteSettings instance
 *
 * Get the FIFO_KEY setting
 *
 * Returns: the value of the FIFO_KEY setting
 */
G_PASTE_VISIBLE gboolean
g_paste_settings_get_fifo (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), FALSE);

    return self->priv->fifo;
}

static void
g_paste_settings_set_fifo_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->fifo = g_settings_get_boolean (priv->settings, FIFO_KEY);
}

/**
 * g_paste_settings_set_fifo:
 * @self: a #GPasteSettings instance
 * @value: whether to trim or not textual items
 *
 * Change the FIFO_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_fifo (GPasteSettings *self,
                                 gboolean        value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->fifo = value;
    g_settings_set_boolean (priv->settings, FIFO_KEY, value);
}

/**
 * g_paste_settings_get_max_history_size:
 * @self: a #GPasteSettings instance
 *
 * Get the MAX_HISTORY_SIZE_KEY setting
 *
 * Returns: the value of the MAX_HISTORY_SIZE_KEY setting
 */
G_PASTE_VISIBLE guint
g_paste_settings_get_max_history_size (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), 0);

    return self->priv->max_history_size;
}

static void
g_paste_settings_set_max_history_size_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->max_history_size = g_settings_get_uint (priv->settings, MAX_HISTORY_SIZE_KEY);
}

/**
 * g_paste_settings_set_max_history_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum number of items the history can contain
 *
 * Change the MAX_HISTORY_SIZE_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_max_history_size (GPasteSettings *self,
                                       guint           value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->max_history_size = value;
    g_settings_set_uint (priv->settings, MAX_HISTORY_SIZE_KEY, value);
}

/**
 * g_paste_settings_get_max_displayed_history_size:
 * @self: a #GPasteSettings instance
 *
 * Get the MAX_DISPLAYED_HISTORY_SIZE_KEY setting
 *
 * Returns: the value of the MAX_DISPLAYED_HISTORY_SIZE_KEY setting
 */
G_PASTE_VISIBLE guint
g_paste_settings_get_max_displayed_history_size (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), 0);

    return self->priv->max_displayed_history_size;
}

static void
g_paste_settings_set_max_displayed_history_size_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->max_displayed_history_size = g_settings_get_uint (priv->settings, MAX_DISPLAYED_HISTORY_SIZE_KEY);
}

/**
 * g_paste_settings_set_max_displayed_history_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum number of items to display
 *
 * Change the MAX_DISPLAYED_HISTORY_SIZE_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_max_displayed_history_size (GPasteSettings *self,
                                                 guint           value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->max_displayed_history_size = value;
    g_settings_set_uint (priv->settings, MAX_DISPLAYED_HISTORY_SIZE_KEY, value);
}

/**
 * g_paste_settings_get_element_size:
 * @self: a #GPasteSettings instance
 *
 * Get the ELEMENT_SIZE_KEY setting
 *
 * Returns: the value of the ELEMENT_SIZE_KEY setting
 */
G_PASTE_VISIBLE guint
g_paste_settings_get_element_size (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), 0);

    return self->priv->element_size;
}

static void
g_paste_settings_set_element_size_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->element_size = g_settings_get_uint (priv->settings, ELEMENT_SIZE_KEY);
}

/**
 * g_paste_settings_set_element_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum displayed size of an item
 *
 * Change the ELEMENT_SIZE_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_element_size (GPasteSettings *self,
                                   guint           value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->element_size = value;
    g_settings_set_uint (priv->settings, ELEMENT_SIZE_KEY, value);
}

/**
 * g_paste_settings_get_min_text_item_size:
 * @self: a #GPasteSettings instance
 *
 * Get the MIN_TEXT_ITEM_SIZE_KEY setting
 *
 * Returns: the value of the MIN_TEXT_ITEM_SIZE_KEY setting
 */
G_PASTE_VISIBLE guint
g_paste_settings_get_min_text_item_size (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), 0);

    return self->priv->min_text_item_size;
}

static void
g_paste_settings_set_min_text_item_size_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->min_text_item_size = g_settings_get_uint (priv->settings, MIN_TEXT_ITEM_SIZE_KEY);
}

/**
 * g_paste_settings_set_min_text_item_size:
 * @self: a #GPasteSettings instance
 * @value: the minimum size for a textual item to be handled
 *
 * Change the MIN_TEXT_ITEM_SIZE_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_min_text_item_size (GPasteSettings *self,
                                         guint           value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->min_text_item_size = value;
    g_settings_set_uint (priv->settings, MIN_TEXT_ITEM_SIZE_KEY, value);
}

/**
 * g_paste_settings_get_max_text_item_size:
 * @self: a #GPasteSettings instance
 *
 * Get the MAX_TEXT_ITEM_SIZE_KEY setting
 *
 * Returns: the value of the MAX_TEXT_ITEM_SIZE_KEY setting
 */
G_PASTE_VISIBLE guint
g_paste_settings_get_max_text_item_size (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), 0);

    return self->priv->max_text_item_size;
}

static void
g_paste_settings_set_max_text_item_size_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->max_text_item_size = g_settings_get_uint (priv->settings, MAX_TEXT_ITEM_SIZE_KEY);
}

/**
 * g_paste_settings_set_max_text_item_size:
 * @self: a #GPasteSettings instance
 * @value: the maximum size for a textual item to be handled
 *
 * Change the MAX_TEXT_ITEM_SIZE_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_max_text_item_size (GPasteSettings *self,
                                         guint           value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->max_text_item_size = value;
    g_settings_set_uint (priv->settings, MAX_TEXT_ITEM_SIZE_KEY, value);
}

/**
 * g_paste_settings_get_show_history:
 * @self: a #GPasteSettings instance
 *
 * Get the SHOW_HISTORY_KEY setting
 *
 * Returns: the value of the SHOW_HISTORY_KEY setting
 */
G_PASTE_VISIBLE const gchar *
g_paste_settings_get_show_history (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), 0);

    return self->priv->show_history;
}

static void
g_paste_settings_set_show_history_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    g_free (priv->show_history);
    priv->show_history = g_settings_get_string (priv->settings, SHOW_HISTORY_KEY);
}

/**
 * g_paste_settings_set_show_history:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the SHOW_HISTORY_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_show_history (GPasteSettings *self,
                                   const gchar    *value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));
    g_return_if_fail (value != NULL);

    GPasteSettingsPrivate *priv = self->priv;

    g_free (priv->show_history);
    priv->show_history = g_strdup (value);
    g_settings_set_string (priv->settings, SHOW_HISTORY_KEY, value);
}

/**
 * g_paste_settings_get_paste_and_pop:
 * @self: a #GPasteSettings instance
 *
 * Get the PASTE_AND_POP_KEY setting
 *
 * Returns: the value of the PASTE_AND_POP_KEY setting
 */
G_PASTE_VISIBLE const gchar *
g_paste_settings_get_paste_and_pop (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), 0);

    return self->priv->paste_and_pop;
}

static void
g_paste_settings_set_paste_and_pop_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    g_free (priv->paste_and_pop);
    priv->paste_and_pop = g_settings_get_string (priv->settings, PASTE_AND_POP_KEY);
}

/**
 * g_paste_settings_set_paste_and_pop:
 * @self: a #GPasteSettings instance
 * @value: the new keyboard shortcut
 *
 * Change the PASTE_AND_POP_KEY setting
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_paste_and_pop (GPasteSettings *self,
                                    const gchar    *value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));
    g_return_if_fail (value != NULL);

    GPasteSettingsPrivate *priv = self->priv;

    g_free (priv->paste_and_pop);
    priv->paste_and_pop = g_strdup (value);
    g_settings_set_string (priv->settings, PASTE_AND_POP_KEY, value);
}

static void
g_paste_settings_dispose (GObject *object)
{
    g_object_unref (G_PASTE_SETTINGS (object)->priv->settings);

    G_OBJECT_CLASS (g_paste_settings_parent_class)->dispose (object);
}

static void
g_paste_settings_finalize (GObject *object)
{
    GPasteSettingsPrivate *priv = G_PASTE_SETTINGS (object)->priv;

    g_free (priv->show_history);
    g_free (priv->paste_and_pop);

    G_OBJECT_CLASS (g_paste_settings_parent_class)->finalize (object);
}

static void
g_paste_settings_class_init (GPasteSettingsClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteSettingsPrivate));

    G_OBJECT_CLASS (klass)->dispose = g_paste_settings_dispose;
    G_OBJECT_CLASS (klass)->finalize = g_paste_settings_finalize;

    signals[CHANGED] = g_signal_new ("changed",
                                     G_PASTE_TYPE_SETTINGS,
                                     G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                     0, /* class offset */
                                     NULL, /* accumulator */
                                     NULL, /* accumulator data */
                                     g_cclosure_marshal_VOID__STRING,
                                     G_TYPE_NONE,
                                     1, /* number of params */
                                     G_TYPE_STRING | G_SIGNAL_TYPE_STATIC_SCOPE);
    signals[REBIND] = g_signal_new ("rebind",
                                    G_PASTE_TYPE_SETTINGS,
                                    G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                                    0, /* class offset */
                                    NULL, /* accumulator */
                                    NULL, /* accumulator data */
                                    g_cclosure_marshal_VOID__STRING,
                                    G_TYPE_NONE,
                                    1, /* number of params */
                                    G_TYPE_STRING);
    signals[TRACK] = g_signal_new ("track",
                                   G_PASTE_TYPE_SETTINGS,
                                   G_SIGNAL_RUN_LAST,
                                   0, /* class offset */
                                   NULL, /* accumulator */
                                   NULL, /* accumulator data */
                                   g_cclosure_marshal_VOID__BOOLEAN,
                                   G_TYPE_NONE,
                                   1, /* number of params */
                                   G_TYPE_BOOLEAN);
}

static void
g_paste_settings_settings_changed (GSettings   *settings G_GNUC_UNUSED,
                                   const gchar *key,
                                   gpointer     user_data)
{
    GPasteSettings *self = G_PASTE_SETTINGS (user_data);
    GPasteSettingsPrivate *priv = self->priv;

    if (g_strcmp0 (key, TRACK_CHANGES_KEY) == 0)
    {
        g_paste_settings_set_track_changes_from_dconf (self);
        g_signal_emit (self,
                       signals[TRACK],
                       0, /* detail */
                       priv->track_changes);
    }
    else if (g_strcmp0 (key, TRACK_EXTENTION_STATE_KEY) == 0)
        g_paste_settings_set_track_extension_state_from_dconf (self);
    else if (g_strcmp0 (key, PRIMARY_TO_HISTORY_KEY ) == 0)
        g_paste_settings_set_primary_to_history_from_dconf (self);
    else if (g_strcmp0 (key, SYNCHRONIZE_CLIPBOARDS_KEY) == 0)
        g_paste_settings_set_synchronize_clipboards_from_dconf (self);
    else if (g_strcmp0 (key, SAVE_HISTORY_KEY) == 0)
        g_paste_settings_set_save_history_from_dconf (self);
    else if (g_strcmp0 (key, TRIM_ITEMS_KEY) == 0)
        g_paste_settings_set_trim_items_from_dconf (self);
    else if (g_strcmp0 (key, FIFO_KEY) == 0)
        g_paste_settings_set_fifo_from_dconf (self);
    else if (g_strcmp0 (key, MAX_HISTORY_SIZE_KEY) == 0)
        g_paste_settings_set_max_history_size_from_dconf (self);
    else if (g_strcmp0 (key, MAX_DISPLAYED_HISTORY_SIZE_KEY) == 0)
        g_paste_settings_set_max_displayed_history_size_from_dconf (self);
    else if (g_strcmp0 (key, ELEMENT_SIZE_KEY) == 0)
        g_paste_settings_set_element_size_from_dconf (self);
    else if (g_strcmp0 (key, MIN_TEXT_ITEM_SIZE_KEY) == 0)
        g_paste_settings_set_min_text_item_size_from_dconf (self);
    else if (g_strcmp0 (key, MAX_TEXT_ITEM_SIZE_KEY) == 0)
        g_paste_settings_set_max_text_item_size_from_dconf (self);
    else if (g_strcmp0 (key, SHOW_HISTORY_KEY) == 0)
    {
        g_paste_settings_set_show_history_from_dconf (self);
        g_signal_emit (self,
                       signals[REBIND],
                       g_quark_from_string (SHOW_HISTORY_KEY),
                       G_PASTE_KEYBINDINGS_SHOW_HISTORY);
    }
    else if (g_strcmp0 (key, PASTE_AND_POP_KEY) == 0)
    {
        g_paste_settings_set_paste_and_pop_from_dconf (self);
        g_signal_emit (self,
                       signals[REBIND],
                       g_quark_from_string (SHOW_HISTORY_KEY),
                       G_PASTE_KEYBINDINGS_PASTE_AND_POP);
    }

    /* Forward the signal */
    g_signal_emit (self,
                   signals[CHANGED],
                   g_quark_from_string (key),
                   key);
}

static void
g_paste_settings_init (GPasteSettings *self)
{
    GPasteSettingsPrivate *priv = self->priv = G_PASTE_SETTINGS_GET_PRIVATE (self);
    GSettings *settings = priv->settings = g_settings_new ("org.gnome.GPaste");

    priv->show_history = NULL;
    priv->paste_and_pop = NULL;

    g_paste_settings_set_track_changes_from_dconf (self);
    g_paste_settings_set_track_extension_state_from_dconf (self);
    g_paste_settings_set_primary_to_history_from_dconf (self);
    g_paste_settings_set_synchronize_clipboards_from_dconf (self);
    g_paste_settings_set_save_history_from_dconf (self);
    g_paste_settings_set_trim_items_from_dconf (self);
    g_paste_settings_set_fifo_from_dconf (self);
    g_paste_settings_set_max_history_size_from_dconf (self);
    g_paste_settings_set_max_displayed_history_size_from_dconf (self);
    g_paste_settings_set_element_size_from_dconf (self);
    g_paste_settings_set_min_text_item_size_from_dconf(self);
    g_paste_settings_set_max_text_item_size_from_dconf(self);
    g_paste_settings_set_show_history_from_dconf (self);
    g_paste_settings_set_paste_and_pop_from_dconf (self);

    g_signal_connect (G_OBJECT (settings),
                      "changed",
                      G_CALLBACK (g_paste_settings_settings_changed),
                      self);
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
