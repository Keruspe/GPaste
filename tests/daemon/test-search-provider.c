// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-test-bus.h>
#include <gpaste-3/gpaste-daemon3.h>
#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gdbus-defines.h>
#include <gpaste-3/gpaste-update-enums.h>
#include <gpaste-3/gpaste-util.h>
#include <gpaste-daemon/gpaste-search-provider.h>

#define SEARCH_PROVIDER_INTERFACE "org.gnome.Shell.SearchProvider2"

#define REMOVED_UUID   "00000000-0000-4000-8000-000000000001"
#define SURVIVING_UUID "00000000-0000-4000-8000-000000000002"

typedef struct
{
    GDBusConnection *server;
    GDBusConnection *caller;
    GPasteDaemon3   *daemon;
    GPasteBusObject *provider;

    guint            searches;
    gchar           *query;
    guint            item_calls;
    guint            history_calls;
} Fixture;

static gboolean
on_search (GPasteDaemon3         *daemon,
           GDBusMethodInvocation *invocation,
           const gchar           *query,
           gpointer               user_data)
{
    Fixture *f = user_data;

    ++f->searches;
    g_set_str (&f->query, query);
    g_paste_daemon3_complete_search (daemon, invocation,
                                     g_variant_new_parsed ("[('" REMOVED_UUID "', 'removed text', uint32 1, false, @as []),"
                                                           " ('" SURVIVING_UUID "', 'surviving text', uint32 1, false, @as [])]"));
    return TRUE;
}

/* What the daemon answers once an item the uuids name has gone. */
static gboolean
on_get_items (GPasteDaemon3         *daemon G_GNUC_UNUSED,
              GDBusMethodInvocation *invocation,
              const gchar * const   *uuids G_GNUC_UNUSED,
              gpointer               user_data)
{
    Fixture *f = user_data;

    ++f->item_calls;
    g_dbus_method_invocation_return_error_literal (invocation, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND,
                                                   "Provided uuid doesn't match any item.");
    return TRUE;
}

static gboolean
on_get_history (GPasteDaemon3         *daemon,
                GDBusMethodInvocation *invocation,
                gpointer               user_data)
{
    Fixture *f = user_data;

    ++f->history_calls;
    g_paste_daemon3_complete_get_history (daemon, invocation,
                                          g_variant_new_parsed ("[('" SURVIVING_UUID "', 'surviving text', uint32 1, false, @as [])]"));
    return TRUE;
}

static void
fixture_setup (Fixture      *f,
               gconstpointer data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;

    f->server = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
    f->caller = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
    f->daemon = g_paste_daemon3_skeleton_new ();
    g_signal_connect (f->daemon, "handle-search", G_CALLBACK (on_search), f);
    g_signal_connect (f->daemon, "handle-get-items", G_CALLBACK (on_get_items), f);
    g_signal_connect (f->daemon, "handle-get-history", G_CALLBACK (on_get_history), f);
    g_assert_true (g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (f->daemon), f->server,
                                                     G_PASTE_DAEMON_OBJECT_PATH, &error));
    g_assert_no_error (error);
    g_paste_daemon3_set_active (f->daemon, TRUE);
    g_paste_daemon3_set_history (f->daemon, G_PASTE_DEFAULT_HISTORY);
    g_paste_daemon3_set_version (f->daemon, PACKAGE_VERSION);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->server, "RequestName", g_variant_new ("(su)", G_PASTE_BUS_NAME, 0u)), ==, 1);

    f->provider = g_paste_search_provider_new ();
    g_assert_true (g_paste_bus_object_register_on_connection (f->provider, f->server, &error));
    g_assert_no_error (error);
}

static void
fixture_teardown (Fixture      *f,
                  gconstpointer data G_GNUC_UNUSED)
{
    g_paste_bus_object_unregister_on_connection (f->provider);
    g_clear_object (&f->provider);
    g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (f->daemon));
    g_clear_object (&f->daemon);
    g_paste_test_bus_name_call (f->server, "ReleaseName", g_variant_new ("(s)", G_PASTE_BUS_NAME));
    g_clear_object (&f->caller);
    g_clear_object (&f->server);
    g_clear_pointer (&f->query, g_free);
}

static void
on_reply (GObject      *source,
          GAsyncResult *result,
          gpointer      user_data)
{
    GVariant **reply = user_data;
    g_autoptr (GError) error = NULL;

    *reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
    g_assert_no_error (error);
}

/* Asynchronous: the provider answers from this process's main context. */
static GVariant *
call_provider (Fixture     *f,
               const gchar *method,
               GVariant    *parameters,
               const gchar *reply_type)
{
    GVariant *reply = NULL;

    g_dbus_connection_call (f->caller, g_dbus_connection_get_unique_name (f->server), G_PASTE_SEARCH_PROVIDER_OBJECT_PATH,
                            SEARCH_PROVIDER_INTERFACE, method, parameters, G_VARIANT_TYPE (reply_type),
                            G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_reply, &reply);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &reply, NULL);
    g_assert_nonnull (reply);

    return reply;
}

