// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-gdbus-defines.h>
#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-search-provider.h>

#include <string.h>

struct _GPasteSearchProvider
{
    GPasteBusObject parent_instance;

    GDBusConnection     *connection;
    guint64              id_on_bus;
    gboolean             registered;

    GPasteClient        *client;

    GDBusNodeInfo       *g_paste_search_provider_dbus_info;
    GDBusInterfaceVTable g_paste_search_provider_dbus_vtable;
};

G_PASTE_DEFINE_TYPE (SearchProvider, search_provider, G_PASTE_TYPE_BUS_OBJECT)

static char *
g_paste_dbus_get_as_result (GVariant *variant)
{
    gsize _len;
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
    /* Reffed by the caller: the provider owns the only other reference and may
     * be finalized while this is in flight. The invocation is not reffed -- GDBus
     * hands it to the method handler and return_value() consumes it. */
    g_autoptr (GPasteClient) client = data[0];
    GDBusMethodInvocation *invocation = data[1];
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) results = g_paste_client_search_finish (client, res, &error);
    if (error)
        g_warning ("GPaste search failed: %s", error->message);

    GVariant *ans = g_variant_new_strv ((const char * const *) results, results ? -1 : 0);
    g_dbus_method_invocation_return_value (invocation, g_variant_new_tuple (&ans, 1));
}

static gboolean
_do_search (GPasteSearchProvider  *priv,
            gchar                 *search,
            GDBusMethodInvocation *invocation)
{
    if (strlen (search) < 3 || !priv->client)
    {
        GVariant *ans = g_variant_new_strv (NULL, 0);
        g_dbus_method_invocation_return_value (invocation, g_variant_new_tuple (&ans, 1));
    }
    else
    {
        gpointer *data = g_new (gpointer, 2);

        data[0] = g_object_ref (priv->client);
        data[1] = invocation;

        g_paste_client_search (priv->client,
                               search,
                               on_search_ready,
                               data);
    }

    return TRUE;
}

static gboolean
g_paste_search_provider_private_get_initial_result_set (GPasteSearchProvider  *priv,
                                                        GDBusMethodInvocation *invocation,
                                                        GVariant              *parameters)
{
    g_autofree gchar *search = _g_paste_dbus_get_as_result (parameters);
    return _do_search (priv, search, invocation);
}

static gboolean
g_paste_search_provider_private_get_subsearch_result_set (GPasteSearchProvider  *priv,
                                                          GDBusMethodInvocation *invocation,
                                                          GVariant              *parameters)
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
    GStrv                  uuids;
} GetResultMetasData;

static void
on_elements_ready (GObject      *source_object G_GNUC_UNUSED,
                   GAsyncResult *res,
                   gpointer      user_data)
{
    g_autofree GetResultMetasData *data = user_data;
    /* See on_search_ready: the client is reffed for us, the invocation is not. */
    g_autoptr (GPasteClient) client = data->client;
    g_auto (GStrv) uuids = data->uuids;
    /* Initialized in the declaration: a g_auto() builder that goes out of scope
     * before its init() would have g_variant_builder_clear() run on stack
     * garbage. */
    g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));

    g_autoptr (GError) error = NULL;
    g_autolist (GPasteClientItem) results = g_paste_client_get_elements_finish (client, res, &error);
    if (error)
        g_warning ("GPaste get elements failed: %s", error->message);
    guint64 n = 0;

    for (const GList *i = results; i; i = i->next, ++n)
    {
        GPasteClientItem *item = i->data;
        const gchar *value = g_paste_client_item_get_value (item);
        g_auto (GVariantBuilder) dict = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
        g_autofree gchar *result = g_strdelimit (g_strdup (value), "\n\t", ' ');

        append_dict_entry (&dict, "id", uuids[n]);
        append_dict_entry (&dict, "name", result);
        append_dict_entry (&dict, "gicon", G_PASTE_ICON_NAME);
        append_dict_entry (&dict, "clipboardText", value);

        g_variant_builder_add_value (&builder, g_variant_builder_end (&dict));
    }

    GVariant *ans = g_variant_builder_end (&builder);
    g_dbus_method_invocation_return_value (data->invocation, g_variant_new_tuple (&ans, 1));
}

static gboolean
g_paste_search_provider_private_get_result_metas (GPasteSearchProvider  *priv,
                                                  GDBusMethodInvocation *invocation,
                                                  GVariant              *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) results = g_variant_iter_next_value (&parameters_iter);
    gsize len;
    /* Deep-copy: the strings must outlive `results` since they are used again
     * in the async on_elements_ready callback. */
    g_auto (GStrv) uuids = g_variant_dup_strv (results, &len);

    /* Nothing to describe, or no daemon connection to describe it with: answer
     * an empty (but correctly typed) result set rather than the NULL the
     * caller's generic "not async" path would return for an "aa{sv}" method. */
    if (!len || !priv->client)
    {
        g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));
        GVariant *ans = g_variant_builder_end (&builder);

        g_dbus_method_invocation_return_value (invocation, g_variant_new_tuple (&ans, 1));

        return TRUE;
    }

    GetResultMetasData *data = g_new (GetResultMetasData, 1);

    data->client = g_object_ref (priv->client);
    data->invocation = invocation;
    data->uuids = g_steal_pointer (&uuids);

    g_paste_client_get_elements (priv->client, (const gchar **) data->uuids, len, on_elements_ready, data);

    return TRUE;
}

