/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gdbus-macros.h>
#include <gpaste/gpaste-gnome-shell-client.h>

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

struct _GPasteGnomeShellClient
{
    GDBusProxy parent_instance;
};

G_PASTE_DEFINE_TYPE (GnomeShellClient, gnome_shell_client, G_TYPE_DBUS_PROXY)

enum
{
    ACCELERATOR_ACTIVATED,

    LAST_SIGNAL
};

static guint64 signals[LAST_SIGNAL] = { 0 };

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
        g_autoptr (GVariant) action = g_variant_iter_next_value (&params_iter);
        /* consume the params but don't use them */
        G_GNUC_UNUSED g_autoptr (GVariant) params = g_variant_iter_next_value (&params_iter);
        g_signal_emit (self,
                       signals[ACCELERATOR_ACTIVATED],
                       0, /* detail */
                       g_variant_get_uint32 (action),
                       NULL);
    }
}

static void
g_paste_gnome_shell_client_class_init (GPasteGnomeShellClientClass *klass G_GNUC_UNUSED)
{
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
    g_autoptr (GDBusNodeInfo) gnome_shell_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_GNOME_SHELL_INTERFACE,
                                                                                    NULL); /* Error */

    g_dbus_proxy_set_interface_info (proxy, gnome_shell_dbus_info->interfaces[0]);
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
