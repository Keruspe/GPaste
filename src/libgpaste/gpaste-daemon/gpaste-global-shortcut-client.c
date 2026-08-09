// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-global-shortcut-client.h>
#include <gpaste-keyval.h>

#include <gtk/gtk.h>

enum
{
    KEYBINDING_ACTIVATED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define G_PASTE_GLOBAL_SHORTCUT_OBJECT_PATH    "/org/freedesktop/portal/desktop"
#define G_PASTE_GLOBAL_SHORTCUT_INTERFACE_NAME "org.freedesktop.portal.GlobalShortcuts"

#define G_PASTE_GLOBAL_SHORTCUT_CREATE_SESSION "CreateSession"
#define G_PASTE_GLOBAL_SHORTCUT_BIND_SHORTCUTS "BindShortcuts"

#define G_PASTE_GLOBAL_SHORTCUT_SIG_ACTIVATED "Activated"

#define G_PASTE_GLOBAL_SHORTCUT_INTERFACE                                                            \
    "<node>"                                                                                             \
        "<interface name='" G_PASTE_GLOBAL_SHORTCUT_INTERFACE_NAME "'>"                              \
            "<method name='" G_PASTE_GLOBAL_SHORTCUT_CREATE_SESSION "'>"                             \
                "<arg type='a{sv}'   direction='in'  name='options' />"                                  \
                "<arg type='o'       direction='out' name='handle'  />"                                  \
            "</method>"                                                                                  \
            "<method name='" G_PASTE_GLOBAL_SHORTCUT_BIND_SHORTCUTS "'>"                             \
                "<arg type='o'         direction='in'  name='session_handle' />"                         \
                "<arg type='a(sa{sv})' direction='in'  name='shortcuts'      />"                         \
                "<arg type='s'         direction='in'  name='parent_window'  />"                         \
                "<arg type='a{sv}'     direction='in'  name='options'        />"                         \
                "<arg type='o'         direction='out' name='handle'         />"                         \
            "</method>"                                                                                  \
            "<signal name='" G_PASTE_GLOBAL_SHORTCUT_SIG_ACTIVATED "'>"                              \
                "<arg type='o'     name='session_handle' />"                                             \
                "<arg type='s'     name='shortcut_id'   />"                                              \
                "<arg type='t'     name='timestamp'     />"                                              \
                "<arg type='a{sv}' name='options'       />"                                              \
            "</signal>"                                                                                  \
        "</interface>"                                                                                   \
    "</node>"

typedef struct
{
    gchar *id;
    gchar *accelerator;
    gchar *description;
} _Shortcut;

static _Shortcut *
_shortcut_new (const gchar *id,
               const gchar *accelerator,
               const gchar *description)
{
    _Shortcut *s = g_new (_Shortcut, 1);
    s->id = g_strdup (id);
    s->accelerator = g_strdup (accelerator);
    s->description = g_strdup (description);
    return s;
}

static void
_shortcut_free (gpointer data)
{
    g_autofree _Shortcut *s = data;
    g_free (s->id);
    g_free (s->accelerator);
    g_free (s->description);
}

struct _GPasteGlobalShortcutClient
{
    GDBusProxy parent_instance;

