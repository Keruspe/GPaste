/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#define __G_PASTE_NEEDS_AU__
#include <gpaste-gdbus-macros.h>
#include <gpaste-keybinder.h>
#include <gpaste-make-password-keybinding.h>
#include <gpaste-pop-keybinding.h>
#include <gpaste-screensaver-client.h>
#include <gpaste-show-history-keybinding.h>
#include <gpaste-sync-clipboard-to-primary-keybinding.h>
#include <gpaste-sync-primary-to-clipboard-keybinding.h>
#include <gpaste-ui-keybinding.h>
#include <gpaste-update-enums.h>
#include <gpaste-upload-keybinding.h>
#include <gpaste-util.h>

#include <string.h>

#define G_PASTE_SEND_DBUS_SIGNAL_FULL(sig,data,error)               \
    g_dbus_connection_emit_signal (priv->connection,                \
                                   NULL, /* destination_bus_name */ \
                                   G_PASTE_DAEMON_OBJECT_PATH,      \
                                   G_PASTE_DAEMON_INTERFACE_NAME,   \
                                   G_PASTE_DAEMON_SIG_##sig,        \
                                   data,                            \
                                   error)

#define __NODATA     g_variant_new_tuple (NULL,  0)
#define __DATA(data) g_variant_new_tuple (&data, 1)

#define G_PASTE_SEND_DBUS_SIGNAL(sig)             G_PASTE_SEND_DBUS_SIGNAL_FULL(sig, __NODATA,  NULL)
#define G_PASTE_SEND_DBUS_SIGNAL_WITH_ERROR(sig)  G_PASTE_SEND_DBUS_SIGNAL_FULL(sig, __NODATA,  error)
#define G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA(sig,d) G_PASTE_SEND_DBUS_SIGNAL_FULL(sig, __DATA(d), NULL)

#define G_PASTE_DBUS_ASSERT_FULL(cond, _msg, ret)          \
    do {                                                   \
        if (!(cond))                                       \
        {                                                  \
            *err = _err (G_PASTE_BUS_NAME ".Error", _msg); \
            return ret;                                    \
        }                                                  \
    } while (FALSE)

#define G_PASTE_DBUS_ASSERT(cond, _msg) G_PASTE_DBUS_ASSERT_FULL (cond, _msg, ;)

enum
{
    C_UPDATE,
    C_REEXECUTE_SELF,
    C_TRACK,
    C_ACTIVE_CHANGED,

    C_LAST_SIGNAL
};

struct _GPasteDaemon
{
    GPasteBusObject parent_instance;
};

typedef struct
{
    GDBusConnection         *connection;
    guint                    id_on_bus;
    gboolean                 registered;

    GPasteHistory           *history;
    GPasteSettings          *settings;
    GPasteClipboardsManager *clipboards_manager;
    GPasteKeybinder         *keybinder;
    GPasteScreensaverClient *screensaver;

    GDBusNodeInfo           *g_paste_daemon_dbus_info;
    GDBusInterfaceVTable     g_paste_daemon_dbus_vtable;

    gulong                   c_signals[C_LAST_SIGNAL];
} GPasteDaemonPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GPasteDaemon, g_paste_daemon, G_PASTE_TYPE_BUS_OBJECT)

enum
{
    REEXECUTE_SELF,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
    const gchar *name;
    const gchar *msg;
} GPasteDBusError;

static inline GPasteDBusError *
_err (const gchar *name,
      const gchar *msg)
{
    GPasteDBusError *err = g_malloc (sizeof (GPasteDBusError));
    err->name = name;
    err->msg = msg;
    return err;
}

static gchar *
g_paste_daemon_get_dbus_string_parameter (GVariant *parameters,
                                          gsize    *length)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
    return g_variant_dup_string (variant, length);
}

static void
_variant_iter_read_strings_parameter (GVariantIter *parameters_iter,
                                      gchar       **str1,
                                      gchar       **str2)
{
    gsize length;

    g_autoptr (GVariant) variant1 = g_variant_iter_next_value (parameters_iter);
    *str1 = g_variant_dup_string (variant1, &length);
    g_autoptr (GVariant) variant2 = g_variant_iter_next_value (parameters_iter);
    *str2 = g_variant_dup_string (variant2, &length);
}

