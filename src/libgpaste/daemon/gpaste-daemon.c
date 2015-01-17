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

#include "gpaste-daemon-private.h"

#include <gpaste-gdbus-defines.h>
#include <gpaste-password-item.h>
#include <gpaste-screensaver-client.h>

#include <string.h>

#define DEFAULT_HISTORY "history"

#define G_PASTE_SEND_DBUS_SIGNAL_FULL(sig,data,error)               \
    g_dbus_connection_emit_signal (priv->connection,                \
                                   NULL, /* destination_bus_name */ \
                                   priv->object_path,               \
                                   G_PASTE_DAEMON_BUS_NAME,         \
                                   G_PASTE_DAEMON_SIG_##sig,        \
                                   data,                            \
                                   error)

#define __NODATA     g_variant_new_tuple (NULL,  0)
#define __DATA(data) g_variant_new_tuple (&data, 1)

#define G_PASTE_SEND_DBUS_SIGNAL(sig)             G_PASTE_SEND_DBUS_SIGNAL_FULL(sig, __NODATA,  NULL)
#define G_PASTE_SEND_DBUS_SIGNAL_WITH_ERROR(sig)  G_PASTE_SEND_DBUS_SIGNAL_FULL(sig, __NODATA,  error)
#define G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA(sig,d) G_PASTE_SEND_DBUS_SIGNAL_FULL(sig, __DATA(d), NULL)

#define NEW_SIGNAL(name) \
    g_signal_new (name, \
                  G_PASTE_TYPE_DAEMON,           \
                  G_SIGNAL_RUN_LAST,             \
                  0, /* class offset */          \
                  NULL, /* accumulator */        \
                  NULL, /* accumulator data */   \
                  g_cclosure_marshal_VOID__VOID, \
                  G_TYPE_NONE,                   \
                  0)

#define LICENSE                                                            \
    "GPaste is free software: you can redistribute it and/or modify"       \
    "it under the terms of the GNU General Public License as published by" \
    "the Free Software Foundation, either version 3 of the License, or"    \
    "(at your option) any later version.\n\n"                              \
    "GPaste is distributed in the hope that it will be useful,"            \
    "but WITHOUT ANY WARRANTY; without even the implied warranty of"       \
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the"        \
    "GNU General Public License for more details.\n\n"                     \
    "You should have received a copy of the GNU General Public License"    \
    "along with GPaste.  If not, see <http://www.gnu.org/licenses/>."

enum
{
    C_UPDATE,
    C_NAME_LOST,
    C_REEXECUTE_SELF,
    C_TRACK,
    C_ACTIVE_CHANGED,

    C_LAST_SIGNAL
};

struct _GPasteDaemonPrivate
{
    GDBusConnection         *connection;
    gchar                   *object_path;
    guint                    id_on_bus;
    GError                  *inner_error;
    GPasteHistory           *history;
    GPasteSettings          *settings;
    GPasteClipboardsManager *clipboards_manager;
    GPasteKeybinder         *keybinder;
    GPasteScreensaverClient *screensaver;
    GDBusNodeInfo           *g_paste_daemon_dbus_info;
    GDBusInterfaceVTable     g_paste_daemon_dbus_vtable;

    gulong                   c_signals[C_LAST_SIGNAL];
};

G_DEFINE_TYPE_WITH_PRIVATE (GPasteDaemon, g_paste_daemon, G_TYPE_OBJECT)

enum
{
    NAME_LOST,
    REEXECUTE_SELF,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static gchar *
g_paste_daemon_get_dbus_string_parameter (GVariant *parameters,
                                          gsize    *length)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant = g_variant_iter_next_value (&parameters_iter);
    return g_variant_dup_string (variant, length);
}

static void
g_paste_daemon_get_dbus_strings_parameter (GVariant *parameters,
                                           gchar   **str1,
                                           gchar   **str2)
{
    GVariantIter parameters_iter;
    gsize length;

    g_variant_iter_init (&parameters_iter, parameters);

    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant1 = g_variant_iter_next_value (&parameters_iter);
    *str1 = g_variant_dup_string (variant1, &length);
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant2 = g_variant_iter_next_value (&parameters_iter);
    *str2 = g_variant_dup_string (variant2, &length);
}

