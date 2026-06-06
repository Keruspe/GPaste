// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-gdbus-macros.h>
#include <gpaste/gpaste-gnome-shell-client.h>
#include <gpaste/gpaste-keybinding-provider.h>

#define G_PASTE_GNOME_SHELL_OBJECT_PATH    "/org/gnome/Shell"
#define G_PASTE_GNOME_SHELL_INTERFACE_NAME "org.gnome.Shell"

#define G_PASTE_GNOME_SHELL_GRAB_ACCELERATOR   "GrabAccelerator"
#define G_PASTE_GNOME_SHELL_GRAB_ACCELERATORS  "GrabAccelerators"
#define G_PASTE_GNOME_SHELL_UNGRAB_ACCELERATOR "UngrabAccelerator"

#define G_PASTE_GNOME_SHELL_SIG_ACCELERATOR_ACTIVATED "AcceleratorActivated"

#define G_PASTE_GNOME_SHELL_INTERFACE                                                                      \
    "<node>"                                                                                               \
        "<interface  name='" G_PASTE_GNOME_SHELL_INTERFACE_NAME "'>"                                       \
            "<method name='" G_PASTE_GNOME_SHELL_GRAB_ACCELERATOR "'>"                                     \
                "<arg type='s' direction='in'  name='accelerator' />"                                      \
                "<arg type='u' direction='in'  name='modeFlags'   />"                                      \
                "<arg type='u' direction='in'  name='grabFlags'   />"                                      \
                "<arg type='u' direction='out' name='action'      />"                                      \
            "</method>"                                                                                    \
            "<method name='" G_PASTE_GNOME_SHELL_GRAB_ACCELERATORS "'>"                                    \
                "<arg type='a(suu)' direction='in'  name='accelerators' />"                                \
                "<arg type='au'     direction='out' name='actions'      />"                                \
            "</method>"                                                                                    \
            "<method name='" G_PASTE_GNOME_SHELL_UNGRAB_ACCELERATOR "'>"                                   \
                "<arg type='u' direction='in'  name='action'  />"                                          \
                "<arg type='b' direction='out' name='success' />"                                          \
            "</method>"                                                                                    \
            "<signal name='" G_PASTE_GNOME_SHELL_SIG_ACCELERATOR_ACTIVATED "'>"                            \
                "<arg name='action'     type='u' />"                                                       \
                "<arg name='parameters' type='a{sv}' />"                                                   \
            "</signal>"                                                                                    \
        "</interface>"                                                                                     \
    "</node>"

typedef struct
{
    GHashTable *id_to_action;  /* gchar* (owned) → GUINT_TO_POINTER (guint32) */
    gchar     **stored_ids;    /* NULL-terminated, owned — saved for re-grab */
    gchar     **stored_accels; /* NULL-terminated, owned — saved for re-grab */
    gsize       stored_n;
    gboolean    grabbing;
    guint64     retries;
    guint       retry_source;
    guint64     shell_watch;
} GPasteGnomeShellClientPrivate;

struct _GPasteGnomeShellClient
{
    GDBusProxy parent_instance;
};

