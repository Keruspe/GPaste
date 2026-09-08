// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste-daemon/gpaste-keybinder.h>
#include <gpaste-daemon/gpaste-global-shortcut-client.h>

#define PORTAL_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define PORTAL_IFACE "org.freedesktop.portal.GlobalShortcuts"
#define SESSION_IFACE "org.freedesktop.portal.Session"

typedef struct
{
    GDBusConnection *connection;
    GDBusMethodInvocation *invocation;
    gchar *sender;
    gchar *path;
    gchar *session;
    GVariant *shortcuts;
    guint registration;
    guint closes;
    guint close_failures;
    gboolean responded;
} Request;

typedef struct
{
    GTestDBus *bus;
    GDBusConnection *server;
    GDBusConnection *replacement;
    GDBusConnection *connection;
    GPasteGlobalShortcutClient *client;
    GDBusNodeInfo *info;
    GPtrArray *creates;
    GPtrArray *binds;
    guint registration;
    guint replacement_registration;
    guint activations;
    guint watchdog;
    gboolean ready;
    gboolean automatic;
} Fixture;

static const gchar portal_xml[] =
    "<node><interface name='" PORTAL_IFACE "'>"
    "<method name='CreateSession'><arg type='a{sv}' direction='in'/>"
    "<arg type='o' direction='out'/></method>"
    "<method name='BindShortcuts'><arg type='o' direction='in'/>"
    "<arg type='a(sa{sv})' direction='in'/><arg type='s' direction='in'/>"
    "<arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
    "<method name='Barrier'/><method name='GetCounts'>"
    "<arg type='u' direction='out'/><arg type='u' direction='out'/>"
    "<arg type='u' direction='out'/></method></interface>"
    "<interface name='" SESSION_IFACE "'><method name='Close'/></interface></node>";

static void method_reply (Request *request);
static void respond (Request *request, guint response);

static const GPasteKeybindingAccelerator shortcuts[] = {
    { "first", "<Control>a", "First" }, { NULL, NULL, NULL }
};
static const GPasteKeybindingAccelerator changed[] = {
    { "first", "<Control>b", "First" }, { NULL, NULL, NULL }
};

static void
request_free (gpointer data)
{
    g_autofree Request *request = data;
    if (request->registration)
        g_dbus_connection_unregister_object (request->connection, request->registration);
    g_clear_object (&request->invocation);
    g_clear_object (&request->connection);
    g_clear_pointer (&request->shortcuts, g_variant_unref);
    g_free (request->sender);
    g_free (request->path);
    g_free (request->session);
}

static void
on_close (GDBusConnection *connection G_GNUC_UNUSED,
          const gchar *sender G_GNUC_UNUSED,
          const gchar *path G_GNUC_UNUSED,
          const gchar *interface G_GNUC_UNUSED,
          const gchar *method,
          GVariant *parameters G_GNUC_UNUSED,
          GDBusMethodInvocation *invocation,
          gpointer user_data)
{
    Request *request = user_data;
    g_assert_cmpstr (method, ==, "Close");
    request->closes++;
    if (request->close_failures)
    {
        --request->close_failures;
        g_dbus_method_invocation_return_dbus_error (invocation,
            "org.freedesktop.DBus.Error.Failed", "Temporary close failure");
        return;
    }
    g_dbus_method_invocation_return_value (invocation, NULL);
}

static void
on_method (GDBusConnection *connection,
           const gchar *sender,
           const gchar *path G_GNUC_UNUSED,
           const gchar *interface G_GNUC_UNUSED,
           const gchar *method,
           GVariant *parameters,
           GDBusMethodInvocation *invocation,
           gpointer user_data)
{
    Fixture *f = user_data;
    if (g_str_equal (method, "Barrier"))
    {
        g_dbus_method_invocation_return_value (invocation, NULL);
        return;
    }

    if (g_str_equal (method, "GetCounts"))
    {
        guint closes = 0;
        for (guint i = 0; i < f->creates->len; ++i)
            closes += ((Request *) g_ptr_array_index (f->creates, i))->closes;
        g_dbus_method_invocation_return_value (invocation,
            g_variant_new ("(uuu)", f->creates->len, f->binds->len, closes));
        return;
    }

    gboolean create = g_str_equal (method, "CreateSession");
    if (!create)
        g_assert_cmpstr (method, ==, "BindShortcuts");
    g_autoptr (GVariant) options = g_variant_get_child_value (parameters, create ? 0 : 3);
    const gchar *token;
    g_assert_true (g_variant_lookup (options, "handle_token", "&s", &token));
    g_autofree gchar *sender_path = g_strdelimit (g_strdup (sender + 1), ".", '_');
    g_autofree Request *request = g_new0 (Request, 1);
    request->connection = g_object_ref (connection);
    request->invocation = g_object_ref (invocation);
    request->sender = g_strdup (sender);
    request->path = g_strdup_printf (PORTAL_PATH "/request/%s/%s", sender_path, token);
    if (create)
    {
        static const GDBusInterfaceVTable vtable = { .method_call = on_close };
        g_autoptr (GError) error = NULL;
        g_assert_true (g_variant_lookup (options, "session_handle_token", "&s", &token));
        request->session = g_strdup_printf (PORTAL_PATH "/session/%s/%s", sender_path, token);
        request->registration = g_dbus_connection_register_object (connection, request->session,
            f->info->interfaces[1], &vtable, request, NULL, &error);
        g_assert_no_error (error);
        g_ptr_array_add (f->creates, g_steal_pointer (&request));
    }
    else
    {
        g_variant_get_child (parameters, 0, "o", &request->session);
        request->shortcuts = g_variant_get_child_value (parameters, 1);
        g_ptr_array_add (f->binds, g_steal_pointer (&request));
    }
    if (f->automatic)
    {
        GPtrArray *requests = create ? f->creates : f->binds;
        Request *pending = g_ptr_array_index (requests, requests->len - 1);
        respond (pending, 0);
        method_reply (pending);
        if (!create)
        {
            g_autoptr (GError) error = NULL;
            g_dbus_connection_emit_signal (connection, sender, PORTAL_PATH, PORTAL_IFACE,
                "Activated", g_variant_new ("(osta{sv})", pending->session, "first", (guint64) 1, NULL), &error);
            g_assert_no_error (error);
        }
    }

}

/* Whichever portal is up: a replacement takes the name over from the first. */
static void
barrier (Fixture *f)
{
    g_paste_test_bus_barrier (f->connection, f->replacement ? f->replacement : f->server,
                              PORTAL_PATH, PORTAL_IFACE);
}

static void
method_reply (Request *request)
{
    g_assert_nonnull (request->invocation);
    g_dbus_method_invocation_return_value (request->invocation, g_variant_new ("(o)", request->path));
    g_clear_object (&request->invocation);
}

static void
respond (Request *request, guint response)
{
    g_auto (GVariantBuilder) results;
    g_variant_builder_init (&results, G_VARIANT_TYPE_VARDICT);
    if (request->registration && !response)
        g_variant_builder_add (&results, "{sv}", "session_handle", g_variant_new_string (request->session));
    g_autoptr (GError) error = NULL;
    g_assert_false (request->responded);
    g_dbus_connection_emit_signal (request->connection, request->sender, request->path,
        "org.freedesktop.portal.Request", "Response", g_variant_new ("(ua{sv})", response, &results), &error);
    g_assert_no_error (error);
    request->responded = TRUE;
}

