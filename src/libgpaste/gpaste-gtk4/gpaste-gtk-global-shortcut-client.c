// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-gdbus-macros.h>
#include <gpaste/gpaste-keybinding-provider.h>
#include <gpaste-gtk4/gpaste-gtk-global-shortcut-client.h>

#include <gtk/gtk.h>

#define G_PASTE_GTK_GLOBAL_SHORTCUT_OBJECT_PATH    "/org/freedesktop/portal/desktop"
#define G_PASTE_GTK_GLOBAL_SHORTCUT_INTERFACE_NAME "org.freedesktop.portal.GlobalShortcuts"

#define G_PASTE_GTK_GLOBAL_SHORTCUT_CREATE_SESSION "CreateSession"
#define G_PASTE_GTK_GLOBAL_SHORTCUT_BIND_SHORTCUTS "BindShortcuts"

#define G_PASTE_GTK_GLOBAL_SHORTCUT_SIG_ACTIVATED "Activated"

#define G_PASTE_GTK_GLOBAL_SHORTCUT_INTERFACE                                                            \
    "<node>"                                                                                             \
        "<interface name='" G_PASTE_GTK_GLOBAL_SHORTCUT_INTERFACE_NAME "'>"                              \
            "<method name='" G_PASTE_GTK_GLOBAL_SHORTCUT_CREATE_SESSION "'>"                             \
                "<arg type='a{sv}'   direction='in'  name='options' />"                                  \
                "<arg type='o'       direction='out' name='handle'  />"                                  \
            "</method>"                                                                                  \
            "<method name='" G_PASTE_GTK_GLOBAL_SHORTCUT_BIND_SHORTCUTS "'>"                             \
                "<arg type='o'         direction='in'  name='session_handle' />"                         \
                "<arg type='a(sa{sv})' direction='in'  name='shortcuts'      />"                         \
                "<arg type='s'         direction='in'  name='parent_window'  />"                         \
                "<arg type='a{sv}'     direction='in'  name='options'        />"                         \
                "<arg type='o'         direction='out' name='handle'         />"                         \
            "</method>"                                                                                  \
            "<signal name='" G_PASTE_GTK_GLOBAL_SHORTCUT_SIG_ACTIVATED "'>"                              \
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

typedef struct
{
    gchar     *session_handle;
    GPtrArray *shortcuts;  /* _Shortcut*, owned via _shortcut_free */
} GPasteGtkGlobalShortcutClientPrivate;

struct _GPasteGtkGlobalShortcutClient
{
    GDBusProxy parent_instance;
};

static void global_shortcut_client_provider_init (GPasteKeybindingProviderInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (GtkGlobalShortcutClient, gtk_global_shortcut_client, G_TYPE_DBUS_PROXY,
    G_PASTE_TYPE_KEYBINDING_PROVIDER, global_shortcut_client_provider_init)

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
build_shortcuts_variant (GPasteGtkGlobalShortcutClientPrivate *priv)
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
    GPasteGtkGlobalShortcutClient *client;
    GTask                         *task;
    GDBusConnection               *connection;
    guint                          signal_id;
} _SessionRequestData;

static void
session_request_data_free (_SessionRequestData *data)
{
    g_clear_object (&data->client);
    g_clear_object (&data->task);
    g_clear_object (&data->connection);
    g_free (data);
}

static void start_bind_async (GPasteGtkGlobalShortcutClient *self, GTask *task);

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
        GPasteGtkGlobalShortcutClient *self = G_PASTE_GTK_GLOBAL_SHORTCUT_CLIENT (source);
        GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
        g_clear_pointer (&priv->session_handle, g_free);
        g_task_return_error (task, g_steal_pointer (&error));
    }
    else
        g_task_return_boolean (task, TRUE);
}