static guint32
g_paste_daemon_get_dbus_uint32_parameter (GVariant *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant = g_variant_iter_next_value (&parameters_iter);
    return g_variant_get_uint32 (variant);
}

/****************/
/* DBus Signals */
/****************/
    
static void
g_paste_daemon_update (GPasteDaemon      *self,
                       GPasteUpdateAction action,
                       GPasteUpdateTarget target,
                       guint              position)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    GVariant *data[] = {
        g_variant_new_string (g_enum_get_value (g_type_class_peek (G_PASTE_TYPE_UPDATE_ACTION), action)->value_nick),
        g_variant_new_string (g_enum_get_value (g_type_class_peek (G_PASTE_TYPE_UPDATE_TARGET), target)->value_nick),
        g_variant_new_uint32 (position)
    };
    G_PASTE_SEND_DBUS_SIGNAL_FULL (UPDATE, g_variant_new_tuple (data, 3), NULL);

    /* TODO: legacy compat, remove me later */
    G_PASTE_SEND_DBUS_SIGNAL (CHANGED);
}

static void
g_paste_daemon_name_lost (GPasteDaemon *self,
                          gpointer      user_data G_GNUC_UNUSED)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    G_PASTE_SEND_DBUS_SIGNAL (NAME_LOST);
}

static void
g_paste_daemon_reexecute_self (GPasteDaemon *self,
                               gpointer      user_data G_GNUC_UNUSED)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    G_PASTE_SEND_DBUS_SIGNAL (REEXECUTE_SELF);
}

/**
 * g_paste_daemon_show_history:
 * @self: (transfer none): the #GPasteDaemon
 * @error: a #GError
 *
 * Emit the signal to show history
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_daemon_show_history (GPasteDaemon *self,
                             GError      **error)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_ERROR (SHOW_HISTORY);
}

static void
g_paste_daemon_tracking (GPasteDaemon   *self,
                         gboolean        tracking_state,
                         GPasteSettings *settings G_GNUC_UNUSED)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    GVariant *variant = g_variant_new_boolean (tracking_state);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA (TRACKING, variant);
}

/****************/
/* DBus Mathods */
/****************/
    
static void
g_paste_daemon_show_about_dialog (void)
{
    const gchar *_authors[] = {
        "Marc-Antoine Perennou <Marc-Antoine@Perennou.com>",
        NULL
    };
    G_PASTE_CLEANUP_B_STRV_FREE GStrv authors = g_boxed_copy (G_TYPE_STRV, _authors);
    gtk_show_about_dialog (NULL,
                           "program-name",   PACKAGE_NAME,
                           "version",        PACKAGE_VERSION,
                           "logo-icon-name", "gtk-paste",
                           "license",        LICENSE,
                           "authors",        authors,
                           "copyright",      "Copyright © 2010-2014 Marc-Antoine Perennou",
                           "comments",       "Clipboard management system",
                           "website",        "http://www.imagination-land.org/tags/GPaste.html",
                           "website-label",  "Follow GPaste news",
                           "wrap-license",   TRUE,
                           NULL);
}

static void
g_paste_daemon_private_do_add (GPasteDaemonPrivate *priv,
                               gchar               *text,
                               gsize                length)
{
    g_return_if_fail (text);

    GPasteSettings *settings = priv->settings;
    G_PASTE_CLEANUP_FREE gchar *stripped = g_strstrip (g_strdup (text));

    if (length >= g_paste_settings_get_min_text_item_size (settings) &&
        length <= g_paste_settings_get_max_text_item_size (settings) &&
        strlen (stripped) != 0)
    {
        GPasteItem *item = g_paste_text_item_new (g_paste_settings_get_trim_items (settings) ? stripped : text);
        g_paste_history_add (priv->history, item);
        g_paste_clipboards_manager_select (priv->clipboards_manager, item);
    }
}

