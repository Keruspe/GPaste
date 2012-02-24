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

#define PRIMARY_TO_HISTORY_KEY         "primary-to-history"
#define MAX_HISTORY_SIZE_KEY           "max-history-size"
#define MAX_DISPLAYED_HISTORY_SIZE_KEY "max-displayed-history-size"
#define SYNCHRONIZE_CLIPBOARDS_KEY     "synchronize-clipboards"
#define TRACK_CHANGES_KEY              "track-changes"
#define TRACK_EXTENTION_STATE_KEY      "track-extension-state"
#define SAVE_HISTORY_KEY               "save-history"
#define TRIM_ITEMS_KEY                 "trim-items"
#define KEYBOARD_SHORTCUT_KEY          "keyboard-shortcut"
#define MAX_TEXT_ITEM_SIZE_KEY         "max-text-item-size"
#define MIN_TEXT_ITEM_SIZE_KEY         "min-text-item-size"

G_DEFINE_TYPE (GPasteSettings, g_paste_settings, G_TYPE_OBJECT)

struct _GPasteSettingsPrivate
{
    GSettings *settings;
    gboolean   primary_to_history;
    guint      max_history_size;
    guint      max_displayed_history_size;
    gboolean   synchronize_clipboards;
    gboolean   track_changes;
    gboolean   track_extension_state;
    gboolean   save_history;
    gboolean   trim_items;
    gchar     *keyboard_shortcut;
    guint      max_text_item_size;
    guint      min_text_item_size;
};

enum
{
    REBIND,
    TRACK,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
g_paste_settings_set_primary_to_history_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->primary_to_history = g_settings_get_boolean (priv->settings, PRIMARY_TO_HISTORY_KEY);
}

/**
 * g_paste_settings_get_primary_to_history:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_max_history_size_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->max_history_size = g_settings_get_uint (priv->settings, MAX_HISTORY_SIZE_KEY);
}

/**
 * g_paste_settings_get_max_history_size:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_max_displayed_history_size_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->max_displayed_history_size = g_settings_get_uint (priv->settings, MAX_DISPLAYED_HISTORY_SIZE_KEY);
}

/**
 * g_paste_settings_get_max_displayed_history_size:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_min_text_item_size_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->min_text_item_size = g_settings_get_uint (priv->settings, MIN_TEXT_ITEM_SIZE_KEY);
}

/**
 * g_paste_settings_get_min_text_item_size:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_max_text_item_size_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->max_text_item_size = g_settings_get_uint (priv->settings, MAX_TEXT_ITEM_SIZE_KEY);
}

/**
 * g_paste_settings_get_max_text_item_size:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_synchronize_clipboards_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->synchronize_clipboards = g_settings_get_boolean (priv->settings, SYNCHRONIZE_CLIPBOARDS_KEY);
}

/**
 * g_paste_settings_get_synchronize_clipboards:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_track_changes_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->track_changes = g_settings_get_boolean (priv->settings, TRACK_CHANGES_KEY);
}

/**
 * g_paste_settings_set_tracking_state:
 * @self: a GPasteSettings instance
 * @value: new tracking state
 *
 * Change the tracking state
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_settings_set_tracking_state (GPasteSettings *self,
                                     gboolean        value)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->track_changes = value;
    g_settings_set_boolean (priv->settings, TRACK_CHANGES_KEY, value);
}

/**
 * g_paste_settings_get_track_changes:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_track_extension_state_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->track_extension_state = g_settings_get_boolean (priv->settings, TRACK_EXTENTION_STATE_KEY);
}

/**
 * g_paste_settings_get_track_extension_state:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_save_history_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->save_history = g_settings_get_boolean (priv->settings, SAVE_HISTORY_KEY);
}

/**
 * g_paste_settings_get_save_history:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_trim_items_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->trim_items = g_settings_get_boolean (priv->settings, TRIM_ITEMS_KEY);
}

/**
 * g_paste_settings_get_trim_items:
 * @self: a GPasteSettings instance
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
g_paste_settings_set_keyboard_shortcut_from_dconf (GPasteSettings *self)
{
    g_return_if_fail (G_PASTE_IS_SETTINGS (self));

    GPasteSettingsPrivate *priv = self->priv;

    priv->keyboard_shortcut = g_settings_get_string (priv->settings, KEYBOARD_SHORTCUT_KEY);
}

/**
 * g_paste_settings_get_keyboard_shortcut:
 * @self: a GPasteSettings instance
 *
 * Get the KEYBOARD_SHORTCUT_KEY setting
 *
 * Returns: the value of the KEYBOARD_SHORTCUT_KEY setting
 */
