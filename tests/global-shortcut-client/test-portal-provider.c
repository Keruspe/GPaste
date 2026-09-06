// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste/gpaste-keybinding-provider.h>
#include <gpaste-gtk4/gpaste-gtk-global-shortcut-client.h>

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
    guint registration;
    guint closes;
    gboolean responded;
} Request;

typedef struct
{
    GTestDBus *bus;
    GDBusConnection *server;
    GDBusConnection *replacement;
    GDBusConnection *connection;
    GPasteGtkGlobalShortcutClient *client;
    GDBusNodeInfo *info;
    GPtrArray *creates;
    GPtrArray *binds;
    guint registration;
    guint replacement_registration;
    guint activations;
    gboolean ready;
} Fixture;

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
        g_ptr_array_add (f->binds, g_steal_pointer (&request));
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
    f->client = g_paste_gtk_global_shortcut_client_new_finish (result, &error);
    g_assert_no_error (error);
    f->ready = TRUE;
}

static void
on_activated (GPasteKeybindingProvider *provider G_GNUC_UNUSED, const gchar *id, gpointer user_data)
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

static void
setup (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    static const gchar xml[] =
        "<node><interface name='" PORTAL_IFACE "'>"
        "<method name='CreateSession'><arg type='a{sv}' direction='in'/>"
        "<arg type='o' direction='out'/></method>"
        "<method name='BindShortcuts'><arg type='o' direction='in'/>"
        "<arg type='a(sa{sv})' direction='in'/><arg type='s' direction='in'/>"
        "<arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
        "<method name='Barrier'/></interface>"
        "<interface name='" SESSION_IFACE "'><method name='Close'/></interface></node>";
    g_autoptr (GError) error = NULL;
    f->bus = g_test_dbus_new (G_TEST_DBUS_NONE);
    g_test_dbus_up (f->bus);
    f->info = g_dbus_node_info_new_for_xml (xml, &error);
    g_assert_no_error (error);
    f->creates = g_ptr_array_new_with_free_func (request_free);
    f->binds = g_ptr_array_new_with_free_func (request_free);
    f->server = new_server (f, &f->registration);
    /* ALLOW_REPLACEMENT lets tests exercise a direct owner handoff. */
    g_assert_cmpuint (g_paste_test_bus_name_call (f->server, "RequestName",
                                                  g_variant_new ("(su)", PORTAL_NAME, 1u)), ==, 1);
    f->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    g_assert_no_error (error);
    g_paste_gtk_global_shortcut_client_new (on_client_ready, f);
    while (!f->ready)
        g_main_context_iteration (NULL, TRUE);
    g_signal_connect (f->client, "keybinding-activated", G_CALLBACK (on_activated), f);
    barrier (f);
}

static void
teardown (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    if (f->client)
        g_paste_keybinding_provider_ungrab_all (G_PASTE_KEYBINDING_PROVIDER (f->client));
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
        GPasteGtkGlobalShortcutClient *weak = f->client;
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
}

static Request *
grab (Fixture *f, const GPasteKeybindingAccelerator *accels)
{
    guint count = f->creates->len;
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), accels);
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
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
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
    g_paste_keybinding_provider_ungrab_all (G_PASTE_KEYBINDING_PROVIDER (f->client));
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
    Request *current = g_ptr_array_index (f->creates, 1);
    complete (f, bind_session (f, current, TRUE), TRUE, 0);
    activate (f, current);
    g_assert_cmpuint (f->activations, ==, 1);
    g_assert_cmpuint (current->closes, ==, 0);
}

/* A CreateSession an ungrab_all () retired is kept, not cancelled: only its
 * Response can carry the handle of the session it may still create. That makes
 * it the one thing naming the portal it was issued to, so a handoff while it is
 * outstanding has to be seen through it -- otherwise it waits on a Response
 * that can never come until its own deadline says so. */
static void
retired_create_handoff (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *old = grab (f, shortcuts);
    method_reply (old);
    barrier (f);

    g_paste_keybinding_provider_ungrab_all (G_PASTE_KEYBINDING_PROVIDER (f->client));
    barrier (f);

    f->replacement = new_server (f, &f->replacement_registration);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->replacement, "RequestName",
                                                  g_variant_new ("(su)", PORTAL_NAME, 2u)), ==, 1);
    barrier (f);

    /* The portal that took the call is gone, and the session it went on to
     * answer with went with it: nothing is closed on a name that no longer
     * answers, and the Response reaches a request already let go of. */
    respond (old, 0);
    barrier (f);
    g_assert_cmpuint (old->closes, ==, 0);
    g_assert_cmpuint (f->creates->len, ==, 1);

    /* Nor is the request left holding the client: what it was waiting for can
     * no longer be sent, and only its own ten minute deadline would end that. */
    GPasteGtkGlobalShortcutClient *weak = f->client;
    g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
    g_clear_object (&f->client);
    barrier (f);
    g_assert_null (weak);
}

/* A request in flight must not be what keeps the client alive: dropping the
 * client has to reach dispose (), which is the one thing that gives up the
 * session and the grabs that go with it. */
static void
drop_with_pending_create (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    Request *session = grab (f, shortcuts);
    method_reply (session);
    barrier (f);

    GPasteGtkGlobalShortcutClient *weak = f->client;
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
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    /* Denial is an expected warning; malformed lifecycle handling is not. */
    g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
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
    g_test_add ("/portal/drop-with-pending-create", Fixture, NULL, setup, drop_with_pending_create, teardown);
    return g_test_run ();
}