static void
complete (Fixture *f, Request *request, gboolean response_first, guint response)
{
    if (response_first)
    {
        respond (request, response);
        barrier (f);
        method_reply (request);
    }
    else
    {
        method_reply (request);
        barrier (f);
        respond (request, response);
    }
    barrier (f);
}

static void
on_client_ready (GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
    Fixture *f = user_data;
    g_autoptr (GError) error = NULL;
    f->client = g_paste_global_shortcut_client_new_finish (result, &error);
    g_assert_no_error (error);
    f->ready = TRUE;
}

static void
on_activated (GPasteGlobalShortcutClient *client G_GNUC_UNUSED, const gchar *id, gpointer user_data)
{
    Fixture *f = user_data;
    g_assert_cmpstr (id, ==, "first");
    f->activations++;
}

static GDBusConnection *
new_server (Fixture *f, guint *registration)
{
    static const GDBusInterfaceVTable vtable = { .method_call = on_method };

    return g_paste_test_bus_new_server (f->bus, PORTAL_PATH, f->info->interfaces[0], &vtable, f, registration);
}

/* Bound every main-context wait, including setup and teardown, and identify
 * the case that stalled instead of leaving Meson to kill the whole suite. */
static gboolean
test_timed_out (gpointer user_data G_GNUC_UNUSED)
{
    g_error ("Timed out waiting for portal test %s", g_test_get_path ());
    return G_SOURCE_REMOVE;
}

static void
setup (Fixture *f, gconstpointer user_data)
{
    g_autoptr (GError) error = NULL;
    f->watchdog = g_timeout_add_seconds (10, test_timed_out, NULL);
    f->bus = g_test_dbus_new (G_TEST_DBUS_NONE);
    gboolean activate = GPOINTER_TO_UINT (user_data) == 100;
    if (activate)
        g_test_dbus_add_service_dir (f->bus, g_test_get_dir (G_TEST_BUILT));
    g_test_dbus_up (f->bus);
    f->info = g_dbus_node_info_new_for_xml (portal_xml, &error);
    g_assert_no_error (error);
    f->creates = g_ptr_array_new_with_free_func (request_free);
    f->binds = g_ptr_array_new_with_free_func (request_free);
    f->server = new_server (f, &f->registration);
    /* ALLOW_REPLACEMENT lets tests exercise a direct owner handoff. */
    if (!activate)
    {
        g_assert_cmpuint (g_paste_test_bus_name_call (f->server, "RequestName",
                                                      g_variant_new ("(su)", PORTAL_NAME, 1u)), ==, 1);
    }
    f->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    g_assert_no_error (error);
    if (activate)
    {
        /* Leave activation to CreateSession, so its request starts with no
         * unique owner. The public constructor otherwise activates the proxy. */
        g_async_initable_new_async (G_PASTE_TYPE_GLOBAL_SHORTCUT_CLIENT, G_PRIORITY_DEFAULT,
            NULL, on_client_ready, f, "g-connection", f->connection,
            "g-flags", G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START_AT_CONSTRUCTION,
            "g-name", PORTAL_NAME, "g-object-path", PORTAL_PATH,
            "g-interface-name", PORTAL_IFACE, NULL);
    }
    else
        g_paste_global_shortcut_client_new (on_client_ready, f);
    while (!f->ready)
        g_main_context_iteration (NULL, TRUE);
    g_signal_connect (f->client, "keybinding-activated", G_CALLBACK (on_activated), f);
    barrier (f);
}

static void
teardown (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    if (f->client)
        g_paste_global_shortcut_client_ungrab_all (f->client);
    /* Deliver anything deliberately left pending. Superseded creates must be
     * closed and no request may keep the client alive after teardown. */
    GPtrArray *requests[] = { f->creates, f->binds };
    for (guint i = 0; i < G_N_ELEMENTS (requests); i++)
    {
        for (guint j = 0; j < requests[i]->len; j++)
        {
            Request *request = g_ptr_array_index (requests[i], j);
            if (!request->responded)
                respond (request, 0);
            if (request->invocation)
                method_reply (request);
        }
    }
    barrier (f);
    /* Unless the test dropped it itself, to see what an outstanding request
     * holds on to. */
    if (f->client)
    {
        GPasteGlobalShortcutClient *weak = f->client;
        g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
        g_clear_object (&f->client);
        barrier (f);
        g_assert_null (weak);
    }
    g_clear_pointer (&f->binds, g_ptr_array_unref);
    g_clear_pointer (&f->creates, g_ptr_array_unref);
    g_dbus_connection_unregister_object (f->server, f->registration);
    if (f->replacement)
        g_dbus_connection_unregister_object (f->replacement, f->replacement_registration);
    g_clear_object (&f->replacement);
    g_clear_object (&f->server);
    g_clear_object (&f->connection);
    g_clear_pointer (&f->info, g_dbus_node_info_unref);
    g_test_dbus_down (f->bus);
    g_clear_object (&f->bus);
    g_clear_handle_id (&f->watchdog, g_source_remove);
}

static Request *
grab (Fixture *f, const GPasteKeybindingAccelerator *accels)
{
    guint count = f->creates->len;
    g_paste_global_shortcut_client_grab_all (f->client, accels);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, count + 1);
    return g_ptr_array_index (f->creates, count);
}

static Request *
bind_session (Fixture *f, Request *create, gboolean response_first)
{
    guint count = f->binds->len;
    complete (f, create, response_first, 0);
    g_assert_cmpuint (f->binds->len, ==, count + 1);
    Request *bind = g_ptr_array_index (f->binds, count);
    g_assert_cmpstr (bind->session, ==, create->session);
    return bind;
}

static void
activate (Fixture *f, Request *session)
{
    g_autoptr (GError) error = NULL;
    g_dbus_connection_emit_signal (session->connection, session->sender, PORTAL_PATH,
        PORTAL_IFACE, "Activated", g_variant_new ("(osta{sv})", session->session, "first", (guint64) 1, NULL), &error);
    g_assert_no_error (error);
    barrier (f);
}

static void
reply_order (Fixture *f, gconstpointer user_data)
{
    gboolean response_first = GPOINTER_TO_INT (user_data);
    Request *session = grab (f, shortcuts);
    Request *bind = bind_session (f, session, response_first);
    complete (f, bind, response_first, 0);
    activate (f, session);
    g_assert_cmpuint (f->activations, ==, 1);
    g_paste_global_shortcut_client_grab_all (f->client, shortcuts);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, 1);
    g_assert_cmpuint (session->closes, ==, 0);
}

static void
superseded_create (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *old = grab (f, shortcuts);
    method_reply (old);
    barrier (f);
    Request *current = grab (f, changed);
    Request *bind = bind_session (f, current, FALSE);
    complete (f, bind, FALSE, 0);
    respond (old, 0);
    barrier (f);
    g_assert_cmpuint (old->closes, ==, 1);
    g_assert_cmpuint (current->closes, ==, 0);
    g_assert_cmpuint (f->binds->len, ==, 1);
    activate (f, old);
    activate (f, current);
    g_assert_cmpuint (f->activations, ==, 1);
}