G_PASTE_VISIBLE const gchar *
g_paste_settings_get_keyboard_shortcut (GPasteSettings *self)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (self), 0);

    return self->priv->keyboard_shortcut;
}

static void
g_paste_settings_dispose (GObject *object)
{
    GPasteSettingsPrivate *priv = G_PASTE_SETTINGS (object)->priv;

    g_object_unref (priv->settings);

    G_OBJECT_CLASS (g_paste_settings_parent_class)->dispose (object);
}

static void
g_paste_settings_class_init (GPasteSettingsClass *klass)
{
    g_type_class_add_private (klass, sizeof (GPasteSettingsPrivate));

    G_OBJECT_CLASS (klass)->dispose = g_paste_settings_dispose;

    signals[REBIND] = g_signal_new ("rebind",
                                    G_PASTE_TYPE_SETTINGS,
                                    G_SIGNAL_RUN_LAST,
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
g_paste_settings_init (GPasteSettings *self)
{
    self->priv = G_PASTE_SETTINGS_GET_PRIVATE (self);
}

static void
g_paste_settings_settings_changed (GSettings   *settings G_GNUC_UNUSED,
                                   const gchar *key,
                                   gpointer     user_data)
{
    GPasteSettings *self = G_PASTE_SETTINGS (user_data);
    GPasteSettingsPrivate *priv = self->priv;

    if (g_strcmp0 (key, PRIMARY_TO_HISTORY_KEY ) == 0)
        g_paste_settings_set_primary_to_history_from_dconf (self);
    else if (g_strcmp0 (key, MAX_HISTORY_SIZE_KEY) == 0)
        g_paste_settings_set_max_history_size_from_dconf (self);
    else if (g_strcmp0 (key, MAX_TEXT_ITEM_SIZE_KEY) == 0)
        g_paste_settings_set_max_text_item_size_from_dconf (self);
    else if (g_strcmp0 (key, MIN_TEXT_ITEM_SIZE_KEY) == 0)
        g_paste_settings_set_min_text_item_size_from_dconf (self);
    else if (g_strcmp0 (key, MAX_DISPLAYED_HISTORY_SIZE_KEY) == 0)
        g_paste_settings_set_max_displayed_history_size_from_dconf (self);
    else if (g_strcmp0 (key, SYNCHRONIZE_CLIPBOARDS_KEY) == 0)
        g_paste_settings_set_synchronize_clipboards_from_dconf (self);
    else if (g_strcmp0 (key, TRACK_CHANGES_KEY) == 0)
    {
        g_paste_settings_set_track_changes_from_dconf (self);
        g_signal_emit (self,
                       signals[TRACK],
                       0, /* detail */
                       priv->track_changes);
    }
    else if (g_strcmp0 (key, TRACK_EXTENTION_STATE_KEY) == 0)
        g_paste_settings_set_track_extension_state_from_dconf (self);
    else if (g_strcmp0 (key, SAVE_HISTORY_KEY) == 0)
        g_paste_settings_set_save_history_from_dconf (self);
    else if (g_strcmp0 (key, TRIM_ITEMS_KEY) == 0)
        g_paste_settings_set_trim_items_from_dconf (self);
    else if (g_strcmp0 (key, KEYBOARD_SHORTCUT_KEY) == 0)
    {
        g_paste_settings_set_keyboard_shortcut_from_dconf (self);
        g_signal_emit (self,
                       signals[REBIND],
                       0, /* detail */
                       priv->keyboard_shortcut);
    }
}

/**
 * g_paste_settings_new:
 *
 * Create a new instance of GPasteSettings
 *
 * Returns: a newly allocated GPasteSettings
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteSettings *
g_paste_settings_new (void)
{
    GPasteSettings *self = g_object_new (G_PASTE_TYPE_SETTINGS, NULL);

    GSettings *settings = self->priv->settings = g_settings_new ("org.gnome.GPaste");

    g_paste_settings_set_primary_to_history_from_dconf (self);
    g_paste_settings_set_max_history_size_from_dconf (self);
    g_paste_settings_set_max_displayed_history_size_from_dconf (self);
    g_paste_settings_set_synchronize_clipboards_from_dconf (self);
    g_paste_settings_set_track_changes_from_dconf (self);
    g_paste_settings_set_track_extension_state_from_dconf (self);
    g_paste_settings_set_save_history_from_dconf (self);
    g_paste_settings_set_trim_items_from_dconf (self);
    g_paste_settings_set_keyboard_shortcut_from_dconf (self);
    g_paste_settings_set_max_text_item_size_from_dconf(self);
    g_paste_settings_set_min_text_item_size_from_dconf(self);

    g_signal_connect (G_OBJECT (settings),
                      "changed",
                      G_CALLBACK (g_paste_settings_settings_changed),
                      self);

    return self;
}