static void gnome_shell_client_provider_init (GPasteKeybindingProviderInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (GnomeShellClient, gnome_shell_client, G_TYPE_DBUS_PROXY,
    G_PASTE_TYPE_KEYBINDING_PROVIDER, gnome_shell_client_provider_init)

enum
{
    ACCELERATOR_ACTIVATED,

    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

/*******************/
/* Methods / Async */
/*******************/

#define DBUS_CALL_ONE_PARAM_ASYNC(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_ASYNC_BASE (GNOME_SHELL_CLIENT, param_type, param_name, G_PASTE_GNOME_SHELL_##method)

#define DBUS_CALL_ONE_PARAMV_ASYNC(method, paramv) \
    DBUS_CALL_ONE_PARAMV_ASYNC_BASE (GNOME_SHELL_CLIENT, paramv, G_PASTE_GNOME_SHELL_##method)

#define DBUS_CALL_THREE_PARAMS_ASYNC(method, params) \
    DBUS_CALL_THREE_PARAMS_ASYNC_BASE (GNOME_SHELL_CLIENT, params, G_PASTE_GNOME_SHELL_##method)

/****************************/
/* Methods / Async - Finish */
/****************************/

#define DBUS_ASYNC_FINISH_RET_BOOL \
    DBUS_ASYNC_FINISH_RET_BOOL_BASE (GNOME_SHELL_CLIENT)

#define DBUS_ASYNC_FINISH_RET_AU \
    DBUS_ASYNC_FINISH_RET_AU_BASE (GNOME_SHELL_CLIENT, NULL)

#define DBUS_ASYNC_FINISH_RET_UINT32 \
    DBUS_ASYNC_FINISH_RET_UINT32_BASE (GNOME_SHELL_CLIENT)

/********************************/
/* Methods / Sync - With return */
/********************************/

#define DBUS_CALL_ONE_PARAM_RET_BOOL(method, param_type, param_name) \
    DBUS_CALL_ONE_PARAM_RET_BOOL_BASE (GNOME_SHELL_CLIENT, param_type, param_name, G_PASTE_GNOME_SHELL_##method)

#define DBUS_CALL_ONE_PARAMV_RET_AU(method, paramv) \
    DBUS_CALL_ONE_PARAMV_RET_AU_BASE (GNOME_SHELL_CLIENT, G_PASTE_GNOME_SHELL_##method, paramv, NULL)

#define DBUS_CALL_THREE_PARAMS_RET_UINT32(method, params) \
    DBUS_CALL_THREE_PARAMS_RET_UINT32_BASE (GNOME_SHELL_CLIENT, params, G_PASTE_GNOME_SHELL_##method)

/******************/
/* Methods / Sync */
/******************/

/**
 * g_paste_gnome_shell_client_grab_accelerator_sync:
 * @self: a #GPasteGnomeShellClient instance
 * @accelerator: a #GPasteGnomeShellAccelerator instance
 * @error: a #GError
 *
 * Grab a keybinding
 *
 * Returns: the action id corresponding
 */
G_PASTE_VISIBLE guint32
g_paste_gnome_shell_client_grab_accelerator_sync (GPasteGnomeShellClient     *self,
                                                  GPasteGnomeShellAccelerator accelerator,
                                                  GError                    **error)
{
    GVariant *accel[] = {
        g_variant_new_string (accelerator.accelerator),
        g_variant_new_uint32 (accelerator.grab_flags),
        g_variant_new_uint32 (accelerator.mode_flags)
    };
    DBUS_CALL_THREE_PARAMS_RET_UINT32 (GRAB_ACCELERATOR, accel);
}

/**
 * g_paste_gnome_shell_client_grab_accelerators_sync:
 * @self: a #GPasteGnomeShellClient instance
 * @accelerators: (array): an array of #GPasteGnomeShellAccelerator instances
 * @error: a #GError
 *
 * Grab some keybindings
 *
 * Returns: the action ids corresponding
 */
G_PASTE_VISIBLE guint32 *
g_paste_gnome_shell_client_grab_accelerators_sync (GPasteGnomeShellClient      *self,
                                                   GPasteGnomeShellAccelerator *accelerators,
                                                   GError                     **error)
{
    g_auto (GVariantBuilder) builder;
    guint64 n_accelerators = 0;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

    for (GPasteGnomeShellAccelerator *accelerator = &accelerators[0]; accelerator->accelerator; accelerator = &accelerators[++n_accelerators])
    {
        g_variant_builder_open (&builder, G_VARIANT_TYPE_TUPLE);
        g_variant_builder_add_value (&builder, g_variant_new_string (accelerator->accelerator));
        g_variant_builder_add_value (&builder, g_variant_new_uint32 (accelerator->grab_flags));
        g_variant_builder_add_value (&builder, g_variant_new_uint32 (accelerator->mode_flags));
        g_variant_builder_close (&builder);
    }

    GVariant *array = g_variant_builder_end (&builder);

    DBUS_CALL_ONE_PARAMV_RET_AU (GRAB_ACCELERATORS, array);
}

/**
 * g_paste_gnome_shell_client_ungrab_accelerator_sync:
 * @self: a #GPasteGnomeShellClient instance
 * @action: the action id corresponding to the keybinding
 * @error: a #GError
 *
 * Ungrab a keybinding
 *
 * Returns: whether the ungrab was succesful or not
 */
G_PASTE_VISIBLE gboolean
g_paste_gnome_shell_client_ungrab_accelerator_sync (GPasteGnomeShellClient *self,
                                                    guint32                 action,
                                                    GError                **error)
{
    DBUS_CALL_ONE_PARAM_RET_BOOL (UNGRAB_ACCELERATOR, uint32, action);
}

/*******************/
/* Methods / Async */
/*******************/

/**
 * g_paste_gnome_shell_client_grab_accelerator:
 * @self: a #GPasteGnomeShellClient instance
 * @accelerator: a #GPasteGnomeShellAccelerator instance
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Grab a keybinding
 */
G_PASTE_VISIBLE void
g_paste_gnome_shell_client_grab_accelerator (GPasteGnomeShellClient     *self,
                                             GPasteGnomeShellAccelerator accelerator,
                                             GAsyncReadyCallback         callback,
                                             gpointer                    user_data)
{
    GVariant *accel[] = {
        g_variant_new_string (accelerator.accelerator),
        g_variant_new_uint32 (accelerator.grab_flags),
        g_variant_new_uint32 (accelerator.mode_flags)
    };
    DBUS_CALL_THREE_PARAMS_ASYNC (GRAB_ACCELERATOR, accel);
}

/**
 * g_paste_gnome_shell_client_grab_accelerators:
 * @self: a #GPasteGnomeShellClient instance
 * @accelerators: (array): an array of #GPasteGnomeShellAccelerator instances
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Grab some keybindings
 */
G_PASTE_VISIBLE void
g_paste_gnome_shell_client_grab_accelerators (GPasteGnomeShellClient      *self,
                                              GPasteGnomeShellAccelerator *accelerators,
                                              GAsyncReadyCallback          callback,
                                              gpointer                     user_data)
{
    g_auto (GVariantBuilder) builder;
    guint64 n_accelerators = 0;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_ARRAY);

    for (GPasteGnomeShellAccelerator *accelerator = &accelerators[0]; accelerator->accelerator; accelerator = &accelerators[++n_accelerators])
    {
        g_variant_builder_open (&builder, G_VARIANT_TYPE_TUPLE);
        g_variant_builder_add_value (&builder, g_variant_new_string (accelerator->accelerator));
        g_variant_builder_add_value (&builder, g_variant_new_uint32 (accelerator->grab_flags));
        g_variant_builder_add_value (&builder, g_variant_new_uint32 (accelerator->mode_flags));
        g_variant_builder_close (&builder);
    }

    GVariant *array = g_variant_builder_end (&builder);

    DBUS_CALL_ONE_PARAMV_ASYNC (GRAB_ACCELERATORS, array);
}

/**
 * g_paste_gnome_shell_client_ungrab_accelerator:
 * @self: a #GPasteGnomeShellClient instance
 * @action: the action id corresponding to the keybinding
 * @callback: (nullable): A #GAsyncReadyCallback to call when the request is satisfied or %NULL if you don't
 * care about the result of the method invocation.
 * @user_data: (nullable): The data to pass to @callback.
 *
 * Ungrab a keybinding
 */
G_PASTE_VISIBLE void
g_paste_gnome_shell_client_ungrab_accelerator (GPasteGnomeShellClient *self,
                                               guint32                 action,
                                               GAsyncReadyCallback     callback,
                                               gpointer                user_data)
{
    DBUS_CALL_ONE_PARAM_ASYNC (UNGRAB_ACCELERATOR, uint32, action);
}

/****************************/
/* Methods / Async - Finish */
/****************************/

/**
 * g_paste_gnome_shell_client_grab_accelerator_finish:
 * @self: a #GPasteGnomeShellClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Grab a keybinding
 *
 * Returns: the action id corresultponding
 */
G_PASTE_VISIBLE guint32
g_paste_gnome_shell_client_grab_accelerator_finish (GPasteGnomeShellClient *self,
                                                    GAsyncResult           *result,
                                                    GError                **error)
{
    DBUS_ASYNC_FINISH_RET_UINT32;
}

/**
 * g_paste_gnome_shell_client_grab_accelerators_finish:
 * @self: a #GPasteGnomeShellClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Grab some keybindings
 *
 * Returns: the action ids corresultponding
 */
G_PASTE_VISIBLE guint32 *
g_paste_gnome_shell_client_grab_accelerators_finish (GPasteGnomeShellClient *self,
                                                     GAsyncResult           *result,
                                                     GError                **error)
{
    DBUS_ASYNC_FINISH_RET_AU;
}

/**
 * g_paste_gnome_shell_client_ungrab_accelerator_finish:
 * @self: a #GPasteGnomeShellClient instance
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback passed to the async call.
 * @error: a #GError
 *
 * Ungrab a keybinding
 *
 * Returns: whether the ungrab was succesful or not
 */
G_PASTE_VISIBLE gboolean
g_paste_gnome_shell_client_ungrab_accelerator_finish (GPasteGnomeShellClient *self,
                                                      GAsyncResult           *result,
                                                      GError                **error)
{
    DBUS_ASYNC_FINISH_RET_BOOL;
}

/**************************/
/* GPasteKeybindingProvider */
/**************************/

typedef struct
{
    GPasteGnomeShellClient *client;
    gchar                 **ids; /* owned copy for mapping after async completes */
    gsize                   n;
} _GrabAllContext;

static gboolean retry_grab_all (gpointer user_data);

static void
grab_all_cb (GObject      *source_object,
             GAsyncResult *res,
             gpointer      user_data)
{
    _GrabAllContext *ctx = user_data;
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (ctx->client);
    g_autoptr (GError) error = NULL;
    g_autofree guint32 *actions = g_paste_gnome_shell_client_grab_accelerators_finish (
        G_PASTE_GNOME_SHELL_CLIENT (source_object), res, &error);

    if (error)
    {
        if (error->code == G_DBUS_ERROR_UNKNOWN_METHOD && priv->retries < 10)
        {
            ++priv->retries;
            priv->retry_source = g_timeout_add_seconds (1, retry_grab_all, ctx->client);
            g_source_set_name_by_id (priv->retry_source, "[GPaste] gnome-shell grab retry");
        }
        else
        {
            priv->retries = 0;
            g_warning ("Couldn't grab keybindings with gnome-shell: %s", error->message);
        }
    }
    else
    {
        priv->retries = 0;

        for (gsize i = 0; i < ctx->n; i++)
        {
            g_hash_table_insert (priv->id_to_action,
                                 g_strdup (ctx->ids[i]),
                                 GUINT_TO_POINTER (actions[i]));
        }
    }

    priv->grabbing = FALSE;
    g_strfreev (ctx->ids);
    g_object_unref (ctx->client);
    g_free (ctx);
}

static void
gnome_shell_client_grab_all (GPasteKeybindingProvider          *provider,
                              const GPasteKeybindingAccelerator *accels)
{
    GPasteGnomeShellClient *self = G_PASTE_GNOME_SHELL_CLIENT (provider);
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    if (priv->grabbing)
        return;

    g_clear_handle_id (&priv->retry_source, g_source_remove);

    g_hash_table_remove_all (priv->id_to_action);

    gsize n = 0;
    for (const GPasteKeybindingAccelerator *a = accels; a->id; a++)
        n++;

    if (!n)
    {
        g_clear_pointer (&priv->stored_ids, g_strfreev);
        g_clear_pointer (&priv->stored_accels, g_strfreev);
        priv->stored_n = 0;
        return;
    }

    /* Copy accel data to owned storage BEFORE freeing the old stored arrays,
     * since the caller may have built accels[] from priv->stored_ids/stored_accels. */
    gchar **new_ids = g_new (gchar *, n + 1);
    gchar **new_accels = g_new (gchar *, n + 1);
    for (gsize i = 0; i < n; i++)
    {
        new_ids[i] = g_strdup (accels[i].id);
        new_accels[i] = g_strdup (accels[i].accelerator);
    }
    new_ids[n] = NULL;
    new_accels[n] = NULL;

    g_strfreev (priv->stored_ids);
    g_strfreev (priv->stored_accels);
    priv->stored_ids = new_ids;
    priv->stored_accels = new_accels;
    priv->stored_n = n;

    g_autofree GPasteGnomeShellAccelerator *shell_accels = g_new (GPasteGnomeShellAccelerator, n + 1);
    for (gsize i = 0; i < n; i++)
        shell_accels[i] = G_PASTE_GNOME_SHELL_ACCELERATOR (priv->stored_accels[i]);
    shell_accels[n].accelerator = NULL;

    _GrabAllContext *ctx = g_new (_GrabAllContext, 1);
    ctx->client = g_object_ref (self);
    ctx->ids = g_strdupv (priv->stored_ids);
    ctx->n = n;

    priv->grabbing = TRUE;

    g_paste_gnome_shell_client_grab_accelerators (self, shell_accels, grab_all_cb, ctx);
}

static gboolean
retry_grab_all (gpointer user_data)
{
    GPasteGnomeShellClient *self = user_data;
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    priv->retry_source = 0;

    if (!priv->stored_ids || priv->stored_n == 0)
        return G_SOURCE_REMOVE;

    g_autofree GPasteKeybindingAccelerator *tmp = g_new (GPasteKeybindingAccelerator, priv->stored_n + 1);
    for (gsize i = 0; i < priv->stored_n; i++)
        tmp[i] = G_PASTE_KEYBINDING_ACCELERATOR (priv->stored_ids[i], priv->stored_accels[i], NULL);
    tmp[priv->stored_n].id = NULL;

    gnome_shell_client_grab_all (G_PASTE_KEYBINDING_PROVIDER (self), tmp);

    return G_SOURCE_REMOVE;
}

static void
gnome_shell_client_ungrab_all (GPasteKeybindingProvider *provider)
{
    GPasteGnomeShellClient *self = G_PASTE_GNOME_SHELL_CLIENT (provider);
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    g_clear_handle_id (&priv->retry_source, g_source_remove);

    GHashTableIter iter;
    gpointer key G_GNUC_UNUSED, value;
    g_hash_table_iter_init (&iter, priv->id_to_action);
    while (g_hash_table_iter_next (&iter, &key, &value))
        g_paste_gnome_shell_client_ungrab_accelerator (self, GPOINTER_TO_UINT (value), NULL, NULL);

    g_hash_table_remove_all (priv->id_to_action);
    g_clear_pointer (&priv->stored_ids, g_strfreev);
    g_clear_pointer (&priv->stored_accels, g_strfreev);
    priv->stored_n = 0;
    priv->grabbing = FALSE;
    priv->retries = 0;
}

static void
gnome_shell_client_provider_init (GPasteKeybindingProviderInterface *iface)
{
    iface->grab_all   = gnome_shell_client_grab_all;
    iface->ungrab_all = gnome_shell_client_ungrab_all;
}

/****************************/
/* Shell watch / D-Bus sig  */
/****************************/

static void
on_shell_appeared (GDBusConnection *connection G_GNUC_UNUSED,
                   const gchar     *name       G_GNUC_UNUSED,
                   const gchar     *name_owner G_GNUC_UNUSED,
                   gpointer         user_data)
{
    GPasteGnomeShellClient *self = user_data;
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    if (!priv->stored_ids || priv->stored_n == 0)
        return;

    g_autofree GPasteKeybindingAccelerator *tmp = g_new (GPasteKeybindingAccelerator, priv->stored_n + 1);
    for (gsize i = 0; i < priv->stored_n; i++)
        tmp[i] = G_PASTE_KEYBINDING_ACCELERATOR (priv->stored_ids[i], priv->stored_accels[i], NULL);
    tmp[priv->stored_n].id = NULL;

    gnome_shell_client_grab_all (G_PASTE_KEYBINDING_PROVIDER (self), tmp);
}

static void
on_shell_vanished (GDBusConnection *connection G_GNUC_UNUSED,
                   const gchar     *name       G_GNUC_UNUSED,
                   gpointer         user_data)
{
    GPasteGnomeShellClient *self = user_data;
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    g_hash_table_remove_all (priv->id_to_action);
    priv->grabbing = FALSE;
}

static void
g_paste_gnome_shell_client_g_signal (GDBusProxy  *proxy,
                                     const gchar *sender_name G_GNUC_UNUSED,
                                     const gchar *signal_name,
                                     GVariant    *parameters)
{
    GPasteGnomeShellClient *self = G_PASTE_GNOME_SHELL_CLIENT (proxy);

    if (g_paste_str_equal (signal_name, G_PASTE_GNOME_SHELL_SIG_ACCELERATOR_ACTIVATED))
    {
        GVariantIter params_iter;
        g_variant_iter_init (&params_iter, parameters);
        g_autoptr (GVariant) action_v = g_variant_iter_next_value (&params_iter);
        G_GNUC_UNUSED g_autoptr (GVariant) params = g_variant_iter_next_value (&params_iter);
        guint32 action = g_variant_get_uint32 (action_v);

        g_signal_emit (self,
                       signals[ACCELERATOR_ACTIVATED],
                       0, /* detail */
                       action,
                       NULL);

        GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init (&iter, priv->id_to_action);
        while (g_hash_table_iter_next (&iter, &key, &value))
        {
            if (GPOINTER_TO_UINT (value) == action)
            {
                g_paste_keybinding_provider_emit_keybinding_activated (G_PASTE_KEYBINDING_PROVIDER (self), key);
                break;
            }
        }
    }
}

static void
g_paste_gnome_shell_client_dispose (GObject *object)
{
    GPasteGnomeShellClient *self = G_PASTE_GNOME_SHELL_CLIENT (object);
    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    g_clear_handle_id (&priv->retry_source, g_source_remove);

    if (priv->shell_watch)
    {
        g_bus_unwatch_name (priv->shell_watch);
        priv->shell_watch = 0;
    }

    g_clear_pointer (&priv->id_to_action, g_hash_table_unref);
    g_clear_pointer (&priv->stored_ids, g_strfreev);
    g_clear_pointer (&priv->stored_accels, g_strfreev);

    G_OBJECT_CLASS (g_paste_gnome_shell_client_parent_class)->dispose (object);
}

static void
g_paste_gnome_shell_client_class_init (GPasteGnomeShellClientClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_gnome_shell_client_dispose;
    G_DBUS_PROXY_CLASS (klass)->g_signal = g_paste_gnome_shell_client_g_signal;

    /**
     * GPasteGnomeShellClient::accelerator-activated:
     * @gnome_shell: the object on which the signal was emitted
     * @id: the id of the activated accelerator
     *
     * The "accelerator-activated" signal is emitted when gnome-shell notifies us
     * that an accelerator has been pressed.
     */
    signals[ACCELERATOR_ACTIVATED] = g_signal_new ("accelerator-activated",
                                                   G_PASTE_TYPE_GNOME_SHELL_CLIENT,
                                                   G_SIGNAL_RUN_LAST,
                                                   0, /* class offset */
                                                   NULL, /* accumulator */
                                                   NULL, /* accumulator data */
                                                   g_cclosure_marshal_VOID__UINT,
                                                   G_TYPE_NONE,
                                                   1,
                                                   G_TYPE_UINT);
}

static void
g_paste_gnome_shell_client_init (GPasteGnomeShellClient *self)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusNodeInfo) gnome_shell_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_GNOME_SHELL_INTERFACE,
                                                                                    &error);
    g_assert_no_error (error);

    g_dbus_proxy_set_interface_info (proxy, gnome_shell_dbus_info->interfaces[0]);

    GPasteGnomeShellClientPrivate *priv = g_paste_gnome_shell_client_get_instance_private (self);

    priv->id_to_action = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    priv->stored_ids = NULL;
    priv->stored_accels = NULL;
    priv->stored_n = 0;
    priv->grabbing = FALSE;
    priv->retries = 0;
    priv->retry_source = 0;
    priv->shell_watch = g_bus_watch_name (G_BUS_TYPE_SESSION,
                                          G_PASTE_GNOME_SHELL_BUS_NAME,
                                          G_BUS_NAME_WATCHER_FLAGS_NONE,
                                          on_shell_appeared,
                                          on_shell_vanished,
                                          self,
                                          NULL);
}

/**
 * g_paste_gnome_shell_client_new_sync:
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteGnomeShellClient
 *
 * Returns: a newly allocated #GPasteGnomeShellClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGnomeShellClient *
g_paste_gnome_shell_client_new_sync (GError **error)
{
    CUSTOM_PROXY_NEW (GNOME_SHELL_CLIENT, GNOME_SHELL, G_PASTE_GNOME_SHELL_BUS_NAME);
}

/**
 * g_paste_gnome_shell_client_new:
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Create a new instance of #GPasteGnomeShellClient
 */
G_PASTE_VISIBLE void
g_paste_gnome_shell_client_new (GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    CUSTOM_PROXY_NEW_ASYNC (GNOME_SHELL_CLIENT, GNOME_SHELL, G_PASTE_GNOME_SHELL_BUS_NAME);
}

/**
 * g_paste_gnome_shell_client_new_finsh:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to the async ctor.
 * @error: Return location for error or %NULL.
 *
 * Create a new instance of #GPasteGnomeShellClient
 *
 * Returns: a newly allocated #GPasteGnomeShellClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteGnomeShellClient *
g_paste_gnome_shell_client_new_finish (GAsyncResult *result,
                                       GError      **error)
{
    CUSTOM_PROXY_NEW_FINISH (GNOME_SHELL_CLIENT);
}