    gchar     *session_handle;
    gboolean   session_creating; /* a CreateSession round-trip is in flight */
    GPtrArray *shortcuts;  /* _Shortcut*, owned via _shortcut_free */
};

G_PASTE_DEFINE_TYPE (GlobalShortcutClient, global_shortcut_client, G_TYPE_DBUS_PROXY)

/**********************/
/* Shortcut variant   */
/**********************/

static gchar *
gtk_accel_to_portal_trigger (const gchar *accel)
{
    if (!accel || !*accel)
        return NULL;

    guint keyval = 0;
    GdkModifierType mods = 0;
    if (!gtk_accelerator_parse (accel, &keyval, &mods) || !keyval)
        return NULL;

    /* Settings written by an older GPaste (or by hand) can name the keypad
     * variant of a key; the portal takes one name, so give it the primary
     * spelling of the alias group rather than whichever one was stored. The
     * capture side has to agree, which is why the rule is shared. */
    keyval = g_paste_keyval_canonicalize (keyval);

    const gchar *key_name = gdk_keyval_name (keyval);
    if (!key_name)
        return NULL;

    g_autoptr (GStrvBuilder) tokens = g_strv_builder_new ();

    if (mods & GDK_CONTROL_MASK)
        g_strv_builder_add (tokens, "CTRL");
    if (mods & GDK_ALT_MASK)
        g_strv_builder_add (tokens, "ALT");
    if (mods & GDK_SHIFT_MASK)
        g_strv_builder_add (tokens, "SHIFT");
    if (mods & GDK_SUPER_MASK)
        g_strv_builder_add (tokens, "SUPER");

    g_strv_builder_take (tokens, g_ascii_strup (key_name, -1));

    g_auto (GStrv) parts = g_strv_builder_end (tokens);
    return g_strjoinv ("+", parts);
}

static GVariant *
build_shortcuts_variant (GPasteGlobalShortcutClient *priv)
{
    g_auto (GVariantBuilder) builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(sa{sv})"));

    for (guint i = 0; i < priv->shortcuts->len; i++)
    {
        _Shortcut *s = g_ptr_array_index (priv->shortcuts, i);
        g_variant_builder_open (&builder, G_VARIANT_TYPE ("(sa{sv})"));
        g_variant_builder_add (&builder, "s", s->id);
        g_variant_builder_open (&builder, G_VARIANT_TYPE_VARDICT);
        if (s->accelerator)
        {
            g_autofree gchar *portal_trigger = gtk_accel_to_portal_trigger (s->accelerator);
            if (portal_trigger)
                g_variant_builder_add (&builder, "{sv}", "preferred_trigger",
                                       g_variant_new_string (portal_trigger));
        }
        if (s->description)
            g_variant_builder_add (&builder, "{sv}", "description",
                                   g_variant_new_string (s->description));
        g_variant_builder_close (&builder);
        g_variant_builder_close (&builder);
    }

    return g_variant_builder_end (&builder);
}

/**********************/
/* Async session/bind */
/**********************/

typedef struct
{
    GPasteGlobalShortcutClient *client;
    GTask                         *task;
    GDBusConnection               *connection;
    guint                          signal_id;
    guint                          timeout_id;
} _SessionRequestData;

static void
session_request_data_free (_SessionRequestData *data)
{
    if (data->timeout_id)
        g_source_remove (data->timeout_id);
    if (data->signal_id)
        g_dbus_connection_signal_unsubscribe (data->connection, data->signal_id);
    g_clear_object (&data->client);
    g_clear_object (&data->task);
    g_clear_object (&data->connection);
    g_free (data);
}

static void start_bind_async (GPasteGlobalShortcutClient *self, GTask *task);

static void
on_bind_method_done (GObject      *source,
                     GAsyncResult *result,
                     gpointer      user_data)
{
    g_autoptr (GTask) task = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

    if (!ret)
    {
        GPasteGlobalShortcutClient *self = G_PASTE_GLOBAL_SHORTCUT_CLIENT (source);
        g_clear_pointer (&self->session_handle, g_free);
        g_task_return_error (task, g_steal_pointer (&error));
    }
    else
        g_task_return_boolean (task, TRUE);
}

static void
start_bind_async (GPasteGlobalShortcutClient *self,
                  GTask                         *task)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);

    g_auto (GVariantBuilder) options;
    g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);

    GVariant *params[] = {
        g_variant_new_object_path (self->session_handle),
        build_shortcuts_variant (self),
        g_variant_new_string (""),
        g_variant_builder_end (&options)
    };

    g_dbus_proxy_call (proxy, G_PASTE_GLOBAL_SHORTCUT_BIND_SHORTCUTS,
                       g_variant_new_tuple (params, 4),
                       G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                       on_bind_method_done, g_object_ref (task));
}

