// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gdbus-defines.h>
#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-search-provider.h>
#include <gpaste-daemon/gpaste-shell-search-provider2.h>

#include <string.h>

struct _GPasteSearchProvider
{
    GPasteBusObject parent_instance;

    GPasteShellSearchProvider2 *skeleton;
    gboolean                    registered;

    GPasteClient               *client;

    /* uuid -> value, for the results of the last search we answered. The shell
     * asks for the metas of identifiers it has just been handed, so this is
     * nearly always the very set that reply carried, and describing them from
     * here is what keeps GetResultMetas from asking for the same strings a
     * second time. Replaced wholesale by each search, and emptied by any update
     * to the history, so what is answered from here was true as of the last
     * thing that happened to it; a uuid it does not hold falls back to
     * GetItems. */
    GHashTable                 *last_results;
};

G_PASTE_DEFINE_TYPE (SearchProvider, search_provider, G_PASTE_TYPE_BUS_OBJECT)

/* GetInitialResultSet and GetSubsearchResultSet answer the same way but have
 * their own completion, so the search path carries whichever one applies. */
typedef void (*SearchCompleteFunc) (GPasteShellSearchProvider2 *object,
                                    GDBusMethodInvocation      *invocation,
                                    const gchar * const        *results);

typedef struct
{
    /* Reffed: the provider owns the client and the skeleton this needs on the
     * way back, and it may be finalized while the call is in flight. The
     * invocation is not reffed -- GDBus hands it to the handler and completing
     * it consumes it. */
    GPasteSearchProvider  *provider;
    GDBusMethodInvocation *invocation;
    SearchCompleteFunc     complete;
} SearchData;

static void
on_search_ready (GObject      *source_object G_GNUC_UNUSED,
                 GAsyncResult *res,
                 gpointer      user_data)
{
    g_autofree SearchData *data = user_data;
    g_autoptr (GPasteSearchProvider) self = data->provider;
    g_autoptr (GError) error = NULL;
    g_autolist (GPasteClientItem) results = g_paste_client_search_finish (self->client, res, &error);

    if (error)
        g_warning ("GPaste search failed: %s", error->message);

    /* The shell's interface takes identifiers and asks for their metas
     * separately -- ours is the only interface where that split is imposed
     * rather than chosen -- so the values this reply carries are kept here
     * rather than asked for again a moment later. */
    g_autoptr (GStrvBuilder) ids = g_strv_builder_new ();

    g_hash_table_remove_all (self->last_results);

    for (const GList *i = results; i; i = i->next)
    {
        const gchar *uuid = g_paste_client_item_get_uuid (i->data);

        g_strv_builder_add (ids, uuid);
        g_hash_table_insert (self->last_results, g_strdup (uuid), g_strdup (g_paste_client_item_get_value (i->data)));
    }

    g_auto (GStrv) identifiers = g_strv_builder_end (ids);

    data->complete (self->skeleton, data->invocation, (const gchar * const *) identifiers);
}

/* @terms is how the shell splits what was typed; GPaste searches the whole
 * phrase, so they go back together. */
static gchar *
g_paste_search_provider_join_terms (const gchar * const *terms)
{
    return g_strjoinv (" ", (GStrv) terms);
}

static gboolean
g_paste_search_provider_search (GPasteSearchProvider  *self,
                                GDBusMethodInvocation *invocation,
                                const gchar * const   *terms,
                                SearchCompleteFunc     complete)
{
    g_autofree gchar *search = g_paste_search_provider_join_terms (terms);

    /* Too short to be worth a round trip, or no daemon to make it against. */
    if (strlen (search) < 3 || !self->client)
    {
        const gchar *empty[] = { NULL };

        complete (self->skeleton, invocation, empty);

        return TRUE;
    }

    SearchData *data = g_new (SearchData, 1);

    data->provider = g_object_ref (self);
    data->invocation = invocation;
    data->complete = complete;

    g_paste_client_search (self->client, search, on_search_ready, data);

    return TRUE;
}