static void
g_paste_daemon_private_add (GPasteDaemonPrivate *priv,
                            GVariant            *parameters)
{
    gsize length;
    G_PASTE_CLEANUP_FREE gchar *text = g_paste_daemon_get_dbus_string_parameter (parameters, &length);

    g_paste_daemon_private_do_add (priv, text, length);
}

static void
g_paste_daemon_private_add_file (GPasteDaemonPrivate *priv,
                                 GVariant            *parameters)
{
    gsize length;
    G_PASTE_CLEANUP_FREE gchar *file = g_paste_daemon_get_dbus_string_parameter (parameters, &length);

    g_return_if_fail (file);

    G_PASTE_CLEANUP_FREE gchar *content = NULL;

    if (g_file_get_contents (file,
                             &content,
                             &length,
                             NULL)) /* error */
    {
        g_paste_daemon_private_do_add (priv, content, length);
    }
}

static void
g_paste_daemon_private_add_password (GPasteDaemonPrivate *priv,
                                     GVariant            *parameters)
{
    G_PASTE_CLEANUP_FREE gchar *name = NULL;
    G_PASTE_CLEANUP_FREE gchar *password = NULL;

    g_paste_daemon_get_dbus_strings_parameter (parameters, &name, &password);

    if (!name || !password)
        return;

    /* FIXME: do we want to check size here? */
    GPasteItem *item = g_paste_password_item_new (name, password);
    /* TODO: split */
    g_paste_history_add (priv->history, item);
    g_paste_clipboards_manager_select (priv->clipboards_manager, item);
}

static void
g_paste_daemon_private_backup_history (GPasteDaemonPrivate *priv,
                                       GVariant            *parameters)
{
    G_PASTE_CLEANUP_FREE gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    g_return_if_fail (name);

    GPasteSettings *settings = priv->settings;

    G_PASTE_CLEANUP_FREE gchar *old_name = g_strdup (g_paste_settings_get_history_name (settings));

    g_paste_settings_set_history_name (settings, name);
    g_paste_history_save (priv->history);
    g_paste_settings_set_history_name (settings, old_name);
}

static void
g_paste_daemon_private_delete (GPasteDaemonPrivate *priv,
                               GVariant            *parameters)
{
    g_paste_history_remove (priv->history,
                            g_paste_daemon_get_dbus_uint32_parameter (parameters));
}

static void
g_paste_daemon_private_delete_history (GPasteDaemonPrivate *priv,
                                       GVariant            *parameters)
{
    G_PASTE_CLEANUP_FREE gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    g_return_if_fail (name);

    GPasteHistory *history = priv->history;

    G_PASTE_CLEANUP_FREE gchar *old_history = g_strdup (g_paste_settings_get_history_name (priv->settings));
    gboolean delete_current = !g_strcmp0 (name, old_history);

    if (!delete_current)
        g_paste_history_switch (history, name);
    g_paste_history_delete (history, NULL);
    g_paste_history_switch (history, (delete_current) ? DEFAULT_HISTORY : old_history);
}

static void
g_paste_daemon_private_delete_password (GPasteDaemonPrivate *priv,
                                        GVariant            *parameters)
{
    G_PASTE_CLEANUP_FREE gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    g_return_if_fail (name);

    g_paste_history_delete_password (priv->history, name);
}

static void
g_paste_daemon_private_empty (GPasteDaemonPrivate *priv)
{
    g_paste_history_empty (priv->history);
}