static void
disable_pending_bind (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *session = grab (f, shortcuts);
    Request *bind = bind_session (f, session, TRUE);
    method_reply (bind);
    barrier (f);
    g_paste_global_shortcut_client_ungrab_all (f->client);
    barrier (f);
    g_assert_cmpuint (session->closes, ==, 1);
    respond (bind, 0);
    barrier (f);
    activate (f, session);
    g_assert_cmpuint (f->activations, ==, 0);
    g_assert_cmpuint (f->creates->len, ==, 1);
}

static void
denied_bind (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *session = grab (f, shortcuts);
    Request *bind = bind_session (f, session, FALSE);
    complete (f, bind, TRUE, 1);
    g_assert_cmpuint (session->closes, ==, 1);
    Request *retry = grab (f, shortcuts);
    g_assert_cmpstr (session->session, !=, retry->session);
    complete (f, bind_session (f, retry, FALSE), FALSE, 0);
}

/* A failure nobody said no to is asked about again on its own: a portal that
 * stays up sends no owner change to recover on, and a user who never writes
 * another accelerator never asks either -- the set would stay bound to nothing
 * for the rest of the session over a backend that was merely slow to start. */
static void
failed_bind_retry (Fixture *f, gconstpointer user_data)
{
    gboolean deny_create = GPOINTER_TO_UINT (user_data);
    Request *session = grab (f, shortcuts);

    if (deny_create)
        complete (f, session, FALSE, 2);
    else
    {
        complete (f, bind_session (f, session, FALSE), FALSE, 2);
        g_assert_cmpuint (session->closes, ==, 1);
    }
    g_assert_cmpuint (f->creates->len, ==, 1);

    /* Nothing but the retry itself ends this wait. */
    while (f->creates->len < 2)
        g_main_context_iteration (NULL, TRUE);
    barrier (f);

    Request *current = g_ptr_array_index (f->creates, 1);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    activate (f, current);
    g_assert_cmpuint (f->activations, ==, 1);
    g_assert_cmpuint (current->closes, ==, 0);
}

static void
failed_create_late_response (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *old = grab (f, shortcuts);
    g_dbus_method_invocation_return_dbus_error (old->invocation,
        "org.freedesktop.DBus.Error.Failed", "The method failed after accepting the request");
    g_clear_object (&old->invocation);
    barrier (f);
    Request *current = grab (f, shortcuts);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    respond (old, 0);
    barrier (f);
    g_assert_cmpuint (old->closes, ==, 1);
    g_assert_cmpuint (current->closes, ==, 0);
    g_assert_cmpuint (f->binds->len, ==, 1);
}

static void
restart (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    Request *old = grab (f, shortcuts);
    if (mode != 2)
        complete (f, bind_session (f, old, FALSE), FALSE, 0);
    else
    {
        method_reply (old);
        barrier (f);
    }
    if (mode == 0)
    {
        g_assert_cmpuint (g_paste_test_bus_name_call (f->server, "ReleaseName",
                                                      g_variant_new ("(s)", PORTAL_NAME)), ==, 1);
        barrier (f);
    }
    f->replacement = new_server (f, &f->replacement_registration);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->replacement, "RequestName",
                                                  g_variant_new ("(su)", PORTAL_NAME, 2u)), ==, 1);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, 2);
    if (mode != 2)
        g_assert_cmpuint (old->closes, ==, 1);
    Request *current = g_ptr_array_index (f->creates, 1);
    complete (f, bind_session (f, current, TRUE), TRUE, 0);
    activate (f, current);
    g_assert_cmpuint (f->activations, ==, 1);
    g_assert_cmpuint (current->closes, ==, 0);
}

static void
emit_closed (Fixture *f, Request *session)
{
    g_autoptr (GError) error = NULL;
    g_dbus_connection_emit_signal (session->connection, session->sender, session->session,
        SESSION_IFACE, "Closed", g_variant_new ("(a{sv})", NULL), &error);
    g_assert_no_error (error);
    barrier (f);
}

static void
session_closed (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    Request *session = grab (f, shortcuts);
    Request *bind = bind_session (f, session, FALSE);
    if (mode == 0)
        complete (f, bind, FALSE, 0);
    else if (mode == 1)
    {
        method_reply (bind);
        barrier (f);
    }

    emit_closed (f, session);
    activate (f, session);
    g_assert_cmpuint (f->activations, ==, 0);
    /* A remote close must not reopen a permission prompt on its own. */
    g_assert_cmpuint (f->creates->len, ==, 1);
    g_assert_cmpuint (session->closes, ==, 0);

    Request *current = grab (f, shortcuts);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    if (mode != 0)
    {
        respond (bind, 0);
        if (bind->invocation)
            method_reply (bind);
        barrier (f);
    }
    /* A late signal or bind reply for the old session cannot clear the new
     * one. */
    emit_closed (f, session);
    activate (f, current);
    g_assert_cmpuint (f->activations, ==, 1);
    g_assert_cmpuint (current->closes, ==, 0);
}

/* A session the backend closed under us is the user's decision. A portal
 * restart is not them changing their mind, and must not put the permission
 * dialog back in front of them over the grant they have just taken away. */
static void
session_closed_restart (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *session = grab (f, shortcuts);
    complete (f, bind_session (f, session, FALSE), FALSE, 0);
    emit_closed (f, session);
    g_assert_cmpuint (f->creates->len, ==, 1);

    f->replacement = new_server (f, &f->replacement_registration);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->replacement, "RequestName",
                                                  g_variant_new ("(su)", PORTAL_NAME, 2u)), ==, 1);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, 1);

    /* A write to an accelerator is the user asking again, and is answered. */
    Request *current = grab (f, changed);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    activate (f, current);
    g_assert_cmpuint (f->activations, ==, 1);
}

/* A refusal is the user's answer as much as a session closed under us is: a
 * portal restart must not put the dialog they have just dismissed back in front
 * of them, whichever of the two calls they refused. */
static void
denied_restart (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    gboolean deny_create = mode & 1;
    guint response = mode & 2 ? 2 : 1;
    Request *session = grab (f, shortcuts);

    if (deny_create)
    {
        complete (f, session, FALSE, response);
        g_assert_cmpuint (f->binds->len, ==, 0);
    }
    else
    {
        complete (f, bind_session (f, session, FALSE), TRUE, response);
        g_assert_cmpuint (session->closes, ==, 1);
    }

    f->replacement = new_server (f, &f->replacement_registration);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->replacement, "RequestName",
                                                  g_variant_new ("(su)", PORTAL_NAME, 2u)), ==, 1);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, response == 1 ? 1 : 2);

    /* A cancellation waits for a user edit; a general failure recovers on
     * restart without needing a settings write. */
    Request *current = response == 1 ? grab (f, changed) : g_ptr_array_index (f->creates, 1);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    activate (f, current);
    g_assert_cmpuint (f->activations, ==, 1);
}

/* A retired CreateSession keeps listening on its unique owner across a
 * well-known name handoff, so its late session is still closed. */