static gboolean
g_paste_search_provider_handle_get_initial_result_set (GPasteSearchProvider  *self,
                                                       GDBusMethodInvocation *invocation,
                                                       const gchar * const   *terms)
{
    return g_paste_search_provider_search (self, invocation, terms,
                                           g_paste_shell_search_provider2_complete_get_initial_result_set);
}

static gboolean
g_paste_search_provider_handle_get_subsearch_result_set (GPasteSearchProvider  *self,
                                                         GDBusMethodInvocation *invocation,
                                                         const gchar * const   *previous_results G_GNUC_UNUSED,
                                                         const gchar * const   *terms)
{
    return g_paste_search_provider_search (self, invocation, terms,
                                           g_paste_shell_search_provider2_complete_get_subsearch_result_set);
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
    /* Same ownership as SearchData, plus the uuids: they are what the call was
     * made with, and what the fallback picks back out of the history. */
    GPasteSearchProvider  *provider;
    GDBusMethodInvocation *invocation;
    GStrv                  uuids;
} GetResultMetasData;

static void
append_meta (GVariantBuilder *builder,
             const gchar     *uuid,
             const gchar     *value)
{
    g_auto (GVariantBuilder) dict = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE_VARDICT);
    g_autofree gchar *result = g_paste_util_one_line (value);

    append_dict_entry (&dict, "id", uuid);
    append_dict_entry (&dict, "name", result);
    append_dict_entry (&dict, "gicon", G_PASTE_ICON_NAME);
    append_dict_entry (&dict, "clipboardText", value);

    g_variant_builder_add_value (builder, g_variant_builder_end (&dict));
}

static void
get_result_metas_data_free (GetResultMetasData *data)
{
    g_object_unref (data->provider);
    g_strfreev (data->uuids);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GetResultMetasData, get_result_metas_data_free)

/* The fallback path: describe the identifiers that are still in the history and
 * quietly drop the ones that are not. Reached when the batch below failed, which
 * it does as a whole as soon as one uuid has gone. */
static void
on_history_ready (GObject      *source_object G_GNUC_UNUSED,
                  GAsyncResult *res,
                  gpointer      user_data)
{
    g_autoptr (GetResultMetasData) data = user_data;
    GPasteSearchProvider *self = data->provider;
    g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));
    g_autoptr (GError) error = NULL;
    g_autolist (GPasteClientItem) history = g_paste_client_get_history_finish (self->client, res, &error);

    if (error)
        g_warning ("GPaste get history failed: %s", error->message);

    /* Indexed rather than scanned per uuid: this runs on GetResultMetas, which
     * the shell calls on every keystroke once one identifier has gone, and a
     * full history against a screenful of results is tens of thousands of
     * comparisons each time. The items belong to @history throughout. */
    g_autoptr (GHashTable) items = g_hash_table_new (g_str_hash, g_str_equal);

    for (const GList *i = history; i; i = i->next)
        g_hash_table_insert (items, (gpointer) g_paste_client_item_get_uuid (i->data), i->data);

    for (GStrv uuid = data->uuids; *uuid; ++uuid)
    {
        GPasteClientItem *item = g_hash_table_lookup (items, *uuid);

        if (item)
            append_meta (&builder, *uuid, g_paste_client_item_get_value (item));
    }

    g_paste_shell_search_provider2_complete_get_result_metas (self->skeleton, data->invocation, g_variant_builder_end (&builder));
}

static void
on_items_ready (GObject      *source_object G_GNUC_UNUSED,
                GAsyncResult *res,
                gpointer      user_data)
{
    g_autoptr (GetResultMetasData) data = user_data;
    GPasteSearchProvider *self = data->provider;
    /* Initialized in the declaration: a g_auto() builder that goes out of scope
     * before its init() would have g_variant_builder_clear() run on stack
     * garbage. */
    g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));

    g_autoptr (GError) error = NULL;
    g_autolist (GPasteClientItem) results = g_paste_client_get_items_finish (self->client, res, &error);

    /* GetItems is all-or-nothing: one uuid the history no longer holds (it was
     * capped away between the search and this call) fails the lot with
     * %G_PASTE_ERROR_INVALID_INDEX. Describing none of them would drop every
     * result the shell is about to show, so fall back to picking out of the
     * history whatever survives.
     *
     * Only for that one error: a daemon that has gone away, or a call that timed
     * out, would otherwise have every GetResultMetas pull the whole history over
     * the bus to build the very empty answer we can complete with right now. */
    if (!results)
    {
        if (g_error_matches (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_INDEX))
        {
            g_paste_client_get_history (self->client, on_history_ready, g_steal_pointer (&data));

            return;
        }

        if (error)
            g_warning ("GPaste get items failed: %s", error->message);
    }

    /* Each item names itself, so nothing here depends on the reply coming back
     * in the order the uuids were asked in. */
    for (const GList *i = results; i; i = i->next)
        append_meta (&builder, g_paste_client_item_get_uuid (i->data), g_paste_client_item_get_value (i->data));

    g_paste_shell_search_provider2_complete_get_result_metas (self->skeleton, data->invocation, g_variant_builder_end (&builder));
}