static GVariant *
g_paste_daemon_private_get_element (GPasteDaemonPrivate *priv,
                                    GVariant            *parameters)
{
    const gchar *value = g_paste_history_get_display_string (priv->history,
                                                             g_paste_daemon_get_dbus_uint32_parameter (parameters));
    // FIXME: error on NULL
    GVariant *variant = g_variant_new_string ((value) ? value : "");

    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_private_get_history (GPasteDaemonPrivate *priv)
{
    const GSList *history = g_paste_history_get_history (priv->history);
    guint length = g_slist_length ((GSList *) history);
    G_PASTE_CLEANUP_FREE const gchar **displayed_history = g_new (const gchar *, length + 1);

    for (guint i = 0; i < length; ++i, history = g_slist_next (history))
        displayed_history[i] = g_paste_item_get_display_string (history->data);
    displayed_history[length] = NULL;

    GVariant *variant = g_variant_new_strv ((const gchar * const *) displayed_history, -1);

    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_private_get_history_size (GPasteDaemonPrivate *priv)
{
    GVariant *variant = g_variant_new_uint32 (g_paste_history_get_length (priv->history));
    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_private_get_raw_element (GPasteDaemonPrivate *priv,
                                        GVariant            *parameters)
{
    const gchar *value = g_paste_history_get_value (priv->history,
                                                    g_paste_daemon_get_dbus_uint32_parameter (parameters));
    // FIXME: error on NULL
    GVariant *variant = g_variant_new_string ((value) ? value : "");

    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_private_get_raw_history (GPasteDaemonPrivate *priv)
{
    const GSList *history = g_paste_history_get_history (priv->history);
    guint length = g_slist_length ((GSList *) history);
    G_PASTE_CLEANUP_FREE const gchar **displayed_history = g_new (const gchar *, length + 1);

    for (guint i = 0; i < length; ++i, history = g_slist_next (history))
        displayed_history[i] = g_paste_item_get_value (history->data);
    displayed_history[length] = NULL;

    GVariant *variant = g_variant_new_strv ((const gchar * const *) displayed_history, -1);

    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_list_histories (void)
{
    G_PASTE_CLEANUP_STRFREEV GStrv history_names = g_paste_history_list (NULL);
    GVariant *variant = g_variant_new_strv ((const gchar * const *) history_names, -1);

    return g_variant_new_tuple (&variant, 1);
}

static void
g_paste_daemon_track (GPasteDaemon *self,
                      GVariant     *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant = g_variant_iter_next_value (&parameters_iter);
    gboolean tracking_state = g_variant_get_boolean (variant);

    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    g_paste_settings_set_track_changes (priv->settings, tracking_state);
    g_paste_daemon_tracking (self, tracking_state, NULL);
}

static void
g_paste_daemon_on_extension_state_changed (GPasteDaemon *self,
                                           GVariant     *parameters)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    if (g_paste_settings_get_track_extension_state (priv->settings))
        g_paste_daemon_track (self, parameters);
}

static void
g_paste_daemon_reexecute (GPasteDaemon *self)
{
    g_signal_emit (self,
                   signals[REEXECUTE_SELF],
                   0, /* detail */
                   NULL);
}

static void
g_paste_daemon_private_rename_password (GPasteDaemonPrivate *priv,
                                        GVariant            *parameters)
{
    G_PASTE_CLEANUP_FREE gchar *old_name = NULL;
    G_PASTE_CLEANUP_FREE gchar *new_name = NULL;

    g_paste_daemon_get_dbus_strings_parameter (parameters, &old_name, &new_name);

    if (!old_name)
        return;

    g_paste_history_rename_password (priv->history, old_name, new_name);
}

static GVariant *
g_paste_daemon_private_search (GPasteDaemonPrivate *priv,
                               GVariant            *parameters)
{
    G_PASTE_CLEANUP_ARRAY_FREE GArray *results = g_paste_history_search (priv->history,
                                                                         g_paste_daemon_get_dbus_string_parameter (parameters, NULL));
    GVariant *variant = g_variant_new_fixed_array (G_VARIANT_TYPE_UINT32, results->data, results->len, sizeof (guint32));

    return g_variant_new_tuple (&variant, 1);
}

static void
g_paste_daemon_private_select (GPasteDaemonPrivate *priv,
                               GVariant            *parameters)
{
    g_paste_history_select (priv->history,
                            g_paste_daemon_get_dbus_uint32_parameter (parameters));
}

static void
g_paste_daemon_private_set_password (GPasteDaemonPrivate *priv,
                                     GVariant            *parameters)
{
    GVariantIter parameters_iter;
    gsize length;

    g_variant_iter_init (&parameters_iter, parameters);

    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant1 = g_variant_iter_next_value (&parameters_iter);
    guint32 index = g_variant_get_uint32 (variant1);
    G_PASTE_CLEANUP_VARIANT_UNREF GVariant *variant2 = g_variant_iter_next_value (&parameters_iter);
    G_PASTE_CLEANUP_FREE gchar *name = g_variant_dup_string (variant2, &length);

    g_return_if_fail (name);

    g_paste_history_set_password (priv->history, index, name);
}

static void
g_paste_daemon_private_switch_history (GPasteDaemonPrivate *priv,
                                       GVariant            *parameters)
{
    G_PASTE_CLEANUP_FREE gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    g_return_if_fail (name);

    g_paste_history_switch (priv->history, name);
}

static void
g_paste_daemon_dbus_method_call (GDBusConnection       *connection     G_GNUC_UNUSED,
                                 const gchar           *sender         G_GNUC_UNUSED,
                                 const gchar           *object_path    G_GNUC_UNUSED,
                                 const gchar           *interface_name G_GNUC_UNUSED,
                                 const gchar           *method_name,
                                 GVariant              *parameters,
                                 GDBusMethodInvocation *invocation,
                                 gpointer               user_data)
{
    GPasteDaemon *self = user_data;
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    GVariant *answer = NULL;

    if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ABOUT))
        g_paste_daemon_show_about_dialog ();
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ADD))
        g_paste_daemon_private_add (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ADD_FILE))
        g_paste_daemon_private_add_file (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ADD_PASSWORD))
        g_paste_daemon_private_add_password (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_BACKUP_HISTORY))
        g_paste_daemon_private_backup_history (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_DELETE))
        g_paste_daemon_private_delete (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_DELETE_HISTORY))
        g_paste_daemon_private_delete_history (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_DELETE_PASSWORD))
        g_paste_daemon_private_delete_password (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_EMPTY))
        g_paste_daemon_private_empty (priv);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_ELEMENT))
        answer = g_paste_daemon_private_get_element (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_HISTORY))
        answer = g_paste_daemon_private_get_history (priv);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_HISTORY_SIZE))
        answer = g_paste_daemon_private_get_history_size (priv);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_RAW_ELEMENT))
        answer = g_paste_daemon_private_get_raw_element (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_RAW_HISTORY))
        answer = g_paste_daemon_private_get_raw_history (priv);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_LIST_HISTORIES))
        answer = g_paste_daemon_list_histories ();
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ON_EXTENSION_STATE_CHANGED))
        g_paste_daemon_on_extension_state_changed (self, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_REEXECUTE))
        g_paste_daemon_reexecute (self);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_RENAME_PASSWORD))
        g_paste_daemon_private_rename_password (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SEARCH))
        answer = g_paste_daemon_private_search (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SELECT))
        g_paste_daemon_private_select (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SET_PASSWORD))
        g_paste_daemon_private_set_password (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SHOW_HISTORY))
        g_paste_daemon_show_history (self, NULL /* FIXME: answer error */);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SWITCH_HISTORY))
        g_paste_daemon_private_switch_history (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_TRACK))
        g_paste_daemon_track (self, parameters);

    g_dbus_method_invocation_return_value (invocation, answer);
}