static void
on_session_created (GDBusConnection *conn,
                    const gchar     *sender G_GNUC_UNUSED,
                    const gchar     *path   G_GNUC_UNUSED,
                    const gchar     *iface  G_GNUC_UNUSED,
                    const gchar     *sig    G_GNUC_UNUSED,
                    GVariant        *params,
                    gpointer         user_data)
{
    _SessionRequestData *data = user_data;
    g_dbus_connection_signal_unsubscribe (conn, data->signal_id);
    data->signal_id = 0; /* already unsubscribed; don't let the free do it again */

    GPasteGlobalShortcutClient *self = data->client;
    self->session_creating = FALSE;

    guint response;
    g_autoptr (GVariant) results = NULL;
    g_variant_get (params, "(u@a{sv})", &response, &results);

    if (response != 0)
    {
        g_task_return_new_error (data->task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "CreateSession portal request failed with response %u", response);
        session_request_data_free (data);
        return;
    }

    g_autoptr (GVariant) handle_v = g_variant_lookup_value (results, "session_handle", NULL);
    if (!handle_v)
    {
        g_task_return_new_error (data->task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "CreateSession response missing session_handle");
        session_request_data_free (data);
        return;
    }

    g_set_str (&self->session_handle, g_variant_get_string (handle_v, NULL));

    g_autoptr (GPasteGlobalShortcutClient) client = g_object_ref (data->client);
    g_autoptr (GTask) task = g_object_ref (data->task);
    session_request_data_free (data);

    start_bind_async (client, task);
}

/* The portal Request may never emit Response (e.g. a broken portal); recover so
 * session_creating does not stay set forever, blocking every later grab. */
static gboolean
on_session_timeout (gpointer user_data)
{
    _SessionRequestData *data = user_data;
    GPasteGlobalShortcutClient *self = data->client;

    self->session_creating = FALSE;
    data->timeout_id = 0; /* this source is firing; don't let the free remove it */

    g_task_return_new_error (data->task, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                             "Timed out waiting for the GlobalShortcuts portal to create a session");
    session_request_data_free (data);

    return G_SOURCE_REMOVE;
}

static void
on_create_session_method_done (GObject      *source,
                               GAsyncResult *result,
                               gpointer      user_data)
{
    _SessionRequestData *data = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) ret = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

    if (!ret)
    {
        GPasteGlobalShortcutClient *self = data->client;
        self->session_creating = FALSE;
        g_task_return_error (data->task, g_steal_pointer (&error));
        session_request_data_free (data);
        return;
    }

    const gchar *request_path;
    g_variant_get (ret, "(o)", &request_path);

    data->signal_id = g_dbus_connection_signal_subscribe (
        data->connection, NULL,
        "org.freedesktop.portal.Request", "Response",
        request_path, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
        on_session_created, data, NULL);

    data->timeout_id = g_timeout_add_seconds (30, on_session_timeout, data);
    g_source_set_name_by_id (data->timeout_id, "[GPaste] portal session timeout");
}

static void
start_create_session_async (GPasteGlobalShortcutClient *self,
                            GTask                      *task)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);
    GDBusConnection *connection = g_dbus_proxy_get_connection (proxy);

    _SessionRequestData *data = g_new (_SessionRequestData, 1);
    data->client = g_object_ref (self);
    data->task = g_object_ref (task);
    data->connection = g_object_ref (connection);
    data->signal_id = 0;
    data->timeout_id = 0; /* only set once the session is created; the synchronous
                           * failure path frees data before then and would
                           * otherwise g_source_remove() an uninitialised id */

    g_auto (GVariantBuilder) options;
    g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&options, "{sv}", "session_handle_token", g_variant_new_string ("gpaste"));

    GVariant *params[] = { g_variant_builder_end (&options) };

    g_dbus_proxy_call (proxy, G_PASTE_GLOBAL_SHORTCUT_CREATE_SESSION,
                       g_variant_new_tuple (params, 1),
                       G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                       on_create_session_method_done, data);
}

/**********************/
/* Shortcut grabbing  */
/**********************/

static void
on_provider_bind_done (GObject      *source   G_GNUC_UNUSED,
                       GAsyncResult *result,
                       gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    if (!g_task_propagate_boolean (G_TASK (result), &error))
        g_warning ("GPasteGlobalShortcutClient: BindShortcuts failed: %s", error->message);
}

/* The portal binds shortcuts to a session for the session's lifetime, so the
 * only way to (re)bind a different set is to drop the session and create a new
 * one. Close the current session, if any. */
