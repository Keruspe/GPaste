/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gdbus-defines.h>
#include <gpaste-search-provider.h>
#include <gpaste-util.h>

#include <string.h>

struct _GPasteSearchProvider
{
    GPasteBusObject parent_instance;
};

typedef struct
{
    GDBusConnection     *connection;
    guint64              id_on_bus;
    gboolean             registered;

    GPasteClient        *client;

    GDBusNodeInfo       *g_paste_search_provider_dbus_info;
    GDBusInterfaceVTable g_paste_search_provider_dbus_vtable;
} GPasteSearchProviderPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (SearchProvider, search_provider, G_PASTE_TYPE_BUS_OBJECT)

static char *
g_paste_dbus_get_as_result (GVariant *variant)
{
    guint64 _len;
    g_autofree const gchar **r = g_variant_get_strv (variant, &_len);

    return g_strjoinv (" ", (gchar **) r);
}

static char *
_g_paste_dbus_get_as_result (GVariant *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
    return g_paste_dbus_get_as_result (variant);
}

/****************/
/* DBus Mathods */
/****************/

static void
on_search_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    g_autofree gpointer *data = (gpointer *) user_data;
    GPasteClient *client = data[0];
    GDBusMethodInvocation *invocation = data[1];
    g_auto (GStrv) results = g_paste_client_search_finish (client, res, NULL /* Error */);

    GVariant *ans = g_variant_new_strv ((const char * const *) results, -1);
    g_dbus_method_invocation_return_value (invocation, g_variant_new_tuple (&ans, 1));
}

static gboolean
_do_search (const GPasteSearchProviderPrivate *priv,
            gchar                             *search,
            GDBusMethodInvocation             *invocation)
{
    if (strlen (search) < 3 || !priv->client)
    {
        GVariant *ans = g_variant_new_strv (NULL, 0);
        g_dbus_method_invocation_return_value (invocation, g_variant_new_tuple (&ans, 1));
    }
    else
    {
        gpointer *data = g_new (gpointer, 2);

        data[0] = priv->client;
        data[1] = invocation;

        g_paste_client_search (priv->client,
                               search,
                               on_search_ready,
                               data);
    }

    return TRUE;
}

static gboolean
g_paste_search_provider_private_get_initial_result_set (const GPasteSearchProviderPrivate *priv,
                                                        GDBusMethodInvocation             *invocation,
                                                        GVariant                          *parameters)
{
    g_autofree gchar *search = _g_paste_dbus_get_as_result (parameters);
    return _do_search (priv, search, invocation);
}

static gboolean
g_paste_search_provider_private_get_subsearch_result_set (const GPasteSearchProviderPrivate *priv,
                                                          GDBusMethodInvocation             *invocation,
                                                          GVariant                          *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    G_GNUC_UNUSED g_autoptr (GVariant) old_results = g_variant_iter_next_value (&parameters_iter);
    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
    g_autofree gchar *search = g_paste_dbus_get_as_result (variant);

    return _do_search (priv, search, invocation);
}

static void
append_dict_entry (GVariantBuilder *dict,
                   const gchar     *key,
                   const gchar     *value)
{
    g_variant_builder_add_value (dict, g_variant_new_dict_entry (g_variant_new_string (key),
                                                                 g_variant_new_variant (g_variant_new_string (value))));
}

typedef struct
{
    GPasteClient          *client;
    GDBusMethodInvocation *invocation;
    const gchar          **uuids;
} GetResultMetasData;

static void
on_elements_ready (GObject      *source_object G_GNUC_UNUSED,
                   GAsyncResult *res,
                   gpointer      user_data)
{
    g_autofree GetResultMetasData *data = user_data;
    GPasteClient *client = data->client;
    g_autofree const gchar **uuids = data->uuids;
    g_auto (GVariantBuilder) builder;

    g_variant_builder_init (&builder, (GVariantType *) "aa{sv}");

    GList *results = g_paste_client_get_elements_finish (client, res, NULL /* Error */);
    guint64 n = 0;

    for (const GList *i = results; i; i = i->next, ++n)
    {
        const GPasteClientItem *item = i->data;
        const gchar *value = g_paste_client_item_get_value (item);
        g_auto (GVariantBuilder) dict;
        g_autofree gchar *result = g_paste_util_replace (value, "\n", " ");

        g_variant_builder_init (&dict, G_VARIANT_TYPE_VARDICT);

        append_dict_entry (&dict, "id", uuids[n]);
        append_dict_entry (&dict, "name", result);
        append_dict_entry (&dict, "gicon", G_PASTE_ICON_NAME);
        append_dict_entry (&dict, "clipboardText", value);

        g_variant_builder_add_value (&builder, g_variant_builder_end (&dict));
    }

    GVariant *ans = g_variant_builder_end (&builder);
    g_dbus_method_invocation_return_value (data->invocation, g_variant_new_tuple (&ans, 1));

    g_list_free_full (results, g_object_unref);
}

static gboolean
g_paste_search_provider_private_get_result_metas (const GPasteSearchProviderPrivate *priv,
                                                  GDBusMethodInvocation             *invocation,
                                                  GVariant                          *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) results = g_variant_iter_next_value (&parameters_iter);
    guint64 len;
    g_autofree const gchar **uuids = g_variant_get_strv (results, &len);

    if (!len)
        return FALSE;

    GetResultMetasData *data = g_new (GetResultMetasData, 1);

    data->client = priv->client;
    data->invocation = invocation;
    data->uuids = uuids;
    uuids = NULL; // don't autofree

    g_paste_client_get_elements (priv->client, data->uuids, len, on_elements_ready, data);

    return TRUE;
}