static GVariant *
g_paste_daemon_dbus_get_property (GDBusConnection *connection G_GNUC_UNUSED,
                                  const gchar     *sender G_GNUC_UNUSED,
                                  const gchar     *object_path G_GNUC_UNUSED,
                                  const gchar     *interface_name G_GNUC_UNUSED,
                                  const gchar     *property_name,
                                  GError         **error G_GNUC_UNUSED,
                                  gpointer         user_data)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (G_PASTE_DAEMON (user_data));

    if (!g_strcmp0 (property_name, G_PASTE_DAEMON_PROP_ACTIVE))
        return g_variant_new_boolean (g_paste_settings_get_track_changes (priv->settings));
    else if (!g_strcmp0 (property_name, G_PASTE_DAEMON_PROP_VERSION))
        return g_variant_new_string (VERSION);

    return NULL;
}

static void
g_paste_daemon_unregister_object (gpointer user_data)
{
    G_PASTE_CLEANUP_UNREF GPasteDaemon *self = G_PASTE_DAEMON (user_data);
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    gulong *c_signals = priv->c_signals;

    g_signal_handler_disconnect (self, c_signals[C_NAME_LOST]);
    g_signal_handler_disconnect (self, c_signals[C_REEXECUTE_SELF]);
    g_signal_handler_disconnect (priv->settings, c_signals[C_TRACK]);
    g_signal_handler_disconnect (priv->history,  c_signals[C_UPDATE]);

    if (priv->screensaver)
        g_signal_handler_disconnect (priv->screensaver,  c_signals[C_ACTIVE_CHANGED]);
}