static void
retired_create_handoff (Fixture *f, gconstpointer user_data)
{
    Request *old = grab (f, shortcuts);
    if (!GPOINTER_TO_UINT (user_data))
    {
        method_reply (old);
        barrier (f);
    }

    g_paste_global_shortcut_client_ungrab_all (f->client);
    barrier (f);

    f->replacement = new_server (f, &f->replacement_registration);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->replacement, "RequestName",
                                                  g_variant_new ("(su)", PORTAL_NAME, 2u)), ==, 1);
    barrier (f);

    /* The old portal lost only its well-known name. It still answers on its
     * unique connection, so the late session must be closed there. */
    respond (old, 0);
    barrier (f);
    g_assert_cmpuint (old->closes, ==, 1);
    g_assert_cmpuint (f->creates->len, ==, 1);

    /* Cleanup also must not retain the client. */
    GPasteGlobalShortcutClient *weak = f->client;
    g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
    g_clear_object (&f->client);
    barrier (f);
    g_assert_null (weak);
}

/* A request in flight must not be what keeps the client alive: dropping the
 * client has to reach dispose (), which is the one thing that gives up the
 * session and the grabs that go with it. */
static void
drop_with_pending_create (Fixture *f, gconstpointer user_data)
{
    Request *session = grab (f, shortcuts);
    if (!GPOINTER_TO_UINT (user_data))
    {
        method_reply (session);
        barrier (f);
    }

    GPasteGlobalShortcutClient *weak = f->client;
    g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
    g_clear_object (&f->client);
    barrier (f);
    g_assert_null (weak);

    /* The session the portal answers with is nobody's, and that Response is the
     * only thing that ever carries the handle it has to be closed by. */
    respond (session, 0);
    barrier (f);
    g_assert_cmpuint (session->closes, ==, 1);
    g_assert_cmpuint (f->binds->len, ==, 0);
    if (session->invocation)
    {
        method_reply (session);
        barrier (f);
    }
}

static void
drop_with_pending_bind (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    Request *session = grab (f, shortcuts);
    Request *bind;
    if (mode == 2)
    {
        respond (session, 0);
        barrier (f);
        g_assert_cmpuint (f->binds->len, ==, 1);
        bind = g_ptr_array_index (f->binds, 0);
    }
    else
        bind = bind_session (f, session, FALSE);

    if (mode == 1)
        method_reply (bind);
    else if (mode == 3)
        respond (bind, 0);
    barrier (f);

    GPasteGlobalShortcutClient *weak = f->client;
    g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
    g_clear_object (&f->client);
    barrier (f);
    g_assert_null (weak);
    g_assert_cmpuint (session->closes, ==, 1);

    if (!bind->responded)
        respond (bind, 0);
    if (bind->invocation)
        method_reply (bind);
    if (session->invocation)
        method_reply (session);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, 1);
    g_assert_cmpuint (session->closes, ==, 1);
}

/* A Response the portal spells wrong is not the user saying no, and reading it
 * is not a critical -- which these tests make fatal. It is a failure like any
 * other, so a portal restart still recovers. */
static void
malformed_response (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *session = grab (f, shortcuts);
    Request *bind = bind_session (f, session, FALSE);
    method_reply (bind);
    barrier (f);

    g_autoptr (GError) error = NULL;
    g_dbus_connection_emit_signal (bind->connection, bind->sender, bind->path,
        "org.freedesktop.portal.Request", "Response", g_variant_new ("(u)", 0u), &error);
    g_assert_no_error (error);
    bind->responded = TRUE;
    barrier (f);

    /* The bind failed, so the session is spent -- and nothing was revoked, so
     * the replacement is asked for one of its own. */
    g_assert_cmpuint (session->closes, ==, 1);
    f->replacement = new_server (f, &f->replacement_registration);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->replacement, "RequestName",
                                                  g_variant_new ("(su)", PORTAL_NAME, 2u)), ==, 1);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, 2);

    Request *current = g_ptr_array_index (f->creates, 1);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    activate (f, current);
    g_assert_cmpuint (f->activations, ==, 1);
}

static void
trigger_format (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    const GPasteKeybindingAccelerator accels[] = {
        { "super", "<Super>a", "Super" },
        { "return", "<Control>Return", "Return" },
        { "mixed", "<Control><Alt><Shift><Super>F5", "Mixed" },
        /* No name for it in the trigger syntax: asking for a chord the portal
         * can spell would be asking for another shortcut altogether. */
        { "meta", "<Meta>a", "Meta" },
        { NULL, NULL, NULL }
    };
    const gchar *expected[] = { "LOGO+a", "CTRL+Return", "CTRL+ALT+SHIFT+LOGO+F5", NULL };
    Request *session = grab (f, accels);
    Request *bind = bind_session (f, session, FALSE);
    g_assert_cmpuint (g_variant_n_children (bind->shortcuts), ==, G_N_ELEMENTS (expected));
    for (guint i = 0; i < G_N_ELEMENTS (expected); i++)
    {
        g_autoptr (GVariant) shortcut = g_variant_get_child_value (bind->shortcuts, i);
        g_autoptr (GVariant) details = g_variant_get_child_value (shortcut, 1);
        const gchar *trigger;
        gboolean found = g_variant_lookup (details, "preferred_trigger", "&s", &trigger);
        g_assert_true (found == (expected[i] != NULL));
        if (expected[i])
            g_assert_cmpstr (trigger, ==, expected[i]);
    }
    complete (f, bind, FALSE, 0);
}


/* These waits exercise timers, not D-Bus ordering; barriers do the latter.
 * The test target builds the client with a 50 ms retry and 1 s retirement. */
static gboolean
wait_elapsed (gpointer user_data)
{
    *(gboolean *) user_data = TRUE;
    return G_SOURCE_REMOVE;
}

static void
wait_for_timers (Fixture *f, guint milliseconds)
{
    gboolean elapsed = FALSE;
    g_timeout_add (milliseconds, wait_elapsed, &elapsed);
    while (!elapsed)
        g_main_context_iteration (NULL, TRUE);
    barrier (f);
}

static void
method_error (Request *request)
{
    g_dbus_method_invocation_return_dbus_error (request->invocation,
        "org.freedesktop.DBus.Error.Failed", "Temporary method failure");
    g_clear_object (&request->invocation);
}

static void
retry_exhaustion (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    Request *old = NULL;
    if (mode)
    {
        old = grab (f, shortcuts);
        if (mode == 1)
        {
            method_reply (old);
            barrier (f);
        }
    }
    Request *current = grab (f, changed);
    for (guint i = 0; i <= 10; ++i)
    {
        guint count = f->creates->len;
        if (i == 10)
            g_test_expect_message ("GPaste", G_LOG_LEVEL_WARNING, "*binding the global shortcuts failed:*");
        complete (f, current, FALSE, 2);
        if (i == 10)
            g_test_assert_expected_messages ();
        if (i < 10)
        {
            while (f->creates->len == count)
                g_main_context_iteration (NULL, TRUE);
            current = g_ptr_array_index (f->creates, count);
        }
    }
    guint count = f->creates->len;
    if (mode == 1)
        respond (old, 2);
    else if (mode == 2)
        method_error (old);
    wait_for_timers (f, 150);
    g_assert_cmpuint (f->creates->len, ==, count);

    /* A real edit gets a fresh budget. */
    current = grab (f, shortcuts);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
}