static void
start_bind_async (GPasteGtkGlobalShortcutClient *self,
                  GTask                         *task)
{
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
    GDBusProxy *proxy = G_DBUS_PROXY (self);

    g_auto (GVariantBuilder) options;
    g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);

    GVariant *params[] = {
        g_variant_new_object_path (priv->session_handle),
        build_shortcuts_variant (priv),
        g_variant_new_string (""),
        g_variant_builder_end (&options)
    };

    g_dbus_proxy_call (proxy, G_PASTE_GTK_GLOBAL_SHORTCUT_BIND_SHORTCUTS,
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

    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (data->client);
    g_set_str (&priv->session_handle, g_variant_get_string (handle_v, NULL));

    g_autoptr (GPasteGtkGlobalShortcutClient) client = g_object_ref (data->client);
    g_autoptr (GTask) task = g_object_ref (data->task);
    session_request_data_free (data);

    start_bind_async (client, task);
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
}

static void
start_create_session_async (GPasteGtkGlobalShortcutClient *self,
                            GTask                      *task)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);
    GDBusConnection *connection = g_dbus_proxy_get_connection (proxy);

    _SessionRequestData *data = g_new (_SessionRequestData, 1);
    data->client = g_object_ref (self);
    data->task = g_object_ref (task);
    data->connection = g_object_ref (connection);
    data->signal_id = 0;

    g_auto (GVariantBuilder) options;
    g_variant_builder_init (&options, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add (&options, "{sv}", "session_handle_token", g_variant_new_string ("gpaste"));

    GVariant *params[] = { g_variant_builder_end (&options) };

    g_dbus_proxy_call (proxy, G_PASTE_GTK_GLOBAL_SHORTCUT_CREATE_SESSION,
                       g_variant_new_tuple (params, 1),
                       G_DBUS_CALL_FLAGS_NONE, -1, NULL,
                       on_create_session_method_done, data);
}

/**************************/
/* GPasteKeybindingProvider */
/**************************/

static void
on_provider_bind_done (GObject      *source   G_GNUC_UNUSED,
                       GAsyncResult *result,
                       gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    if (!g_task_propagate_boolean (G_TASK (result), &error))
        g_warning ("GPasteGtkGlobalShortcutClient: BindShortcuts failed: %s", error->message);
}

static void
global_shortcut_client_grab_all (GPasteKeybindingProvider          *provider,
                                  const GPasteKeybindingAccelerator *accels)
{
    GPasteGtkGlobalShortcutClient *self = G_PASTE_GTK_GLOBAL_SHORTCUT_CLIENT (provider);
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    g_ptr_array_set_size (priv->shortcuts, 0);

    for (const GPasteKeybindingAccelerator *a = accels; a->id; a++)
        g_ptr_array_add (priv->shortcuts, _shortcut_new (a->id, a->accelerator, a->description));

    g_autoptr (GTask) task = g_task_new (self, NULL, on_provider_bind_done, NULL);

    if (priv->session_handle)
        start_bind_async (self, task);
    else if (priv->shortcuts->len)
        start_create_session_async (self, task);
    else
        g_task_return_boolean (task, TRUE);
}

static void
global_shortcut_client_ungrab_all (GPasteKeybindingProvider *provider)
{
    GPasteGtkGlobalShortcutClient *self = G_PASTE_GTK_GLOBAL_SHORTCUT_CLIENT (provider);
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    g_ptr_array_set_size (priv->shortcuts, 0);

    if (!priv->session_handle)
        return;

    g_autoptr (GTask) task = g_task_new (self, NULL, on_provider_bind_done, NULL);
    start_bind_async (self, task);
}

static void
global_shortcut_client_provider_init (GPasteKeybindingProviderInterface *iface)
{
    iface->grab_all   = global_shortcut_client_grab_all;
    iface->ungrab_all = global_shortcut_client_ungrab_all;
}

/**********************/
/* D-Bus signal       */
/**********************/