static void
g_paste_daemon_on_history_update (GPasteDaemon      *self,
                                  GPasteUpdateAction action,
                                  GPasteUpdateTarget target,
                                  guint              position,
                                  gpointer           user_data G_GNUC_UNUSED)
{
    g_paste_daemon_update (self, action, target, position);
}

static void
g_paste_daemon_on_screensaver_active_changed (GPasteDaemonPrivate *priv,
                                              gboolean             active,
                                              gpointer             user_data G_GNUC_UNUSED)
{
    if (active)
    {
        G_PASTE_CLEANUP_UNREF GPasteItem *item = g_paste_text_item_new ("");
        g_paste_clipboards_manager_select (priv->clipboards_manager, item);
    }
    else
    {
        const GPasteItem *item = g_paste_history_get (priv->history, 0);

        if (item)
            g_paste_clipboards_manager_select (priv->clipboards_manager, item);
    }
}

static void
g_paste_daemon_register_object (GPasteDaemon    *self,
                                GDBusConnection *connection,
                                const gchar     *path)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    priv->connection = g_object_ref (connection);
    priv->object_path = g_strdup (path);

    if (!g_dbus_connection_register_object (connection,
                                            path,
                                            priv->g_paste_daemon_dbus_info->interfaces[0],
                                            &priv->g_paste_daemon_dbus_vtable,
                                            g_object_ref (self),
                                            g_paste_daemon_unregister_object,
                                            &priv->inner_error))
    {
        return;
    }

    gulong *c_signals = priv->c_signals;
    c_signals[C_NAME_LOST] = g_signal_connect (self,
                                              "name-lost",
                                               G_CALLBACK (g_paste_daemon_name_lost),
                                               NULL);
    c_signals[C_REEXECUTE_SELF] = g_signal_connect (self,
                                                    "reexecute-self",
                                                    G_CALLBACK (g_paste_daemon_reexecute_self),
                                                    NULL);
    c_signals[C_TRACK] = g_signal_connect_swapped (priv->settings,
                                                   "track",
                                                   G_CALLBACK (g_paste_daemon_tracking),
                                                   self);
    c_signals[C_UPDATE] = g_signal_connect_swapped (priv->history,
                                                    "update",
                                                    G_CALLBACK (g_paste_daemon_on_history_update),
                                                    self);

    if (priv->screensaver)
    {
        c_signals[C_ACTIVE_CHANGED] = g_signal_connect_swapped (priv->screensaver,
                                                                "active-changed",
                                                                G_CALLBACK (g_paste_daemon_on_screensaver_active_changed),
                                                                priv);
    }
}

static gboolean
_g_paste_daemon_changed (gpointer data)
{
    GPasteDaemon *self = G_PASTE_DAEMON (data);

    g_paste_daemon_update (self, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, 0);

    return G_SOURCE_REMOVE;
}

static void
g_paste_daemon_on_bus_acquired (GDBusConnection *connection,
                                const char      *name G_GNUC_UNUSED,
                                gpointer         user_data)
{
    GPasteDaemon *self = G_PASTE_DAEMON (user_data);

    g_paste_daemon_register_object (self,
                                    connection,
                                    G_PASTE_DAEMON_OBJECT_PATH);

    g_source_set_name_by_id (g_timeout_add_seconds (1, _g_paste_daemon_changed, user_data), "[GPaste] Startup - changed");
}

static void
g_paste_daemon_on_name_lost (GDBusConnection *connection G_GNUC_UNUSED,
                             const char      *name G_GNUC_UNUSED,
                             gpointer         user_data)
{
    g_signal_emit (G_PASTE_DAEMON (user_data),
                   signals[NAME_LOST],
                   0, /* detail */
                   NULL);
}