static void
retry_cancelled (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    complete (f, grab (f, shortcuts), FALSE, 2);
    if (mode == 0)
        g_paste_global_shortcut_client_ungrab_all (f->client);
    else if (mode == 1)
        g_clear_object (&f->client);
    else
        complete (f, bind_session (f, grab (f, changed), FALSE), FALSE, 0);
    guint count = f->creates->len;
    wait_for_timers (f, 150);
    g_assert_cmpuint (f->creates->len, ==, count);
}

static void
stale_create_denied (Fixture *f, gconstpointer user_data)
{
    Request *old = grab (f, shortcuts);
    method_reply (old);
    barrier (f);
    Request *current = grab (f, changed);
    respond (old, GPOINTER_TO_UINT (user_data));
    barrier (f);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    wait_for_timers (f, 150);
    g_assert_cmpuint (f->creates->len, ==, 2);
    activate (f, current);
    g_assert_cmpuint (f->activations, ==, 1);
}

static void
failed_method_retry (Fixture *f, gconstpointer user_data)
{
    Request *session = grab (f, shortcuts);
    Request *request = GPOINTER_TO_UINT (user_data) ? session : bind_session (f, session, FALSE);
    method_error (request);
    barrier (f);
    while (f->creates->len < 2)
        g_main_context_iteration (NULL, TRUE);
    Request *current = g_ptr_array_index (f->creates, 1);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    respond (request, 0);
    barrier (f);
    g_assert_cmpuint (session->closes, ==, 1);
    g_assert_cmpuint (current->closes, ==, 0);
}

/* A method error for a superseded bind must not close the new session. */
static void
stale_bind_method_error (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *old = bind_session (f, grab (f, shortcuts), FALSE);
    Request *current = grab (f, changed);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    method_error (old);
    wait_for_timers (f, 150);
    g_assert_cmpuint (current->closes, ==, 0);
    g_assert_cmpuint (f->creates->len, ==, 2);
}

/* Force the bus and the client's I/O thread to receive both signals before
 * dispatching the main context. This makes the missing-subscription race
 * deterministic without a sleep or a barrier that would dispatch Response. */