/* Whether every one of @identifiers came out of the search we last answered. */
static gboolean
g_paste_search_provider_metas_are_known (GPasteSearchProvider *self,
                                         const gchar * const  *identifiers)
{
    for (const gchar * const *uuid = identifiers; *uuid; ++uuid)
    {
        if (!g_hash_table_contains (self->last_results, *uuid))
            return FALSE;
    }

    return TRUE;
}

static gboolean
g_paste_search_provider_handle_get_result_metas (GPasteSearchProvider  *self,
                                                 GDBusMethodInvocation *invocation,
                                                 const gchar * const   *identifiers)
{
    gsize len = (identifiers) ? g_strv_length ((GStrv) identifiers) : 0;

    /* Nothing to describe, or no daemon connection to describe it with: answer
     * an empty (but correctly typed) result set. */
    if (!len || !self->client)
    {
        g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));

        g_paste_shell_search_provider2_complete_get_result_metas (self->skeleton, invocation, g_variant_builder_end (&builder));

        return TRUE;
    }

    /* The identifiers are the ones the last search answered with, so their
     * values are already here: describe them without a round trip at all, and
     * without the daemon marshalling the same strings a second time. All or
     * nothing -- one identifier from an older search, and the batch below is
     * both cheaper than a call per uuid and fresher than what is kept here. */
    if (g_paste_search_provider_metas_are_known (self, identifiers))
    {
        g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_VARIANT_TYPE ("aa{sv}"));

        for (const gchar * const *uuid = identifiers; *uuid; ++uuid)
            append_meta (&builder, *uuid, g_hash_table_lookup (self->last_results, *uuid));

        g_paste_shell_search_provider2_complete_get_result_metas (self->skeleton, invocation, g_variant_builder_end (&builder));

        return TRUE;
    }

    GetResultMetasData *data = g_new (GetResultMetasData, 1);

    data->provider = g_object_ref (self);
    data->invocation = invocation;
    /* Deep-copy: the strings must outlive @identifiers, since the call reads
     * them and the fallback matches on them once it comes back. */
    data->uuids = g_strdupv ((GStrv) identifiers);

    g_paste_client_get_items (self->client, (const gchar * const *) data->uuids, on_items_ready, data);

    return TRUE;
}

static gboolean
g_paste_search_provider_handle_activate_result (GPasteSearchProvider  *self,
                                                GDBusMethodInvocation *invocation,
                                                const gchar           *identifier,
                                                const gchar * const   *terms G_GNUC_UNUSED,
                                                guint                  timestamp G_GNUC_UNUSED)
{
    /* Failing to reach the daemon is not fatal to this process, so the client
     * can legitimately be missing for good — as the other entry points already
     * assume. */
    if (self->client)
        g_paste_client_select (self->client, identifier, NULL, NULL);

    g_paste_shell_search_provider2_complete_activate_result (self->skeleton, invocation);

    return TRUE;
}

static gboolean
g_paste_search_provider_handle_launch_search (GPasteSearchProvider  *self,
                                              GDBusMethodInvocation *invocation,
                                              const gchar * const   *terms,
                                              guint                  timestamp G_GNUC_UNUSED)
{
    g_autofree gchar *search = g_paste_search_provider_join_terms (terms);

    g_paste_util_activate_ui ("search", g_variant_new_string (search));

    g_paste_shell_search_provider2_complete_launch_search (self->skeleton, invocation);

    return TRUE;
}