/**
 * g_paste_daemon_own_bus_name:
 * @self: (transfer none): the #GPasteDaemon
 * @error: a #GError
 *
 * Own the bus name
 *
 * Returns:
 */
G_PASTE_VISIBLE gboolean
g_paste_daemon_own_bus_name (GPasteDaemon *self,
                             GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_DAEMON (self), FALSE);

    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    g_return_val_if_fail (!priv->id_on_bus, FALSE);

    priv->inner_error = *error;
    priv->id_on_bus = g_bus_own_name (G_BUS_TYPE_SESSION,
                                      G_PASTE_DAEMON_BUS_NAME,
                                      G_BUS_NAME_OWNER_FLAGS_NONE,
                                      g_paste_daemon_on_bus_acquired,
                                      NULL, /* on_name_acquired */
                                      g_paste_daemon_on_name_lost,
                                      g_object_ref (self),
                                      g_object_unref);

    return (!priv->inner_error);
}

static void
g_paste_daemon_dispose (GObject *object)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (G_PASTE_DAEMON (object));

    if (priv->settings)
    {
        g_bus_unown_name (priv->id_on_bus);
        g_clear_object (&priv->connection);
        g_clear_object (&priv->history);
        g_clear_object (&priv->settings);
        g_clear_object (&priv->clipboards_manager);
        g_clear_object (&priv->keybinder);
        g_clear_object (&priv->screensaver);
        g_dbus_node_info_unref (priv->g_paste_daemon_dbus_info);
    }

    G_OBJECT_CLASS (g_paste_daemon_parent_class)->dispose (object);
}

static void
g_paste_daemon_finalize (GObject *object)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (G_PASTE_DAEMON (object));

    g_free (priv->object_path);

    G_OBJECT_CLASS (g_paste_daemon_parent_class)->finalize (object);
}

static void
g_paste_daemon_class_init (GPasteDaemonClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_daemon_dispose;
    object_class->finalize = g_paste_daemon_finalize;

    signals[NAME_LOST]      = NEW_SIGNAL ("name-lost");
    signals[REEXECUTE_SELF] = NEW_SIGNAL ("reexecute-self");
}

static void
g_paste_daemon_init (GPasteDaemon *self)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    GDBusInterfaceVTable *vtable = &priv->g_paste_daemon_dbus_vtable;

    priv->id_on_bus = 0;
    priv->g_paste_daemon_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_DAEMON_INTERFACE,
                                                                   NULL); /* Error */

    vtable->method_call = g_paste_daemon_dbus_method_call;
    vtable->get_property = g_paste_daemon_dbus_get_property;
    vtable->set_property = NULL;

    priv->screensaver = g_paste_screensaver_client_new_sync (NULL);
}

/**
 * g_paste_daemon_new:
 * @history: (transfer none): a #GPasteHistory
 * @settings: (transfer none): a #GPasteSettings
 * @clipboards_manager: (transfer none): a #GPasteClipboardsManager
 * @keybinder: (transfer none): a #GPasteKeybinder
 *
 * Create a new instance of #GPasteDaemon
 *
 * Returns: a newly allocated #GPasteDaemon
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteDaemon *
g_paste_daemon_new (GPasteHistory           *history,
                    GPasteSettings          *settings,
                    GPasteClipboardsManager *clipboards_manager,
                    GPasteKeybinder         *keybinder)
{
    g_return_val_if_fail (G_PASTE_IS_HISTORY (history), NULL);
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARDS_MANAGER (clipboards_manager), NULL);
    g_return_val_if_fail (G_PASTE_IS_KEYBINDER (keybinder), NULL);

    GPasteDaemon *self = g_object_new (G_PASTE_TYPE_DAEMON, NULL);
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);

    priv->history = g_object_ref (history);
    priv->settings = g_object_ref (settings);
    priv->clipboards_manager = g_object_ref (clipboards_manager);
    priv->keybinder = g_object_ref (keybinder);

    return self;
}