static void
flush_received (Fixture *f)
{
    GDBusConnection *connections[] = { f->server, f->connection };
    for (guint i = 0; i < G_N_ELEMENTS (connections); ++i)
    {
        g_autoptr (GError) error = NULL;
        g_autoptr (GVariant) ret = g_dbus_connection_call_sync (connections[i],
            "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetId",
            NULL, G_VARIANT_TYPE ("(s)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        g_assert_no_error (error);
        g_assert_nonnull (ret);
    }
}

static void
immediate_closed (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *session = grab (f, shortcuts);
    method_reply (session);
    barrier (f);
    respond (session, 0);
    g_autoptr (GError) error = NULL;
    g_dbus_connection_emit_signal (session->connection, session->sender, session->session,
        SESSION_IFACE, "Closed", g_variant_new ("(a{sv})", NULL), &error);
    g_assert_no_error (error);
    flush_received (f);
    wait_for_timers (f, 150);
    g_assert_cmpuint (f->creates->len, ==, 1);
    Request *current = grab (f, shortcuts);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
}

static void
changed_request_path (Fixture *f, gconstpointer user_data)
{
    Request *session = grab (f, shortcuts);
    Request *request = GPOINTER_TO_UINT (user_data) ? bind_session (f, session, FALSE) : session;
    g_autofree gchar *path = g_strconcat (request->path, "_actual", NULL);
    g_free (request->path);
    request->path = g_steal_pointer (&path);
    if (request == session)
        request = bind_session (f, session, FALSE);
    complete (f, request, FALSE, 0);
    activate (f, session);
    g_assert_cmpuint (f->activations, ==, 1);
}

static void
invalid_session_handle (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    Request *session = grab (f, shortcuts);
    method_reply (session);
    barrier (f);
    g_auto (GVariantBuilder) results;
    g_variant_builder_init (&results, G_VARIANT_TYPE_VARDICT);
    if (mode != 0)
    {
        GVariant *handle = mode == 1 ? g_variant_new_uint32 (42) :
                           mode == 2 ? g_variant_new_string ("not/a/path") :
                                       g_variant_new_object_path (session->session);
        g_variant_builder_add (&results, "{sv}", "session_handle", handle);
    }
    g_autoptr (GError) error = NULL;
    g_dbus_connection_emit_signal (session->connection, session->sender, session->path,
        "org.freedesktop.portal.Request", "Response", g_variant_new ("(ua{sv})", 0u, &results), &error);
    g_assert_no_error (error);
    session->responded = TRUE;
    barrier (f);
    g_assert_cmpuint (f->binds->len, ==, mode == 3 ? 1 : 0);
    if (mode == 3)
        complete (f, g_ptr_array_index (f->binds, 0), FALSE, 0);
    else
        g_paste_global_shortcut_client_ungrab_all (f->client);
}

static void
unrelated_sender (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    guint registration = 0;
    g_autoptr (GDBusConnection) other = new_server (f, &registration);
    Request *session = grab (f, shortcuts);
    g_autoptr (GError) error = NULL;
    g_dbus_connection_emit_signal (other, session->sender, session->path,
        "org.freedesktop.portal.Request", "Response",
        g_variant_new ("(ua{sv})", 1u, NULL), &error);
    g_assert_no_error (error);
    g_paste_test_bus_barrier (f->connection, other, PORTAL_PATH, PORTAL_IFACE);
    Request *bind = bind_session (f, session, FALSE);
    complete (f, bind, FALSE, 0);
    g_dbus_connection_emit_signal (other, session->sender, session->session,
        SESSION_IFACE, "Closed", g_variant_new ("(a{sv})", NULL), &error);
    g_assert_no_error (error);
    g_dbus_connection_emit_signal (other, session->sender, PORTAL_PATH,
        PORTAL_IFACE, "Activated", g_variant_new ("(osta{sv})", session->session, "first", (guint64) 1, NULL), &error);
    g_assert_no_error (error);
    g_paste_test_bus_barrier (f->connection, other, PORTAL_PATH, PORTAL_IFACE);
    g_assert_cmpuint (f->activations, ==, 0);
    activate (f, session);
    g_assert_cmpuint (f->activations, ==, 1);
    g_dbus_connection_unregister_object (other, registration);
}

static void
retired_deadline (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    Request *session = grab (f, shortcuts);
    if (mode != 1)
    {
        method_reply (session);
        barrier (f);
    }
    if (mode == 2)
        g_clear_object (&f->client);
    else
        g_paste_global_shortcut_client_ungrab_all (f->client);
    wait_for_timers (f, 1100);
    respond (session, 0);
    if (session->invocation)
        method_reply (session);
    barrier (f);
    g_assert_cmpuint (session->closes, ==, 0);
    g_assert_cmpuint (f->binds->len, ==, 0);
    g_assert_cmpuint (f->creates->len, ==, 1);
}

static void
explicit_dispose (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    Request *session = grab (f, shortcuts);
    if (mode != 0)
    {
        Request *bind = bind_session (f, session, FALSE);
        if (mode == 2)
            complete (f, bind, FALSE, 0);
    }
    g_object_run_dispose (G_OBJECT (f->client));
    g_object_run_dispose (G_OBJECT (f->client));
    g_paste_global_shortcut_client_grab_all (f->client, shortcuts);
    g_paste_global_shortcut_client_ungrab_all (f->client);
    if (mode == 0)
        complete (f, session, FALSE, 0);
    barrier (f);
    g_assert_cmpuint (session->closes, ==, 1);
    g_assert_cmpuint (f->creates->len, ==, 1);
}

static void
close_failure (Fixture *f, gconstpointer user_data)
{
    Request *session = grab (f, shortcuts);
    complete (f, bind_session (f, session, FALSE), FALSE, 0);
    session->close_failures = GPOINTER_TO_UINT (user_data) ? 20 : 1;
    if (GPOINTER_TO_UINT (user_data))
        g_test_expect_message ("GPaste", G_LOG_LEVEL_WARNING, "*closing the portal session failed:*");
    /* Cleanup retries must outlive the client. */
    g_clear_object (&f->client);
    barrier (f);
    g_assert_cmpuint (session->closes, ==, 1);
    wait_for_timers (f, 650);
    g_test_assert_expected_messages ();
    g_assert_cmpuint (session->closes, ==, GPOINTER_TO_UINT (user_data) ? 11 : 2);
    wait_for_timers (f, 150);
    g_assert_cmpuint (session->closes, ==, GPOINTER_TO_UINT (user_data) ? 11 : 2);
}

/* Exercise GDBus's real missing-object error, not an injected error name.
 * Re-export after the reply so any incorrectly scheduled retry is observable. */
static void
close_removed_session (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *session = grab (f, shortcuts);
    complete (f, bind_session (f, session, FALSE), FALSE, 0);
    g_assert_true (g_dbus_connection_unregister_object (session->connection, session->registration));
    session->registration = 0;
    g_clear_object (&f->client);
    barrier (f);

    static const GDBusInterfaceVTable vtable = { .method_call = on_close };
    g_autoptr (GError) error = NULL;
    session->registration = g_dbus_connection_register_object (session->connection, session->session,
        f->info->interfaces[1], &vtable, session, NULL, &error);
    g_assert_no_error (error);
    wait_for_timers (f, 150);
    g_assert_cmpuint (session->closes, ==, 0);
}

/* A successful bind can contain any subset, including none. Do not turn that
 * user choice into a fresh permission dialog for the omitted shortcuts. */
static void
partial_bind (Fixture *f, gconstpointer user_data)
{
    const GPasteKeybindingAccelerator accels[] = {
        { "first", "<Control>a", "First" }, { "second", "<Control>b", "Second" }, { NULL, NULL, NULL }
    };
    Request *session = grab (f, accels);
    Request *bind = bind_session (f, session, FALSE);
    method_reply (bind);
    barrier (f);
    g_auto (GVariantBuilder) bound;
    g_variant_builder_init (&bound, G_VARIANT_TYPE ("a(sa{sv})"));
    if (GPOINTER_TO_UINT (user_data))
        g_variant_builder_add (&bound, "(sa{sv})", "first", NULL);
    g_autoptr (GVariant) results = g_variant_ref_sink (
        g_variant_new_parsed ("{'shortcuts': %v}", g_variant_builder_end (&bound)));
    g_autoptr (GVariant) granted = g_variant_lookup_value (results, "shortcuts", G_VARIANT_TYPE ("a(sa{sv})"));
    g_assert_nonnull (granted);
    g_assert_cmpuint (g_variant_n_children (granted), ==, GPOINTER_TO_UINT (user_data));
    g_autoptr (GError) error = NULL;
    g_dbus_connection_emit_signal (bind->connection, bind->sender, bind->path,
        "org.freedesktop.portal.Request", "Response",
        g_variant_new ("(u@a{sv})", 0u, results), &error);
    g_assert_no_error (error);
    bind->responded = TRUE;
    barrier (f);
    g_paste_global_shortcut_client_grab_all (f->client, accels);
    wait_for_timers (f, 150);
    g_assert_cmpuint (f->creates->len, ==, 1);
    g_assert_cmpuint (session->closes, ==, 0);
}


/* This executable doubles as a genuinely D-Bus-activated portal. The child
 * answers immediately in both response-first paths, and dies with its bus. */
static int
activated_portal (void)
{
    static const GDBusInterfaceVTable vtable = { .method_call = on_method };
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusConnection) connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    g_assert_no_error (error);
    g_autoptr (GDBusNodeInfo) info = g_dbus_node_info_new_for_xml (portal_xml, &error);
    g_assert_no_error (error);
    g_autoptr (GPtrArray) creates = g_ptr_array_new_with_free_func (request_free);
    g_autoptr (GPtrArray) binds = g_ptr_array_new_with_free_func (request_free);
    Fixture f = { .info = info, .creates = creates, .binds = binds, .automatic = TRUE };
    guint registration = g_dbus_connection_register_object (connection, PORTAL_PATH,
        info->interfaces[0], &vtable, &f, NULL, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_paste_test_bus_name_call (connection, "RequestName",
        g_variant_new ("(su)", PORTAL_NAME, 1u)), ==, 1);
    g_autoptr (GMainLoop) loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);
    g_dbus_connection_unregister_object (connection, registration);
    return 0;
}

static void
activation (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    g_autofree gchar *owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (f->client));
    g_assert_null (owner);
    g_paste_global_shortcut_client_grab_all (f->client, shortcuts);
    while (!f->activations)
        g_main_context_iteration (NULL, TRUE);
    g_autoptr (GError) error = NULL;
    g_autoptr (GVariant) counts = g_dbus_connection_call_sync (f->connection,
        PORTAL_NAME, PORTAL_PATH, PORTAL_IFACE, "GetCounts", NULL,
        G_VARIANT_TYPE ("(uuu)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    g_assert_no_error (error);
    guint creates, binds, closes;
    g_variant_get (counts, "(uuu)", &creates, &binds, &closes);
    g_assert_cmpuint (creates, ==, 1);
    g_assert_cmpuint (binds, ==, 1);
    g_assert_cmpuint (closes, ==, 0);
    g_assert_cmpuint (f->activations, ==, 1);
}

/* Owner death, unlike a name handoff, makes even a retired response impossible.
 * No Close should be retried against the replacement or activate a dead name. */
static void
owner_dies (Fixture *f, gconstpointer user_data)
{
    guint mode = GPOINTER_TO_UINT (user_data);
    Request *old = grab (f, shortcuts);
    if (mode == 0)
        complete (f, bind_session (f, old, FALSE), FALSE, 0);
    else if (mode == 1)
    {
        method_reply (old);
        barrier (f);
    }
    f->replacement = new_server (f, &f->replacement_registration);
    g_autoptr (GError) error = NULL;
    g_dbus_connection_close_sync (f->server, NULL, &error);
    g_assert_no_error (error);
    barrier (f);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->replacement, "RequestName",
        g_variant_new ("(su)", PORTAL_NAME, 2u)), ==, 1);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, 2);
    Request *current = g_ptr_array_index (f->creates, 1);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
    wait_for_timers (f, 150);
    g_assert_cmpuint (f->creates->len, ==, 2);
    g_assert_cmpuint (current->closes, ==, 0);
    /* The dead server cannot send any of the pending teardown replies. */
    old->responded = TRUE;
    /* Returning also releases GDBus's invocation reference, even though the
     * connection can no longer deliver the reply. */
    if (old->invocation)
        method_error (old);
}