static void
close_session (GPasteGlobalShortcutClient *self)
{
    if (!self->session_handle)
        return;

    g_dbus_connection_call (g_dbus_proxy_get_connection (G_DBUS_PROXY (self)),
                            g_dbus_proxy_get_name (G_DBUS_PROXY (self)),
                            self->session_handle,
                            "org.freedesktop.portal.Session", "Close",
                            NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

    g_clear_pointer (&self->session_handle, g_free);
}

/**
 * g_paste_global_shortcut_client_grab_all:
 * @self: a #GPasteGlobalShortcutClient
 * @accels: (array): a %NULL-terminated (by id) array of #GPasteKeybindingAccelerator
 *
 * Replace all currently registered shortcuts with @accels.
 */
G_PASTE_VISIBLE void
g_paste_global_shortcut_client_grab_all (GPasteGlobalShortcutClient     *self,
                                             const GPasteKeybindingAccelerator *accels)
{
    g_return_if_fail (G_PASTE_IS_GLOBAL_SHORTCUT_CLIENT (self));
    g_return_if_fail (accels);

    g_ptr_array_set_size (self->shortcuts, 0);

    for (const GPasteKeybindingAccelerator *a = accels; a->id; a++)
        g_ptr_array_add (self->shortcuts, _shortcut_new (a->id, a->accelerator, a->description));

    /* A CreateSession is already in flight: it will bind self->shortcuts (the
     * latest set) when it completes, so let it finish rather than starting a
     * second session. */
    if (self->session_creating)
        return;

    g_autoptr (GTask) task = g_task_new (self, NULL, on_provider_bind_done, NULL);

    /* Drop any existing session and create a fresh one so the new shortcuts
     * actually take effect. */
    close_session (self);

    if (self->shortcuts->len)
    {
        self->session_creating = TRUE;
        start_create_session_async (self, task);
    }
    else
        g_task_return_boolean (task, TRUE);
}

/**
 * g_paste_global_shortcut_client_ungrab_all:
 * @self: a #GPasteGlobalShortcutClient
 *
 * Release all currently registered shortcuts.
 */
G_PASTE_VISIBLE void
g_paste_global_shortcut_client_ungrab_all (GPasteGlobalShortcutClient *self)
{
    g_return_if_fail (G_PASTE_IS_GLOBAL_SHORTCUT_CLIENT (self));

    g_ptr_array_set_size (self->shortcuts, 0);

    /* If a session is being created it will bind the now-empty set; otherwise
     * just drop the session, there is nothing left to bind. */
    if (!self->session_creating)
        close_session (self);
}

/**********************/
/* D-Bus signal       */
/**********************/

static void
g_paste_global_shortcut_client_g_signal (GDBusProxy  *proxy,
                                             const gchar *sender_name G_GNUC_UNUSED,
                                             const gchar *signal_name,
                                             GVariant    *parameters)
{
    GPasteGlobalShortcutClient *self = G_PASTE_GLOBAL_SHORTCUT_CLIENT (proxy);

    if (g_paste_str_equal (signal_name, G_PASTE_GLOBAL_SHORTCUT_SIG_ACTIVATED))
    {
        const gchar *session_handle;
        const gchar *shortcut_id;
        guint64 timestamp G_GNUC_UNUSED;
        g_autoptr (GVariant) options = NULL;
        g_variant_get (parameters, "(&o&st@a{sv})",
                       &session_handle, &shortcut_id, &timestamp, &options);

        if (g_paste_str_equal (session_handle, self->session_handle))
            g_signal_emit (self, signals[KEYBINDING_ACTIVATED], 0, shortcut_id);
    }
}

/****************/
/* GObject glue */
/****************/

static void
g_paste_global_shortcut_client_dispose (GObject *object)
{
    GPasteGlobalShortcutClient *self = G_PASTE_GLOBAL_SHORTCUT_CLIENT (object);

    g_clear_pointer (&self->session_handle, g_free);
    g_clear_pointer (&self->shortcuts, g_ptr_array_unref);

    G_OBJECT_CLASS (g_paste_global_shortcut_client_parent_class)->dispose (object);
}

static void
g_paste_global_shortcut_client_class_init (GPasteGlobalShortcutClientClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_global_shortcut_client_dispose;
    G_DBUS_PROXY_CLASS (klass)->g_signal = g_paste_global_shortcut_client_g_signal;

    /**
     * GPasteGlobalShortcutClient::keybinding-activated:
     * @client: the object on which the signal was emitted
     * @id: the id of the activated shortcut (its dconf key)
     *
     * The "keybinding-activated" signal is emitted when a registered shortcut
     * is pressed by the user.
     */
    signals[KEYBINDING_ACTIVATED] = g_signal_new ("keybinding-activated",
                                                  G_TYPE_FROM_CLASS (klass),
                                                  G_SIGNAL_RUN_LAST,
                                                  0,
                                                  NULL, /* accumulator */
                                                  NULL, /* accumulator data */
                                                  g_cclosure_marshal_VOID__STRING,
                                                  G_TYPE_NONE,
                                                  1,
                                                  G_TYPE_STRING);
}

static void
g_paste_global_shortcut_client_init (GPasteGlobalShortcutClient *self)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusNodeInfo) dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_GLOBAL_SHORTCUT_INTERFACE,
                                                                        &error);
    g_assert_no_error (error);

    g_dbus_proxy_set_interface_info (proxy, dbus_info->interfaces[0]);

    self->session_handle = NULL;
    self->shortcuts = g_ptr_array_new_with_free_func (_shortcut_free);
}