/* See g_paste_bus_object_unregister_on_connection(): the export has to be
 * dropped explicitly or the object path stays exported on a connection that
 * outlives the daemon (gnome-shell's). */
static void
g_paste_search_provider_unregister_on_connection (GPasteBusObject *object)
{
    GPasteSearchProvider *self = G_PASTE_SEARCH_PROVIDER (object);

    if (!self->registered)
        return;

    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->skeleton));
    self->registered = FALSE;
}

static void
g_paste_search_provider_dispose (GObject *object)
{
    GPasteSearchProvider *self = G_PASTE_SEARCH_PROVIDER (object);

    g_paste_search_provider_unregister_on_connection (G_PASTE_BUS_OBJECT (self));

    g_clear_object (&self->skeleton);
    g_clear_object (&self->client);
    g_clear_pointer (&self->last_results, g_hash_table_unref);

    G_OBJECT_CLASS (g_paste_search_provider_parent_class)->dispose (object);
}

static gboolean
g_paste_search_provider_register_on_connection (GPasteBusObject *object,
                                                GDBusConnection *connection,
                                                GError         **error)
{
    GPasteSearchProvider *self = G_PASTE_SEARCH_PROVIDER (object);

    if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->skeleton),
                                           connection,
                                           G_PASTE_SEARCH_PROVIDER_OBJECT_PATH,
                                           error))
    {
        return FALSE;
    }

    return (self->registered = TRUE);
}

static void
g_paste_search_provider_class_init (GPasteSearchProviderClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_search_provider_dispose;
    G_PASTE_BUS_OBJECT_CLASS (klass)->register_on_connection = g_paste_search_provider_register_on_connection;
    G_PASTE_BUS_OBJECT_CLASS (klass)->unregister_on_connection = g_paste_search_provider_unregister_on_connection;
}

/* Swapped, so @self leads: the payload of the "update" signal says nothing this
 * needs -- any change at all is a reason to stop answering from the values the
 * last search handed us. */
static void
g_paste_search_provider_forget_results (GPasteSearchProvider *self)
{
    g_hash_table_remove_all (self->last_results);
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

    /* Anything at all changing in the history is enough to drop the kept
     * values: they are a snapshot of one reply, and an item edited, renamed or
     * turned into a password since would otherwise be described with the string
     * it used to have. Dropping them costs one GetItems on the next
     * GetResultMetas, which is what the path did every time before. */
    if (self->client)
        g_signal_connect_object (self->client, "update", G_CALLBACK (g_paste_search_provider_forget_results), self, G_CONNECT_SWAPPED);

    /* Never abort here: this provider is also built inside gnome-shell, where
     * g_error() would take the whole compositor down over a transient D-Bus
     * failure. Without a client every search simply answers empty. */
    if (error)
        g_warning ("Failed to connect to GPaste daemon, searching is disabled: %s", error->message);
}

static void
g_paste_search_provider_init (GPasteSearchProvider *self)
{
    self->skeleton = G_PASTE_SHELL_SEARCH_PROVIDER2 (g_paste_shell_search_provider2_skeleton_new ());
    self->last_results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

    /* Swapped, so @self leads each handler. */
    g_signal_connect_swapped (self->skeleton, "handle-get-initial-result-set",
                              G_CALLBACK (g_paste_search_provider_handle_get_initial_result_set), self);
    g_signal_connect_swapped (self->skeleton, "handle-get-subsearch-result-set",
                              G_CALLBACK (g_paste_search_provider_handle_get_subsearch_result_set), self);
    g_signal_connect_swapped (self->skeleton, "handle-get-result-metas",
                              G_CALLBACK (g_paste_search_provider_handle_get_result_metas), self);
    g_signal_connect_swapped (self->skeleton, "handle-activate-result",
                              G_CALLBACK (g_paste_search_provider_handle_activate_result), self);
    g_signal_connect_swapped (self->skeleton, "handle-launch-search",
                              G_CALLBACK (g_paste_search_provider_handle_launch_search), self);

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