static void
activation_disposed (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    g_paste_global_shortcut_client_grab_all (f->client, shortcuts);
    g_clear_object (&f->client);
    guint creates = 0, binds = 0, closes = 0;
    /* Querying the activated child also synchronizes its outgoing Response.
     * Dispatch that locally so the abandoned session can be closed. */
    while (!closes)
    {
        g_autoptr (GError) error = NULL;
        g_autoptr (GVariant) counts = g_dbus_connection_call_sync (f->connection,
            PORTAL_NAME, PORTAL_PATH, PORTAL_IFACE, "GetCounts", NULL,
            G_VARIANT_TYPE ("(uuu)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
        g_assert_no_error (error);
        g_variant_get (counts, "(uuu)", &creates, &binds, &closes);
        while (g_main_context_iteration (NULL, FALSE))
            ;
    }
    g_assert_cmpuint (creates, ==, 1);
    g_assert_cmpuint (binds, ==, 0);
    g_assert_cmpuint (closes, ==, 1);
}

static void
duplicate_pending (Fixture *f, gconstpointer user_data)
{
    Request *session = grab (f, shortcuts);
    Request *bind = NULL;
    if (GPOINTER_TO_UINT (user_data))
        bind = bind_session (f, session, FALSE);
    g_paste_global_shortcut_client_grab_all (f->client, shortcuts);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, 1);
    g_assert_cmpuint (session->closes, ==, 0);
    if (!bind)
        bind = bind_session (f, session, FALSE);
    complete (f, bind, FALSE, 0);
}

static void
retired_method_denied (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *old = grab (f, shortcuts);
    method_error (old);
    barrier (f);
    /* Its task is already failed. A cleanup-only response cannot revoke the
     * current attempt while the retry timer is waiting to run. */
    respond (old, 1);
    barrier (f);
    while (f->creates->len < 2)
        g_main_context_iteration (NULL, TRUE);
    Request *current = g_ptr_array_index (f->creates, 1);
    complete (f, bind_session (f, current, FALSE), FALSE, 0);
}

static void
keybinder_activated (GPasteKeybinding *binding G_GNUC_UNUSED,
                     gpointer          user_data)
{
    ++*(guint *) user_data;
}

static void
keybinder_switch (Fixture *f, gconstpointer user_data)
{
    gboolean initially_enabled = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_paste_settings_set_keybindings_enabled (settings, initially_enabled);
    g_paste_settings_set_launch_ui (settings, "<Control>a");
    g_autoptr (GPasteKeybinder) keybinder = g_paste_keybinder_new (settings, f->client);
    guint activations = 0;
    g_paste_keybinder_add_keybinding (keybinder,
                                      g_paste_keybinding_new ("first", "First", g_paste_settings_get_launch_ui,
                                                              keybinder_activated, &activations));
    g_paste_keybinder_activate_all (keybinder);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, initially_enabled ? 1 : 0);
    if (!initially_enabled)
    {
        g_paste_settings_set_keybindings_enabled (settings, TRUE);
        wait_for_timers (f, 350);
        g_assert_cmpuint (f->creates->len, ==, 1);
    }

    Request *session = g_ptr_array_index (f->creates, 0);
    complete (f, bind_session (f, session, FALSE), FALSE, 0);
    activate (f, session);
    g_assert_cmpuint (activations, ==, 1);

    g_paste_keybinder_activate_all (keybinder);
    barrier (f);
    g_assert_cmpuint (f->creates->len, ==, 1);
    g_assert_cmpuint (session->closes, ==, 0);

    g_paste_settings_set_keybindings_enabled (settings, FALSE);
    wait_for_timers (f, 350);
    g_assert_cmpuint (session->closes, ==, 1);
    activate (f, session);
    g_assert_cmpuint (activations, ==, 1);

    g_paste_settings_set_keybindings_enabled (settings, TRUE);
    wait_for_timers (f, 350);
    g_assert_cmpuint (f->creates->len, ==, 2);
    Request *replacement = g_ptr_array_index (f->creates, 1);
    complete (f, bind_session (f, replacement, FALSE), FALSE, 0);
    activate (f, replacement);
    g_assert_cmpuint (activations, ==, 2);

    g_paste_settings_set_launch_ui (settings, "<Control>b");
    g_clear_object (&keybinder);
    wait_for_timers (f, 350);
    g_assert_cmpuint (replacement->closes, ==, 1);
    g_assert_cmpuint (f->creates->len, ==, 2);
}