/* The provider answers every search empty, asking the daemon nothing, until
 * its client is up: search until one reaches the daemon. */
static void
search_terms (Fixture             *f,
              const gchar * const *terms)
{
    static const gchar * const found[] = { REMOVED_UUID, SURVIVING_UUID, NULL };
    gint64 deadline = g_get_monotonic_time () + G_PASTE_TEST_BUS_WAIT_SECONDS * G_USEC_PER_SEC;

    while (!f->searches && g_get_monotonic_time () < deadline)
    {
        g_autoptr (GVariant) reply = call_provider (f, "GetInitialResultSet", g_variant_new ("(^as)", terms), "(as)");
        g_autofree const gchar **ids = NULL;

        g_variant_get (reply, "(^a&s)", &ids);
        if (f->searches)
            g_assert_cmpstrv (ids, found);
        else
        {
            g_assert_null (ids[0]);
            g_paste_test_bus_pump (10);
        }
    }
    g_assert_cmpuint (f->searches, ==, 1);
}

static void
search (Fixture *f)
{
    static const gchar * const terms[] = { "text", NULL };

    search_terms (f, terms);
}

/* What the overview's user typed is searched for as text: Search takes a
 * regular expression, which would not compile it, or would match what was not
 * typed. */
static void
test_escaped (Fixture      *f,
              gconstpointer user_data G_GNUC_UNUSED)
{
    static const gchar * const terms[] = { "c++", "(gcc", NULL };

    search_terms (f, terms);
    g_assert_cmpstr (f->query, ==, "c\\+\\+ \\(gcc");
}

/* Every signal the daemon sent is ahead of the reply to the round trip, so once
 * it is back the provider's client, on the shared session connection, has
 * heard the update. */
static void
update (Fixture           *f,
        GPasteUpdateAction action,
        GPasteUpdateTarget target,
        const gchar       *uuid)
{
    g_autoptr (GDBusConnection) session = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

    g_paste_daemon3_emit_raw_update (f->daemon, action, target, uuid, 0);
    g_paste_test_bus_round_trip (session, f->server);
}

/* The metas of both results, asked for in the order the search gave them. */
static GVariant *
result_metas (Fixture *f)
{
    static const gchar * const ids[] = { REMOVED_UUID, SURVIVING_UUID, NULL };
    g_autoptr (GVariant) reply = call_provider (f, "GetResultMetas", g_variant_new ("(^as)", ids), "(aa{sv})");

    return g_variant_get_child_value (reply, 0);
}

static void
assert_meta (GVariant    *metas,
             gsize        position,
             const gchar *uuid,
             const gchar *text)
{
    g_autoptr (GVariant) meta = g_variant_get_child_value (metas, position);
    const gchar *value;

    g_assert_true (g_variant_lookup (meta, "id", "&s", &value));
    g_assert_cmpstr (value, ==, uuid);
    g_assert_true (g_variant_lookup (meta, "name", "&s", &value));
    g_assert_cmpstr (value, ==, text);
    /* Nothing for the shell to put on the clipboard ahead of Select. */
    g_assert_false (g_variant_lookup (meta, "clipboardText", "&s", &value));
}

/* A removal leaves the results as the search found them, the one removed
 * included: one meta per identifier, in order, without asking the daemon. */
static void
test_removed (Fixture      *f,
              gconstpointer data G_GNUC_UNUSED)
{
    search (f);
    update (f, G_PASTE_UPDATE_ACTION_REMOVE, G_PASTE_UPDATE_TARGET_ITEM, REMOVED_UUID);

    g_autoptr (GVariant) metas = result_metas (f);

    g_assert_cmpuint (f->item_calls, ==, 0);
    g_assert_cmpuint (f->history_calls, ==, 0);
    g_assert_cmpuint (g_variant_n_children (metas), ==, 2);
    assert_meta (metas, 0, REMOVED_UUID, "removed text");
    assert_meta (metas, 1, SURVIVING_UUID, "surviving text");
}

/* Any other update drops them, so the identifiers go to GetItems, which the one
 * gone fails: answered empty, without a warning and without the history. */
static void
test_replaced (Fixture      *f,
               gconstpointer data G_GNUC_UNUSED)
{
    search (f);
    update (f, G_PASTE_UPDATE_ACTION_REPLACE, G_PASTE_UPDATE_TARGET_ALL, "");

    g_autoptr (GVariant) metas = result_metas (f);

    g_assert_cmpuint (f->item_calls, ==, 1);
    g_assert_cmpuint (f->history_calls, ==, 0);
    g_assert_cmpuint (g_variant_n_children (metas), ==, 0);
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add ("/search-provider/search/escaped", Fixture, NULL, fixture_setup, test_escaped, fixture_teardown);
    g_test_add ("/search-provider/metas/removed", Fixture, NULL, fixture_setup, test_removed, fixture_teardown);
    g_test_add ("/search-provider/metas/replaced", Fixture, NULL, fixture_setup, test_replaced, fixture_teardown);
    return g_paste_test_env_run ();
}