static void
g_paste_daemon_get_dbus_strings_parameter (GVariant *parameters,
                                           gchar   **str1,
                                           gchar   **str2)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);
    _variant_iter_read_strings_parameter (&parameters_iter, str1, str2);
}

static guint32
g_paste_daemon_get_dbus_uint32_parameter (GVariant *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
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
g_paste_daemon_private_do_add_item (GPasteDaemonPrivate *priv,
                                    GPasteItem          *item)
{
    g_paste_history_add (priv->history, item);
    g_paste_clipboards_manager_select (priv->clipboards_manager, item);
}

static void
g_paste_daemon_private_do_add (GPasteDaemonPrivate *priv,
                               const gchar         *text,
                               gsize                length,
                               GPasteDBusError    **err)
{
    G_PASTE_DBUS_ASSERT (text && length, "no content to add");

    GPasteSettings *settings = priv->settings;
    g_autofree gchar *stripped = g_strstrip (g_strdup (text));

    if (length >= g_paste_settings_get_min_text_item_size (settings) &&
        length <= g_paste_settings_get_max_text_item_size (settings) &&
        strlen (stripped) != 0)
    {
        g_paste_daemon_private_do_add_item (priv,
                                            g_paste_text_item_new (g_paste_settings_get_trim_items (settings) ? stripped : text));
    }
}

static void
g_paste_daemon_private_add (GPasteDaemonPrivate *priv,
                            GVariant            *parameters,
                            GPasteDBusError    **err)
{
    gsize length;
    g_autofree gchar *text = g_paste_daemon_get_dbus_string_parameter (parameters, &length);

    g_paste_daemon_private_do_add (priv, text, length, err);
}

static void
g_paste_daemon_private_add_file (GPasteDaemonPrivate *priv,
                                 GVariant            *parameters,
                                 GError             **error,
                                 GPasteDBusError    **err)
{
    gsize length;
    g_autofree gchar *file = g_paste_daemon_get_dbus_string_parameter (parameters, &length);
    g_autofree gchar *content = NULL;

    G_PASTE_DBUS_ASSERT (file, "no file to add");

    if (g_file_get_contents (file,
                             &content,
                             &length,
                             error))
    {
        g_paste_daemon_private_do_add (priv, content, length, err);
    }
}

static void
g_paste_daemon_private_add_password (GPasteDaemonPrivate *priv,
                                     GVariant            *parameters,
                                     GPasteDBusError    **err)
{
    g_autofree gchar *name = NULL;
    g_autofree gchar *password = NULL;

    g_paste_daemon_get_dbus_strings_parameter (parameters, &name, &password);

    G_PASTE_DBUS_ASSERT (name && password, "no password to add");

    g_paste_daemon_private_do_add_item (priv,
                                        g_paste_password_item_new (name, password));
}

static void
g_paste_daemon_private_delete_history_signal (GPasteDaemonPrivate *priv,
                                              const gchar         *history)
{
    GVariant *variant = g_variant_new_string (history);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA (DELETE_HISTORY, variant);
}

static void
g_paste_daemon_private_switch_history_signal (GPasteDaemonPrivate *priv,
                                              const gchar         *history)
{
    GVariant *variant = g_variant_new_string (history);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA (SWITCH_HISTORY, variant);
}

static void
g_paste_daemon_private_backup_history (GPasteDaemonPrivate *priv,
                                       GVariant            *parameters,
                                       GPasteDBusError    **err)
{
    g_autofree gchar *history = NULL;
    g_autofree gchar *backup = NULL;

    g_paste_daemon_get_dbus_strings_parameter (parameters, &history, &backup);

    G_PASTE_DBUS_ASSERT (history && backup, "no history to backup");

    GPasteSettings *settings = priv->settings;

    g_autofree gchar *old_name = g_strdup (g_paste_settings_get_history_name (settings));

    /* FIXME: simplify things */
    g_paste_settings_set_history_name (settings, history);
    g_paste_history_load (priv->history);
    g_paste_daemon_private_switch_history_signal (priv, history);
    g_paste_settings_set_history_name (settings, backup);
    g_paste_history_save (priv->history);
    g_paste_daemon_private_switch_history_signal (priv, backup);
    g_paste_settings_set_history_name (settings, old_name);
    g_paste_history_load (priv->history);
    g_paste_daemon_private_switch_history_signal (priv, old_name);
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
                                       GVariant            *parameters,
                                       GPasteDBusError    **err)
{
    g_autofree gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    G_PASTE_DBUS_ASSERT (name, "no history to delete");

    GPasteHistory *history = priv->history;

    g_autofree gchar *old_history = g_strdup (g_paste_settings_get_history_name (priv->settings));
    gboolean delete_current = !g_strcmp0 (name, old_history);

    if (!delete_current)
        g_paste_history_switch (history, name);
    g_paste_history_delete (history, NULL);
    g_paste_daemon_private_delete_history_signal (priv, name);

    const gchar *new_history = (delete_current) ? DEFAULT_HISTORY : old_history;

    g_paste_history_switch (history, new_history);
    g_paste_daemon_private_switch_history_signal (priv, new_history);
}

static void
g_paste_daemon_private_delete_password (GPasteDaemonPrivate *priv,
                                        GVariant            *parameters,
                                        GPasteDBusError    **err)
{
    g_autofree gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    G_PASTE_DBUS_ASSERT (name, "no password to delete");

    g_paste_history_delete_password (priv->history, name);
}

static void
g_paste_daemon_private_empty (GPasteDaemonPrivate *priv)
{
    g_paste_history_empty (priv->history);
}

static GVariant *
g_paste_daemon_private_get_element (GPasteDaemonPrivate *priv,
                                    GVariant            *parameters,
                                    GPasteDBusError    **err)
{
    GPasteHistory *history = priv->history;
    guint32 index = g_paste_daemon_get_dbus_uint32_parameter (parameters);

    G_PASTE_DBUS_ASSERT_FULL (index < g_paste_history_get_length (history), "invalid index received", NULL);

    const gchar *value = g_paste_history_get_display_string (history, index);

    G_PASTE_DBUS_ASSERT_FULL (value, "received no value for this index", NULL);

    GVariant *variant = g_variant_new_string (value);

    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_private_get_element_kind (GPasteDaemonPrivate *priv,
                                         GVariant            *parameters,
                                         GPasteDBusError    **err)
{
    GPasteHistory *history = priv->history;
    guint32 index = g_paste_daemon_get_dbus_uint32_parameter (parameters);

    G_PASTE_DBUS_ASSERT_FULL (index < g_paste_history_get_length (history), "invalid index received", NULL);

    const GPasteItem *item = g_paste_history_get (history, index);

    G_PASTE_DBUS_ASSERT_FULL (item, "received no item for this index", NULL);

    GVariant *variant = g_variant_new_string (g_paste_item_get_kind (item));

    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_private_get_elements (GPasteDaemonPrivate *priv,
                                     GVariant            *parameters,
                                     GPasteDBusError    **err)
{
    GPasteHistory *history = priv->history;
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
    gsize len;
    g_autofree guint32 *indexes = g_paste_dbus_get_au_result (variant, &len);
    g_auto (GStrv) ans = g_new0 (gchar *, len + 1);
    gsize history_length = g_paste_history_get_length (history);

    for (gsize i = 0; i < len; ++i)
    {
        G_PASTE_DBUS_ASSERT_FULL (indexes[i] < history_length, "invalid index received", NULL);
        const gchar *value = g_paste_history_get_display_string (history, indexes[i]);
        G_PASTE_DBUS_ASSERT_FULL (value, "received no value for this index", NULL);
        ans[i] = g_strdup (value);
    }

    GVariant *answer = g_variant_new_strv ((const gchar * const *) ans, len);

    return g_variant_new_tuple (&answer, 1);
}

static GVariant *
g_paste_daemon_private_get_history (GPasteDaemonPrivate *priv)
{
    const GSList *history = g_paste_history_get_history (priv->history);
    guint length = g_slist_length ((GSList *) history);
    g_autofree const gchar **displayed_history = g_new (const gchar *, length + 1);

    for (guint i = 0; i < length; ++i, history = g_slist_next (history))
        displayed_history[i] = g_paste_item_get_display_string (history->data);
    displayed_history[length] = NULL;

    GVariant *variant = g_variant_new_strv ((const gchar * const *) displayed_history, -1);

    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_private_get_history_name (GPasteDaemonPrivate *priv)
{
    GVariant *variant = g_variant_new_string (g_paste_settings_get_history_name (priv->settings));
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
                                        GVariant            *parameters,
                                        GPasteDBusError    **err)
{
    GPasteHistory *history = priv->history;
    guint32 index = g_paste_daemon_get_dbus_uint32_parameter (parameters);

    G_PASTE_DBUS_ASSERT_FULL (index < g_paste_history_get_length (history), "invalid index received", NULL);

    const gchar *value = g_paste_history_get_value (priv->history, index);

    G_PASTE_DBUS_ASSERT_FULL (value, "received no value for this index", NULL);

    GVariant *variant = g_variant_new_string (value);

    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_private_get_raw_history (GPasteDaemonPrivate *priv)
{
    const GSList *history = g_paste_history_get_history (priv->history);
    guint length = g_slist_length ((GSList *) history);
    g_autofree const gchar **displayed_history = g_new (const gchar *, length + 1);

    for (guint i = 0; i < length; ++i, history = g_slist_next (history))
        displayed_history[i] = g_paste_item_get_value (history->data);
    displayed_history[length] = NULL;

    GVariant *variant = g_variant_new_strv ((const gchar * const *) displayed_history, -1);

    return g_variant_new_tuple (&variant, 1);
}

static GVariant *
g_paste_daemon_list_histories (GError **error)
{
    g_auto (GStrv) history_names = g_paste_history_list (error);

    if (!history_names)
        return NULL;

    GVariant *variant = g_variant_new_strv ((const gchar * const *) history_names, -1);

    return g_variant_new_tuple (&variant, 1);
}

static void
g_paste_daemon_private_merge (GPasteDaemonPrivate *priv,
                              GVariant            *parameters,
                              GPasteDBusError    **err)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autofree gchar *decoration = NULL;
    g_autofree gchar *separator  = NULL;

    _variant_iter_read_strings_parameter (&parameters_iter, &decoration, &separator);

    g_autoptr (GVariant) v_indexes = g_variant_iter_next_value (&parameters_iter);
    gsize length;
    const guint32 *indexes = g_variant_get_fixed_array (v_indexes, &length, sizeof (guint32));

    GPasteHistory *history = priv->history;
    gsize history_length = g_paste_history_get_length (history);

    G_PASTE_DBUS_ASSERT (length, "nothing to merge");
    for (gsize i = 0; i < length; ++i)
    {
        G_PASTE_DBUS_ASSERT (indexes[i] < history_length, "invalid index received");
    }

    G_PASTE_CLEANUP_STRING_FREE GString *str = g_string_new (NULL);

    for (gsize i = 0; i < length; ++i)
    {
        g_string_append_printf (str, "%s%s%s%s",
                                (i) ? separator : "",
                                decoration,
                                g_paste_history_get_value (history, indexes[i]),
                                decoration);
    }

    g_paste_daemon_private_do_add (priv, str->str, str->len, err);
}

static void
g_paste_daemon_track (GPasteDaemon *self,
                      GVariant     *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
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
                                        GVariant            *parameters,
                                        GPasteDBusError    **err)
{
    g_autofree gchar *old_name = NULL;
    g_autofree gchar *new_name = NULL;

    g_paste_daemon_get_dbus_strings_parameter (parameters, &old_name, &new_name);

    G_PASTE_DBUS_ASSERT (old_name, "no password to rename");

    g_paste_history_rename_password (priv->history, old_name, new_name);
}

static GVariant *
g_paste_daemon_private_search (GPasteDaemonPrivate *priv,
                               GVariant            *parameters)
{
    g_autoptr (GArray) results = g_paste_history_search (priv->history,
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
g_paste_daemon_private_replace (GPasteDaemonPrivate *priv,
                                GVariant            *parameters,
                                GPasteDBusError    **err)
{
    GVariantIter parameters_iter;
    gsize length;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant1 = g_variant_iter_next_value (&parameters_iter);
    guint32 index = g_variant_get_uint32 (variant1);
    g_autoptr (GVariant) variant2 = g_variant_iter_next_value (&parameters_iter);
    g_autofree gchar *contents = g_variant_dup_string (variant2, &length);

    G_PASTE_DBUS_ASSERT (contents, "no contents given");

    /* FIXME: check item type first */
    g_paste_history_replace (priv->history, index, contents);
}

static void
g_paste_daemon_private_set_password (GPasteDaemonPrivate *priv,
                                     GVariant            *parameters,
                                     GPasteDBusError    **err)
{
    GVariantIter parameters_iter;
    gsize length;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant1 = g_variant_iter_next_value (&parameters_iter);
    guint32 index = g_variant_get_uint32 (variant1);
    g_autoptr (GVariant) variant2 = g_variant_iter_next_value (&parameters_iter);
    g_autofree gchar *name = g_variant_dup_string (variant2, &length);

    G_PASTE_DBUS_ASSERT (name, "no password name given");

    /* FIXME: check item type first */
    g_paste_history_set_password (priv->history, index, name);
}

static void
g_paste_daemon_private_switch_history (GPasteDaemonPrivate *priv,
                                       GVariant            *parameters,
                                       GPasteDBusError    **err)
{
    g_autofree gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    G_PASTE_DBUS_ASSERT (name, "no history to switch to");

    g_paste_history_switch (priv->history, name);
    g_paste_daemon_private_switch_history_signal (priv, name);
}

static void
g_paste_daemon_private_upload_finish (GObject      *source_object,
                                      GAsyncResult *res,
                                      gpointer      user_data)
{
    g_autoptr (GSubprocess) upload = G_SUBPROCESS (source_object);
    g_autofree gchar *url = NULL;
    g_autofree GPasteDBusError *err = NULL;
    GPasteDaemonPrivate *priv = user_data;

    g_subprocess_communicate_utf8_finish (upload,
                                          res,
                                          &url,
                                          NULL, /* stderr */
                                          NULL); /* error */

    if (url)
        g_paste_daemon_private_do_add (priv, url, strlen (url), &err);
}

/**
 * g_paste_daemon_upload:
 * @self: (transfer none): the #GPasteDaemon
 * @index: the index of the item to upload
 *
 * Upload an item to a pastebin service
 *
 * Returns:
 */
G_PASTE_VISIBLE void
g_paste_daemon_upload (GPasteDaemon *self,
                       guint32       index)
{
    g_return_if_fail (G_PASTE_IS_DAEMON (self));

    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    GSubprocess *upload = g_subprocess_new (G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE, NULL, "wgetpaste", NULL);
    const gchar *value = g_paste_history_get_value (priv->history, index);

    g_subprocess_communicate_utf8_async (upload,
                                         value,
                                         NULL, /* cancellable */
                                         g_paste_daemon_private_upload_finish,
                                         priv);
}

static void
_g_paste_daemon_upload (GPasteDaemon *self,
                        GVariant     *parameters)
{
    g_paste_daemon_upload(self, g_paste_daemon_get_dbus_uint32_parameter (parameters));
}

static void
g_paste_daemon_activate_default_keybindings (GPasteDaemon *self)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    GPasteKeybinder *keybinder = priv->keybinder;
    GPasteHistory *history = priv->history;
    GPasteClipboardsManager *clipboards_manager = priv->clipboards_manager;
    GPasteKeybinding *keybindings[] = {
        g_paste_make_password_keybinding_new (history),
        g_paste_pop_keybinding_new (history),
        g_paste_show_history_keybinding_new (self),
        g_paste_sync_clipboard_to_primary_keybinding_new (clipboards_manager),
        g_paste_sync_primary_to_clipboard_keybinding_new (clipboards_manager),
        g_paste_ui_keybinding_new (),
        g_paste_upload_keybinding_new (self)
    };

    for (guint k = 0; k < G_N_ELEMENTS (keybindings); ++k)
        g_paste_keybinder_add_keybinding (keybinder, keybindings[k]);

    g_paste_keybinder_activate_all (keybinder);
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
    GError *error = NULL;
    g_autofree GPasteDBusError *err = NULL;

    if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ABOUT))
        g_paste_util_activate_ui ("about", NULL);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ADD))
        g_paste_daemon_private_add (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ADD_FILE))
        g_paste_daemon_private_add_file (priv, parameters, &error, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ADD_PASSWORD))
        g_paste_daemon_private_add_password (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_BACKUP_HISTORY))
        g_paste_daemon_private_backup_history (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_DELETE))
        g_paste_daemon_private_delete (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_DELETE_HISTORY))
        g_paste_daemon_private_delete_history (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_DELETE_PASSWORD))
        g_paste_daemon_private_delete_password (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_EMPTY))
        g_paste_daemon_private_empty (priv);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_ELEMENT))
        answer = g_paste_daemon_private_get_element (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_ELEMENT_KIND))
        answer = g_paste_daemon_private_get_element_kind (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_ELEMENTS))
        answer = g_paste_daemon_private_get_elements (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_HISTORY))
        answer = g_paste_daemon_private_get_history (priv);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_HISTORY_NAME))
        answer = g_paste_daemon_private_get_history_name (priv);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_HISTORY_SIZE))
        answer = g_paste_daemon_private_get_history_size (priv);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_RAW_ELEMENT))
        answer = g_paste_daemon_private_get_raw_element (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_GET_RAW_HISTORY))
        answer = g_paste_daemon_private_get_raw_history (priv);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_LIST_HISTORIES))
        answer = g_paste_daemon_list_histories (&error);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_MERGE))
        g_paste_daemon_private_merge (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_ON_EXTENSION_STATE_CHANGED))
        g_paste_daemon_on_extension_state_changed (self, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_REEXECUTE))
        g_paste_daemon_reexecute (self);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_RENAME_PASSWORD))
        g_paste_daemon_private_rename_password (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_REPLACE))
        g_paste_daemon_private_replace (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SEARCH))
        answer = g_paste_daemon_private_search (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SELECT))
        g_paste_daemon_private_select (priv, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SET_PASSWORD))
        g_paste_daemon_private_set_password (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SHOW_HISTORY))
        g_paste_daemon_show_history (self, &error);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_SWITCH_HISTORY))
        g_paste_daemon_private_switch_history (priv, parameters, &err);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_TRACK))
        g_paste_daemon_track (self, parameters);
    else if (!g_strcmp0 (method_name, G_PASTE_DAEMON_UPLOAD))
        _g_paste_daemon_upload (self, parameters);

    if (error)
        g_dbus_method_invocation_take_error (invocation, error);
    else if (err)
        g_dbus_method_invocation_return_dbus_error (invocation, err->name, err->msg);
    else
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
    g_autoptr (GPasteDaemon) self = G_PASTE_DAEMON (user_data);
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    gulong *c_signals = priv->c_signals;

    g_signal_handler_disconnect (self, c_signals[C_REEXECUTE_SELF]);
    g_signal_handler_disconnect (priv->settings, c_signals[C_TRACK]);
    g_signal_handler_disconnect (priv->history,  c_signals[C_UPDATE]);

    if (priv->screensaver)
        g_signal_handler_disconnect (priv->screensaver,  c_signals[C_ACTIVE_CHANGED]);

    priv->registered = FALSE;
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
    if (!priv->registered)
        return;

    /* The deactivate signal is always sent, but not the activate one */
    /* We always do the activate action, so that the deactivate one works anyways */
    {
        g_autoptr (GPasteItem) item = g_paste_text_item_new ("");
        g_paste_clipboards_manager_select (priv->clipboards_manager, item);
    }

    if (!active)
    {
        const GPasteItem *item = g_paste_history_get (priv->history, 0);

        if (item)
            g_paste_clipboards_manager_select (priv->clipboards_manager, item);
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
g_paste_daemon_dispose (GObject *object)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (G_PASTE_DAEMON (object));

    if (priv->settings)
    {
        g_dbus_connection_unregister_object (priv->connection, priv->id_on_bus);
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

static gboolean
g_paste_daemon_register_on_connection (GPasteBusObject *self,
                                       GDBusConnection *connection,
                                       GError         **error)
{
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (G_PASTE_DAEMON (self));

    g_clear_object (&priv->connection);
    priv->connection = g_object_ref (connection);

    priv->id_on_bus = g_dbus_connection_register_object (connection,
                                                         G_PASTE_DAEMON_OBJECT_PATH,
                                                         priv->g_paste_daemon_dbus_info->interfaces[0],
                                                         &priv->g_paste_daemon_dbus_vtable,
                                                         g_object_ref (self),
                                                         g_paste_daemon_unregister_object,
                                                         error);

    if (!priv->id_on_bus)
        return FALSE;

    gulong *c_signals = priv->c_signals;

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
    priv->registered = TRUE;

    g_source_set_name_by_id (g_timeout_add_seconds (1, _g_paste_daemon_changed, self), "[GPaste] Startup - changed");

    return TRUE;
}

static void
g_paste_daemon_class_init (GPasteDaemonClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_daemon_dispose;
    G_PASTE_BUS_OBJECT_CLASS (klass)->register_on_connection = g_paste_daemon_register_on_connection;

    /**
     * GPasteDaemon::reexecute-self:
     * @gpaste_daemon: the object on which the signal was emitted
     *
     * The "reexecute-self" signal is emitted when the daemon is about
     * to reexecute itself into a new freshly spawned daemon
     */
    signals[REEXECUTE_SELF] = g_signal_new ("reexecute-self",
                                            G_PASTE_TYPE_DAEMON,
                                            G_SIGNAL_RUN_LAST,
                                            0, /* class offset */
                                            NULL, /* accumulator */
                                            NULL, /* accumulator data */
                                            g_cclosure_marshal_VOID__VOID,
                                            G_TYPE_NONE,
                                            0);
}

static void
on_screensaver_client_ready (GObject      *source_object G_GNUC_UNUSED,
                             GAsyncResult *res,
                             gpointer      user_data)
{
    GPasteDaemonPrivate *priv = user_data;
    GPasteScreensaverClient *screensaver = priv->screensaver = g_paste_screensaver_client_new_finish (res, NULL);

    if (screensaver)
    {
        priv->c_signals[C_ACTIVE_CHANGED] = g_signal_connect_swapped (priv->screensaver,
                                                                      "active-changed",
                                                                      G_CALLBACK (g_paste_daemon_on_screensaver_active_changed),
                                                                      priv);
    }
}

static void
on_shell_client_ready (GObject      *source_object G_GNUC_UNUSED,
                       GAsyncResult *res,
                       gpointer      user_data)
{
    GPasteDaemon *self = user_data;
    GPasteDaemonPrivate *priv = g_paste_daemon_get_instance_private (self);
    g_autoptr (GPasteGnomeShellClient) shell_client = g_paste_gnome_shell_client_new_finish (res, NULL);

    g_paste_screensaver_client_new (on_screensaver_client_ready, priv);

    priv->keybinder = g_paste_keybinder_new (priv->settings, shell_client);
    g_paste_daemon_activate_default_keybindings (self);
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

    GPasteSettings *settings = priv->settings = g_paste_settings_new ();
    GPasteHistory *history = priv->history = g_paste_history_new (settings);
    GPasteClipboardsManager *clipboards_manager = priv->clipboards_manager = g_paste_clipboards_manager_new (history, settings);

    g_autoptr (GPasteClipboard) clipboard = g_paste_clipboard_new (GDK_SELECTION_CLIPBOARD, settings);
    g_autoptr (GPasteClipboard) primary = g_paste_clipboard_new (GDK_SELECTION_PRIMARY, settings);

    g_paste_clipboards_manager_add_clipboard (clipboards_manager, clipboard);
    g_paste_clipboards_manager_add_clipboard (clipboards_manager, primary);
    g_paste_clipboards_manager_activate (clipboards_manager);

    g_paste_history_load (history);

    g_paste_gnome_shell_client_new (on_shell_client_ready, self);
}

/**
 * g_paste_daemon_new:
 *
 * Create a new instance of #GPasteDaemon
 *
 * Returns: a newly allocated #GPasteDaemon
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteDaemon *
g_paste_daemon_new (void)
{
    return G_PASTE_DAEMON (g_object_new (G_PASTE_TYPE_DAEMON, NULL));
}