int
main (int argc, char **argv)
{
    if (argc == 2 && g_str_equal (argv[1], "--activated-portal"))
        return activated_portal ();

    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add ("/keybinder/switch-enabled", Fixture, GINT_TO_POINTER (TRUE), setup, keybinder_switch, teardown);
    g_test_add ("/keybinder/switch-disabled", Fixture, GINT_TO_POINTER (FALSE), setup, keybinder_switch, teardown);
    /* Unexpected warnings are failures; exhaustion tests expect theirs
     * explicitly. */
    g_test_add ("/portal/method-first", Fixture, GINT_TO_POINTER (FALSE), setup, reply_order, teardown);
    g_test_add ("/portal/response-first", Fixture, GINT_TO_POINTER (TRUE), setup, reply_order, teardown);
    g_test_add ("/portal/superseded-create", Fixture, NULL, setup, superseded_create, teardown);
    g_test_add ("/portal/disable-pending-bind", Fixture, NULL, setup, disable_pending_bind, teardown);
    g_test_add ("/portal/denied-bind", Fixture, NULL, setup, denied_bind, teardown);
    g_test_add ("/portal/failed-create-late-response", Fixture, NULL, setup, failed_create_late_response, teardown);
    g_test_add ("/portal/restart", Fixture, GUINT_TO_POINTER (0), setup, restart, teardown);
    g_test_add ("/portal/owner-handoff", Fixture, GUINT_TO_POINTER (1), setup, restart, teardown);
    g_test_add ("/portal/owner-handoff-pending-create", Fixture, GUINT_TO_POINTER (2), setup, restart, teardown);
    g_test_add ("/portal/retired-create-handoff", Fixture, NULL, setup, retired_create_handoff, teardown);
    g_test_add ("/portal/session-closed", Fixture, GUINT_TO_POINTER (0), setup, session_closed, teardown);
    g_test_add ("/portal/session-closed-pending-response", Fixture, GUINT_TO_POINTER (1), setup, session_closed, teardown);
    g_test_add ("/portal/session-closed-pending-method", Fixture, GUINT_TO_POINTER (2), setup, session_closed, teardown);
    g_test_add ("/portal/session-closed-restart", Fixture, NULL, setup, session_closed_restart, teardown);
    g_test_add ("/portal/denied-bind-restart", Fixture, GUINT_TO_POINTER (0), setup, denied_restart, teardown);
    g_test_add ("/portal/denied-create-restart", Fixture, GUINT_TO_POINTER (1), setup, denied_restart, teardown);
    g_test_add ("/portal/failed-bind-restart", Fixture, GUINT_TO_POINTER (2), setup, denied_restart, teardown);
    g_test_add ("/portal/failed-create-restart", Fixture, GUINT_TO_POINTER (3), setup, denied_restart, teardown);
    g_test_add ("/portal/failed-bind-retry", Fixture, GUINT_TO_POINTER (0), setup, failed_bind_retry, teardown);
    g_test_add ("/portal/failed-create-retry", Fixture, GUINT_TO_POINTER (1), setup, failed_bind_retry, teardown);
    g_test_add ("/portal/drop-with-pending-create", Fixture, NULL, setup, drop_with_pending_create, teardown);
    g_test_add ("/portal/drop-with-pending-create-method", Fixture, GUINT_TO_POINTER (1), setup, drop_with_pending_create, teardown);
    g_test_add ("/portal/drop-with-pending-bind-method", Fixture, GUINT_TO_POINTER (0), setup, drop_with_pending_bind, teardown);
    g_test_add ("/portal/drop-with-pending-bind-response", Fixture, GUINT_TO_POINTER (1), setup, drop_with_pending_bind, teardown);
    g_test_add ("/portal/drop-with-two-pending-methods", Fixture, GUINT_TO_POINTER (2), setup, drop_with_pending_bind, teardown);
    g_test_add ("/portal/drop-after-bind-response", Fixture, GUINT_TO_POINTER (3), setup, drop_with_pending_bind, teardown);
    g_test_add ("/portal/malformed-response", Fixture, NULL, setup, malformed_response, teardown);
    g_test_add ("/portal/trigger-format", Fixture, NULL, setup, trigger_format, teardown);
    g_test_add ("/portal/retry-exhaustion", Fixture, GUINT_TO_POINTER (0), setup, retry_exhaustion, teardown);
    g_test_add ("/portal/stale-failure-after-exhaustion", Fixture, GUINT_TO_POINTER (1), setup, retry_exhaustion, teardown);
    g_test_add ("/portal/retry-disable", Fixture, GUINT_TO_POINTER (0), setup, retry_cancelled, teardown);
    g_test_add ("/portal/retry-dispose", Fixture, GUINT_TO_POINTER (1), setup, retry_cancelled, teardown);
    g_test_add ("/portal/retry-replaced", Fixture, GUINT_TO_POINTER (2), setup, retry_cancelled, teardown);
    g_test_add ("/portal/stale-create-denied", Fixture, GUINT_TO_POINTER (1), setup, stale_create_denied, teardown);
    g_test_add ("/portal/stale-create-failed", Fixture, GUINT_TO_POINTER (2), setup, stale_create_denied, teardown);
    g_test_add ("/portal/create-method-retry", Fixture, GUINT_TO_POINTER (1), setup, failed_method_retry, teardown);
    g_test_add ("/portal/bind-method-retry", Fixture, GUINT_TO_POINTER (0), setup, failed_method_retry, teardown);
    g_test_add ("/portal/stale-bind-method-error", Fixture, GUINT_TO_POINTER (0), setup, stale_bind_method_error, teardown);
    g_test_add ("/portal/immediate-closed", Fixture, GUINT_TO_POINTER (0), setup, immediate_closed, teardown);
    g_test_add ("/portal/create-actual-path", Fixture, GUINT_TO_POINTER (0), setup, changed_request_path, teardown);
    g_test_add ("/portal/bind-actual-path", Fixture, GUINT_TO_POINTER (1), setup, changed_request_path, teardown);
    g_test_add ("/portal/missing-handle", Fixture, GUINT_TO_POINTER (0), setup, invalid_session_handle, teardown);
    g_test_add ("/portal/wrong-handle-type", Fixture, GUINT_TO_POINTER (1), setup, invalid_session_handle, teardown);
    g_test_add ("/portal/invalid-handle-path", Fixture, GUINT_TO_POINTER (2), setup, invalid_session_handle, teardown);
    g_test_add ("/portal/object-path-handle", Fixture, GUINT_TO_POINTER (3), setup, invalid_session_handle, teardown);
    g_test_add ("/portal/unrelated-sender", Fixture, GUINT_TO_POINTER (0), setup, unrelated_sender, teardown);
    g_test_add ("/portal/retired-deadline", Fixture, GUINT_TO_POINTER (0), setup, retired_deadline, teardown);
    g_test_add ("/portal/retired-deadline-pending-method", Fixture, GUINT_TO_POINTER (1), setup, retired_deadline, teardown);
    g_test_add ("/portal/retired-deadline-disposed", Fixture, GUINT_TO_POINTER (2), setup, retired_deadline, teardown);
    g_test_add ("/portal/dispose-pending-create", Fixture, GUINT_TO_POINTER (0), setup, explicit_dispose, teardown);
    g_test_add ("/portal/dispose-pending-bind", Fixture, GUINT_TO_POINTER (1), setup, explicit_dispose, teardown);
    g_test_add ("/portal/dispose-bound", Fixture, GUINT_TO_POINTER (2), setup, explicit_dispose, teardown);
    g_test_add ("/portal/close-removed-session", Fixture, NULL, setup, close_removed_session, teardown);
    g_test_add ("/portal/close-retry", Fixture, GUINT_TO_POINTER (0), setup, close_failure, teardown);
    g_test_add ("/portal/close-retry-exhaustion", Fixture, GUINT_TO_POINTER (1), setup, close_failure, teardown);
    g_test_add ("/portal/empty-bind", Fixture, GUINT_TO_POINTER (0), setup, partial_bind, teardown);
    g_test_add ("/portal/partial-bind", Fixture, GUINT_TO_POINTER (1), setup, partial_bind, teardown);
    g_test_add ("/portal/activation", Fixture, GUINT_TO_POINTER (100), setup, activation, teardown);
    g_test_add ("/portal/owner-dies-bound", Fixture, GUINT_TO_POINTER (0), setup, owner_dies, teardown);
    g_test_add ("/portal/owner-dies-response-pending", Fixture, GUINT_TO_POINTER (1), setup, owner_dies, teardown);
    g_test_add ("/portal/owner-dies-method-pending", Fixture, GUINT_TO_POINTER (2), setup, owner_dies, teardown);
    g_test_add ("/portal/activation-disposed", Fixture, GUINT_TO_POINTER (100), setup, activation_disposed, teardown);
    g_test_add ("/portal/duplicate-pending-create", Fixture, GUINT_TO_POINTER (0), setup, duplicate_pending, teardown);
    g_test_add ("/portal/duplicate-pending-bind", Fixture, GUINT_TO_POINTER (1), setup, duplicate_pending, teardown);
    g_test_add ("/portal/retired-method-denied", Fixture, NULL, setup, retired_method_denied, teardown);
    g_test_add ("/portal/stale-method-after-exhaustion", Fixture, GUINT_TO_POINTER (2), setup, retry_exhaustion, teardown);
    g_test_add ("/portal/retired-create-handoff-pending-method", Fixture, GUINT_TO_POINTER (1), setup, retired_create_handoff, teardown);
    return g_test_run ();
}