/**
 * g_paste_global_shortcut_client_new_sync:
 * @error: return location for a #GError, or %NULL
 *
 * Create a new instance of #GPasteGlobalShortcutClient
 *
 * A failure is a %G_DBUS_ERROR or a %G_IO_ERROR: this only reaches the portal's
 * bus name, never a shortcut request.
 *
 * Returns: a newly allocated #GPasteGlobalShortcutClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGlobalShortcutClient *
g_paste_global_shortcut_client_new_sync (GError **error)
{
    GInitable *self = g_initable_new (G_PASTE_TYPE_GLOBAL_SHORTCUT_CLIENT,
                                      NULL, /* cancellable */
                                      error,
                                      "g-bus-type",       G_BUS_TYPE_SESSION,
                                      "g-flags",          G_DBUS_PROXY_FLAGS_NONE,
                                      "g-name",           G_PASTE_GLOBAL_SHORTCUT_BUS_NAME,
                                      "g-object-path",    G_PASTE_GLOBAL_SHORTCUT_OBJECT_PATH,
                                      "g-interface-name", G_PASTE_GLOBAL_SHORTCUT_INTERFACE_NAME,
                                      NULL);

    return (self) ? G_PASTE_GLOBAL_SHORTCUT_CLIENT (self) : NULL;
}

/**
 * g_paste_global_shortcut_client_new:
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Create a new instance of #GPasteGlobalShortcutClient
 */
G_PASTE_VISIBLE void
g_paste_global_shortcut_client_new (GAsyncReadyCallback callback,
                                    gpointer            user_data)
{
    g_async_initable_new_async (G_PASTE_TYPE_GLOBAL_SHORTCUT_CLIENT,
                                G_PRIORITY_DEFAULT,
                                NULL, /* cancellable */
                                callback,
                                user_data,
                                "g-bus-type",       G_BUS_TYPE_SESSION,
                                "g-flags",          G_DBUS_PROXY_FLAGS_NONE,
                                "g-name",           G_PASTE_GLOBAL_SHORTCUT_BUS_NAME,
                                "g-object-path",    G_PASTE_GLOBAL_SHORTCUT_OBJECT_PATH,
                                "g-interface-name", G_PASTE_GLOBAL_SHORTCUT_INTERFACE_NAME,
                                NULL);
}

/**
 * g_paste_global_shortcut_client_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to the async ctor.
 * @error: return location for a #GError, or %NULL
 *
 * Create a new instance of #GPasteGlobalShortcutClient
 *
 * A failure is a %G_DBUS_ERROR or a %G_IO_ERROR: this only reaches the portal's
 * bus name, never a shortcut request.
 *
 * Returns: a newly allocated #GPasteGlobalShortcutClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGlobalShortcutClient *
g_paste_global_shortcut_client_new_finish (GAsyncResult *result,
                                           GError      **error)
{
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GObject) source = g_async_result_get_source_object (result);

    g_assert (source);

    GObject *self = g_async_initable_new_finish (G_ASYNC_INITABLE (source), result, error);

    return (self) ? G_PASTE_GLOBAL_SHORTCUT_CLIENT (self) : NULL;
}