static gboolean
g_paste_search_provider_private_activate_result (GPasteSearchProvider *priv,
                                                 GVariant             *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) indexv = g_variant_iter_next_value (&parameters_iter);
    G_GNUC_UNUSED g_autoptr (GVariant) terms = g_variant_iter_next_value (&parameters_iter);
    G_GNUC_UNUSED g_autoptr (GVariant) timestamp = g_variant_iter_next_value (&parameters_iter);

    /* Failing to reach the daemon is no longer fatal to this process, so the
     * client can legitimately be missing for good — as the other two entry
     * points already assume. */
    if (priv->client)
        g_paste_client_select (priv->client, g_variant_get_string (indexv, NULL), NULL, NULL);

    return FALSE;
}

static gboolean
g_paste_search_provider_private_launch_search (GPasteSearchProvider *priv G_GNUC_UNUSED,
                                               GVariant             *parameters)
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
    gboolean async = FALSE;

    if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_GET_INITIAL_RESULT_SET))
        async = g_paste_search_provider_private_get_initial_result_set (self, invocation, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_GET_SUBSEARCH_RESULT_SET))
        async = g_paste_search_provider_private_get_subsearch_result_set (self, invocation, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_GET_RESULT_METAS))
        async = g_paste_search_provider_private_get_result_metas (self, invocation, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_ACTIVATE_RESULT))
        async = g_paste_search_provider_private_activate_result (self, parameters);
    else if (g_paste_str_equal (method_name, G_PASTE_SEARCH_PROVIDER_LAUNCH_SEARCH))
        async = g_paste_search_provider_private_launch_search (self, parameters);

    if (!async)
        g_dbus_method_invocation_return_value (invocation, NULL);
}

static void
g_paste_search_provider_unregister_object (gpointer user_data)
{
    g_autoptr (GPasteSearchProvider) self = G_PASTE_SEARCH_PROVIDER (user_data);

    self->registered = FALSE;
}

/* See g_paste_bus_object_unregister_on_connection(): the registration owns a
 * reference on us, so it has to be dropped explicitly or the object path stays
 * exported on a connection that outlives the daemon (gnome-shell's). */
static void
g_paste_search_provider_unregister_on_connection (GPasteBusObject *self)
{
    GPasteSearchProvider *priv = G_PASTE_SEARCH_PROVIDER (self);

    if (!priv->connection)
        return;

    g_dbus_connection_unregister_object (priv->connection, priv->id_on_bus);
    priv->id_on_bus = 0;
    g_clear_object (&priv->connection);
}

static void
g_paste_search_provider_dispose (GObject *object)
{
    GPasteSearchProvider *self = G_PASTE_SEARCH_PROVIDER (object);

    if (self->connection)
    {
        g_dbus_connection_unregister_object (self->connection, self->id_on_bus);
        self->id_on_bus = 0;
        g_clear_object (&self->connection);
    }

    /* Not gated on the connection: it may already have been dropped by
     * g_paste_search_provider_unregister_on_connection(). */
    g_clear_pointer (&self->g_paste_search_provider_dbus_info, g_dbus_node_info_unref);
    g_clear_object (&self->client);

    G_OBJECT_CLASS (g_paste_search_provider_parent_class)->dispose (object);
}

static gboolean
g_paste_search_provider_register_on_connection (GPasteBusObject *self,
                                                GDBusConnection *connection,
                                                GError         **error)
{
    GPasteSearchProvider *priv = G_PASTE_SEARCH_PROVIDER (self);

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
    G_PASTE_BUS_OBJECT_CLASS (klass)->unregister_on_connection = g_paste_search_provider_unregister_on_connection;
}

static void
on_client_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    /* The callback owns this ref: holding it is what keeps dispose from running
     * while the client is being created, so the assignment below always lands in
     * a live provider and is cleared by the dispose our unref triggers. */
    g_autoptr (GPasteSearchProvider) self = user_data;
    g_autoptr (GError) error = NULL;

    self->client = g_paste_client_new_finish (res, &error);

    /* Never abort here: this provider is also built inside gnome-shell, where
     * g_error() would take the whole compositor down over a transient D-Bus
     * failure. Without a client every search simply answers empty (_do_search). */
    if (error)
        g_warning ("Failed to connect to GPaste daemon, searching is disabled: %s", error->message);
}

static void
g_paste_search_provider_init (GPasteSearchProvider *self)
{
    GDBusInterfaceVTable *vtable = &self->g_paste_search_provider_dbus_vtable;

    self->id_on_bus = 0;
    g_autoptr (GError) error = NULL;
    self->g_paste_search_provider_dbus_info = g_dbus_node_info_new_for_xml (G_PASTE_SEARCH_PROVIDER_INTERFACE,
                                                                            &error);
    g_assert_no_error (error);

    vtable->method_call = g_paste_search_provider_dbus_method_call;
    vtable->get_property = NULL;
    vtable->set_property = NULL;

    g_paste_client_new (on_client_ready, g_object_ref (self));
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