static void
g_paste_gtk_global_shortcut_client_g_signal (GDBusProxy  *proxy,
                                             const gchar *sender_name G_GNUC_UNUSED,
                                             const gchar *signal_name,
                                             GVariant    *parameters)
{
    GPasteGtkGlobalShortcutClient *self = G_PASTE_GTK_GLOBAL_SHORTCUT_CLIENT (proxy);

    if (g_paste_str_equal (signal_name, G_PASTE_GTK_GLOBAL_SHORTCUT_SIG_ACTIVATED))
    {
        GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
        const gchar *session_handle;
        const gchar *shortcut_id;
        guint64 timestamp G_GNUC_UNUSED;
        g_autoptr (GVariant) options = NULL;
        g_variant_get (parameters, "(&o&st@a{sv})",
                       &session_handle, &shortcut_id, &timestamp, &options);

        if (g_paste_str_equal (session_handle, priv->session_handle))
            g_paste_keybinding_provider_emit_keybinding_activated (G_PASTE_KEYBINDING_PROVIDER (self), shortcut_id);
    }
}

/****************/
/* GObject glue */
/****************/

static void
g_paste_gtk_global_shortcut_client_dispose (GObject *object)
{
    GPasteGtkGlobalShortcutClient *self = G_PASTE_GTK_GLOBAL_SHORTCUT_CLIENT (object);
    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);

    g_clear_pointer (&priv->session_handle, g_free);
    g_clear_pointer (&priv->shortcuts, g_ptr_array_unref);

    G_OBJECT_CLASS (g_paste_gtk_global_shortcut_client_parent_class)->dispose (object);
}

static void
g_paste_gtk_global_shortcut_client_class_init (GPasteGtkGlobalShortcutClientClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_gtk_global_shortcut_client_dispose;
    G_DBUS_PROXY_CLASS (klass)->g_signal = g_paste_gtk_global_shortcut_client_g_signal;
}

static void
g_paste_gtk_global_shortcut_client_init (GPasteGtkGlobalShortcutClient *self)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusNodeInfo) dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_GTK_GLOBAL_SHORTCUT_INTERFACE,
                                                                        &error);
    g_assert_no_error (error);

    g_dbus_proxy_set_interface_info (proxy, dbus_info->interfaces[0]);

    GPasteGtkGlobalShortcutClientPrivate *priv = g_paste_gtk_global_shortcut_client_get_instance_private (self);
    priv->session_handle = NULL;
    priv->shortcuts = g_ptr_array_new_with_free_func (_shortcut_free);
}

/**
 * g_paste_gtk_global_shortcut_client_new_sync:
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteGtkGlobalShortcutClient
 *
 * Returns: a newly allocated #GPasteGtkGlobalShortcutClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGtkGlobalShortcutClient *
g_paste_gtk_global_shortcut_client_new_sync (GError **error)
{
    CUSTOM_PROXY_NEW (GTK_GLOBAL_SHORTCUT_CLIENT, GTK_GLOBAL_SHORTCUT, G_PASTE_GTK_GLOBAL_SHORTCUT_BUS_NAME);
}

/**
 * g_paste_gtk_global_shortcut_client_new:
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Create a new instance of #GPasteGtkGlobalShortcutClient
 */
G_PASTE_VISIBLE void
g_paste_gtk_global_shortcut_client_new (GAsyncReadyCallback callback,
                                    gpointer            user_data)
{
    CUSTOM_PROXY_NEW_ASYNC (GTK_GLOBAL_SHORTCUT_CLIENT, GTK_GLOBAL_SHORTCUT, G_PASTE_GTK_GLOBAL_SHORTCUT_BUS_NAME);
}

/**
 * g_paste_gtk_global_shortcut_client_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to the async ctor.
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteGtkGlobalShortcutClient
 *
 * Returns: a newly allocated #GPasteGtkGlobalShortcutClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGtkGlobalShortcutClient *
g_paste_gtk_global_shortcut_client_new_finish (GAsyncResult *result,
                                           GError      **error)
{
    CUSTOM_PROXY_NEW_FINISH (GTK_GLOBAL_SHORTCUT_CLIENT);
}