static gboolean
g_paste_search_provider_private_activate_result (const GPasteSearchProviderPrivate *priv G_GNUC_UNUSED,
                                                 GVariant                          *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) indexv = g_variant_iter_next_value (&parameters_iter);
    G_GNUC_UNUSED g_autoptr (GVariant) terms = g_variant_iter_next_value (&parameters_iter);
    G_GNUC_UNUSED g_autoptr (GVariant) timestamp = g_variant_iter_next_value (&parameters_iter);

    g_paste_client_select (priv->client, g_variant_get_string (indexv, NULL), NULL, NULL);

    return FALSE;
}

static gboolean
g_paste_search_provider_private_launch_search (const GPasteSearchProviderPrivate *priv G_GNUC_UNUSED,
                                               GVariant                          *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) searchv = g_variant_iter_next_value (&parameters_iter);
    G_GNUC_UNUSED g_autoptr (GVariant) timestamp = g_variant_iter_next_value (&parameters_iter);
    g_autofree gchar *search = g_paste_dbus_get_as_result (searchv);

    g_paste_util_activate_ui ("search", g_variant_new_string (search));

    return FALSE;
}

static void
g_paste_search_provider_dbus_method_call (GDBusConnection       *connection     G_GNUC_UNUSED,
                                          const gchar           *sender         G_GNUC_UNUSED,
                                          const gchar           *object_path    G_GNUC_UNUSED,
                                          const gchar           *interface_name G_GNUC_UNUSED,
                                          const gchar           *method_name,
                                          GVariant              *parameters,
                                          GDBusMethodInvocation *invocation,
                                          gpointer               user_data)
{
    GPasteSearchProvider *self = user_data;
    const GPasteSearchProviderPrivate *priv = _g_paste_search_provider_get_instance_private (self);
    gboolean async = FALSE;

    if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_GET_INITIAL_RESULT_SET))
        async = g_paste_search_provider_private_get_initial_result_set (priv, invocation, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_GET_SUBSEARCH_RESULT_SET))
        async = g_paste_search_provider_private_get_subsearch_result_set (priv, invocation, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_GET_RESULT_METAS))
        async = g_paste_search_provider_private_get_result_metas (priv, invocation, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_ACTIVATE_RESULT))
        async = g_paste_search_provider_private_activate_result (priv, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_LAUNCH_SEARCH))
        async = g_paste_search_provider_private_launch_search (priv, parameters);

    if (!async)
        g_dbus_method_invocation_return_value (invocation, NULL);
}

static void
g_paste_search_provider_unregister_object (gpointer user_data)
{
    g_autoptr (GPasteSearchProvider) self = G_PASTE_SEARCH_PROVIDER (user_data);
    GPasteSearchProviderPrivate *priv = g_paste_search_provider_get_instance_private (self);

    priv->registered = FALSE;
}

static void
g_paste_search_provider_dispose (GObject *object)
{
    GPasteSearchProviderPrivate *priv = g_paste_search_provider_get_instance_private (G_PASTE_SEARCH_PROVIDER (object));

    if (priv->connection)
    {
        g_dbus_connection_unregister_object (priv->connection, priv->id_on_bus);
        g_clear_object (&priv->connection);
        g_dbus_node_info_unref (priv->g_paste_search_provider_dbus_info);
        g_clear_object (&priv->client);
    }

    G_OBJECT_CLASS (g_paste_search_provider_parent_class)->dispose (object);
}

static gboolean
g_paste_search_provider_register_on_connection (GPasteBusObject *self,
                                                GDBusConnection *connection,
                                                GError         **error)
{
    GPasteSearchProviderPrivate *priv = g_paste_search_provider_get_instance_private (G_PASTE_SEARCH_PROVIDER (self));

    g_clear_object (&priv->connection);
    priv->connection = g_object_ref (connection);

    priv->id_on_bus = g_dbus_connection_register_object (connection,
                                                         G_PASTE_SEARCH_PROVIDER_OBJECT_PATH,
                                                         priv->g_paste_search_provider_dbus_info->interfaces[0],
                                                         &priv->g_paste_search_provider_dbus_vtable,
                                                         g_object_ref (self),
                                                         g_paste_search_provider_unregister_object,
                                                         error);

    return (priv->registered = !!priv->id_on_bus);
}

static void
g_paste_search_provider_class_init (GPasteSearchProviderClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_search_provider_dispose;
    G_PASTE_BUS_OBJECT_CLASS (klass)->register_on_connection = g_paste_search_provider_register_on_connection;
}

static void
on_client_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    GPasteSearchProviderPrivate *priv = user_data;

    priv->client = g_paste_client_new_finish (res,
                                              NULL); /* Error */
}

static void
g_paste_search_provider_init (GPasteSearchProvider *self)
{
    GPasteSearchProviderPrivate *priv = g_paste_search_provider_get_instance_private (self);
    GDBusInterfaceVTable *vtable = &priv->g_paste_search_provider_dbus_vtable;

    priv->id_on_bus = 0;
    priv->g_paste_search_provider_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_SEARCH_PROVIDER_INTERFACE,
                                                                            NULL); /* Error */

    vtable->method_call = g_paste_search_provider_dbus_method_call;
    vtable->get_property = NULL;
    vtable->set_property = NULL;

    g_paste_client_new (on_client_ready, priv);
}

/**
 * g_paste_search_provider_new:
 *
 * Create a new instance of #GPasteSearchProvider
 *
 * Returns: a newly allocated #GPasteSearchProvider
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteBusObject *
g_paste_search_provider_new (void)
{
    return G_PASTE_BUS_OBJECT (g_object_new (G_PASTE_TYPE_SEARCH_PROVIDER, NULL));
}
