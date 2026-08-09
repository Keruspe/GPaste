// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-screensaver-client.h>

#define G_PASTE_SCREENSAVER_OBJECT_PATH    "/org/gnome/ScreenSaver"
#define G_PASTE_SCREENSAVER_INTERFACE_NAME "org.gnome.ScreenSaver"

#define G_PASTE_SCREENSAVER_SIG_ACTIVE_CHANGED "ActiveChanged"

#define G_PASTE_SCREENSAVER_INTERFACE                                    \
    "<node>"                                                             \
        "<interface  name='" G_PASTE_SCREENSAVER_INTERFACE_NAME "'>"     \
            "<signal name='" G_PASTE_SCREENSAVER_SIG_ACTIVE_CHANGED "'>" \
                "<arg name='new_value' type='b' />"                      \
            "</signal>"                                                  \
        "</interface>"                                                   \
    "</node>"

struct _GPasteScreensaverClient
{
    GDBusProxy parent_instance;

    gboolean active;
};

G_PASTE_DEFINE_TYPE (ScreensaverClient, screensaver_client, G_TYPE_DBUS_PROXY)

enum
{
    PROP_0,
    PROP_ACTIVE,

    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
g_paste_screensaver_client_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
    GPasteScreensaverClient *self = G_PASTE_SCREENSAVER_CLIENT (object);

    switch (prop_id)
    {
    case PROP_ACTIVE:
        g_value_set_boolean (value, self->active);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
g_paste_screensaver_client_g_signal (GDBusProxy  *proxy,
                                     const gchar *sender_name G_GNUC_UNUSED,
                                     const gchar *signal_name,
                                     GVariant    *parameters)
{
    GPasteScreensaverClient *self = G_PASTE_SCREENSAVER_CLIENT (proxy);

    if (g_paste_str_equal (signal_name, G_PASTE_SCREENSAVER_SIG_ACTIVE_CHANGED))
    {
        GVariantIter params_iter;
        g_variant_iter_init (&params_iter, parameters);
        g_autoptr (GVariant) value = g_variant_iter_next_value (&params_iter);
        gboolean active = g_variant_get_boolean (value);

        /* The screensaver re-announces the state it is already in (the
         * deactivate signal is sent unconditionally), so filter here rather
         * than making every handler check whether anything moved. */
        if (self->active == active)
            return;

        self->active = active;
        g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ACTIVE]);
    }
}

/**
 * g_paste_screensaver_client_is_active:
 * @self: a #GPasteScreensaverClient instance
 *
 * Whether the screensaver is currently showing.
 *
 * Returns: %TRUE when the screensaver is active
 */
G_PASTE_VISIBLE gboolean
g_paste_screensaver_client_is_active (GPasteScreensaverClient *self)
{
    g_return_val_if_fail (G_PASTE_IS_SCREENSAVER_CLIENT (self), FALSE);

    return self->active;
}

static void
g_paste_screensaver_client_class_init (GPasteScreensaverClientClass *klass)
{
    G_OBJECT_CLASS (klass)->get_property = g_paste_screensaver_client_get_property;
    G_DBUS_PROXY_CLASS (klass)->g_signal = g_paste_screensaver_client_g_signal;

    /**
     * GPasteScreensaverClient:active:
     *
     * Whether the screensaver is currently showing.
     *
     * Read-only: the screensaver owns this, we only mirror what it announces.
     * G_PARAM_EXPLICIT_NOTIFY because the notify comes from the D-Bus signal
     * handler, once the mirrored value has actually changed.
     */
    properties[PROP_ACTIVE] = g_param_spec_boolean ("active", NULL, NULL, FALSE,
                                                    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

    g_object_class_install_properties (G_OBJECT_CLASS (klass), N_PROPERTIES, properties);
}

static void
g_paste_screensaver_client_init (GPasteScreensaverClient *self)
{
    GDBusProxy *proxy = G_DBUS_PROXY (self);
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusNodeInfo) screensaver_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_SCREENSAVER_INTERFACE,
                                                                                    &error);
    g_assert_no_error (error);

    g_dbus_proxy_set_interface_info (proxy, screensaver_dbus_info->interfaces[0]);
}

/**
 * g_paste_screensaver_client_new_sync:
 * @error: return location for a #GError, or %NULL
 *
 * Create a new instance of #GPasteScreensaverClient
 *
 * A failure is a %G_DBUS_ERROR or a %G_IO_ERROR: this only reaches the bus.
 *
 * Returns: a newly allocated #GPasteScreensaverClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteScreensaverClient *
g_paste_screensaver_client_new_sync (GError **error)
{
    GInitable *self = g_initable_new (G_PASTE_TYPE_SCREENSAVER_CLIENT,
                                      NULL, /* cancellable */
                                      error,
                                      "g-bus-type",       G_BUS_TYPE_SESSION,
                                      "g-flags",          G_DBUS_PROXY_FLAGS_NONE,
                                      "g-name",           G_PASTE_SCREENSAVER_BUS_NAME,
                                      "g-object-path",    G_PASTE_SCREENSAVER_OBJECT_PATH,
                                      "g-interface-name", G_PASTE_SCREENSAVER_INTERFACE_NAME,
                                      NULL);

    return (self) ? G_PASTE_SCREENSAVER_CLIENT (self) : NULL;
}

/**
 * g_paste_screensaver_client_new:
 * @callback: Callback function to invoke when the proxy is ready.
 * @user_data: User data to pass to @callback.
 *
 * Create a new instance of #GPasteScreensaverClient
 */
G_PASTE_VISIBLE void
g_paste_screensaver_client_new (GAsyncReadyCallback callback,
                                gpointer            user_data)
{
    g_async_initable_new_async (G_PASTE_TYPE_SCREENSAVER_CLIENT,
                                G_PRIORITY_DEFAULT,
                                NULL, /* cancellable */
                                callback,
                                user_data,
                                "g-bus-type",       G_BUS_TYPE_SESSION,
                                "g-flags",          G_DBUS_PROXY_FLAGS_NONE,
                                "g-name",           G_PASTE_SCREENSAVER_BUS_NAME,
                                "g-object-path",    G_PASTE_SCREENSAVER_OBJECT_PATH,
                                "g-interface-name", G_PASTE_SCREENSAVER_INTERFACE_NAME,
                                NULL);
}

/**
 * g_paste_screensaver_client_new_finish:
 * @result: A #GAsyncResult obtained from the #GAsyncReadyCallback function passed to the async ctor.
 * @error: return location for a #GError, or %NULL
 *
 * Create a new instance of #GPasteScreensaverClient
 *
 * A failure is a %G_DBUS_ERROR or a %G_IO_ERROR: this only reaches the bus.
 *
 * Returns: a newly allocated #GPasteScreensaverClient
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteScreensaverClient *
g_paste_screensaver_client_new_finish (GAsyncResult *result,
                                       GError      **error)
{
    g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
    g_return_val_if_fail (!error || !(*error), NULL);

    g_autoptr (GObject) source = g_async_result_get_source_object (result);

    g_assert (source);

    GObject *self = g_async_initable_new_finish (G_ASYNC_INITABLE (source), result, error);

    return (self) ? G_PASTE_SCREENSAVER_CLIENT (self) : NULL;
}
