// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-bus.h>
#include <gpaste/gpaste-gnome-shell-client.h>
#include <gpaste/gpaste-keybinding-provider.h>

#define SHELL_NAME  "org.gnome.Shell"
#define SHELL_PATH  "/org/gnome/Shell"
#define SHELL_IFACE "org.gnome.Shell"

/* A real proxy on a private bus, with a Shell that lets each test decide when
 * a grab finishes. No desktop session or access to the user's shortcuts. */
typedef struct
{
    GTestDBus *bus;
    GDBusConnection *server;
    GDBusConnection *replacement;
    GDBusConnection *connection;
    GPasteGnomeShellClient *client;
    GDBusNodeInfo *info;
    GPtrArray *pending;
    GArray *released;
    guint registration;
    guint replacement_registration;
    guint grabs;
    guint replacement_releases;
    gboolean ready;
} Fixture;

static const GPasteKeybindingAccelerator shortcuts[] = {
    { "first", "<Control>a", "First" },
    { "second", "<Control>b", "Second" },
    { NULL, NULL, NULL },
};

static void
on_method_call (GDBusConnection *connection,
                const gchar *sender G_GNUC_UNUSED,
                const gchar *path G_GNUC_UNUSED,
                const gchar *interface G_GNUC_UNUSED,
                const gchar *method,
                GVariant *parameters,
                GDBusMethodInvocation *invocation,
                gpointer user_data)
{
    Fixture *f = user_data;

    if (g_str_equal (method, "GrabAccelerators"))
    {
        g_autoptr (GVariant) accels = g_variant_get_child_value (parameters, 0);
        g_assert_cmpuint (g_variant_n_children (accels), ==, 2);
        if (f->replacement)
            g_assert_true (connection == f->replacement);
        f->grabs++;
        /* One per shell at the very most, and a handoff leaves the one the shell
         * that left never answered outstanding beside the replacement's. */
        g_ptr_array_add (f->pending, g_object_ref (invocation));
    }
    else if (g_str_equal (method, "UngrabAccelerators"))
    {
        g_autoptr (GVariant) actions = g_variant_get_child_value (parameters, 0);
        gsize n;
        const guint32 *ids = g_variant_get_fixed_array (actions, &n, sizeof (guint32));
        if (connection == f->replacement)
            f->replacement_releases += n;
        g_array_append_vals (f->released, ids, n);
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(b)", TRUE));
    }
    else
    {
        g_assert_cmpstr (method, ==, "Barrier");
        g_dbus_method_invocation_return_value (invocation, NULL);
    }
}

/* Whichever shell is up: a handoff leaves the replacement holding the name. */
static void
barrier (Fixture *f)
{
    g_paste_test_bus_barrier (f->connection, f->replacement ? f->replacement : f->server,
                              SHELL_PATH, SHELL_IFACE);
}

static void
on_client_ready (GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
    Fixture *f = user_data;
    g_autoptr (GError) error = NULL;
    f->client = g_paste_gnome_shell_client_new_finish (result, &error);
    g_assert_no_error (error);
    f->ready = TRUE;
}

static const gchar shell_xml[] =
    "<node><interface name='" SHELL_IFACE "'>"
    "<method name='GrabAccelerators'><arg type='a(suu)' direction='in'/>"
    "<arg type='au' direction='out'/></method>"
    "<method name='UngrabAccelerators'><arg type='au' direction='in'/>"
    "<arg type='b' direction='out'/></method>"
    "<method name='Barrier'/></interface></node>";

static GDBusConnection *
new_server (Fixture *f, guint *registration)
{
    static const GDBusInterfaceVTable vtable = { .method_call = on_method_call };

    return g_paste_test_bus_new_server (f->bus, SHELL_PATH, f->info->interfaces[0], &vtable, f, registration);
}

static void
setup (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    f->bus = g_test_dbus_new (G_TEST_DBUS_NONE);
    g_test_dbus_up (f->bus);
    f->info = g_dbus_node_info_new_for_xml (shell_xml, &error);
    g_assert_no_error (error);
    /* Everything the method handler touches, before the object it is on can be
     * called: registering it is what makes it reachable. */
    f->pending = g_ptr_array_new_with_free_func (g_object_unref);
    f->released = g_array_new (FALSE, FALSE, sizeof (guint32));
    f->server = new_server (f, &f->registration);
    /* ALLOW_REPLACEMENT lets a test exercise `gnome-shell --replace`. */
    g_assert_cmpuint (g_paste_test_bus_name_call (f->server, "RequestName",
                                                  g_variant_new ("(su)", SHELL_NAME, 1u)), ==, 1);
    f->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    g_assert_no_error (error);
    g_paste_gnome_shell_client_new (on_client_ready, f);
    while (!f->ready)
        g_main_context_iteration (NULL, TRUE);
    barrier (f);
}

/* Answer the oldest grab the fake Shell has yet to reply to. */
static void
answer_grab (Fixture *f, guint32 first, guint32 second)
{
    g_assert_cmpuint (f->pending->len, >, 0);
    g_autoptr (GDBusMethodInvocation) invocation = g_ptr_array_steal_index (f->pending, 0);
    guint32 ids[] = { first, second };
    g_dbus_method_invocation_return_value (invocation, g_variant_new ("(@au)",
        g_variant_new_fixed_array (G_VARIANT_TYPE_UINT32, ids, G_N_ELEMENTS (ids), sizeof (guint32))));
    barrier (f);
}

static void
teardown (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    g_assert_cmpuint (f->pending->len, ==, 0);
    if (f->client)
        g_paste_keybinding_provider_ungrab_all (G_PASTE_KEYBINDING_PROVIDER (f->client));
    barrier (f);
    g_clear_object (&f->client);
    barrier (f);
    g_clear_object (&f->connection);
    g_dbus_connection_unregister_object (f->server, f->registration);
    if (f->replacement)
        g_dbus_connection_unregister_object (f->replacement, f->replacement_registration);
    g_clear_object (&f->replacement);
    g_clear_object (&f->server);
    g_clear_pointer (&f->pending, g_ptr_array_unref);
    g_clear_pointer (&f->released, g_array_unref);
    g_clear_pointer (&f->info, g_dbus_node_info_unref);
    g_test_dbus_down (f->bus);
    g_clear_object (&f->bus);
}

static void
unchanged_complete (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    answer_grab (f, 1, 2);
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    g_assert_cmpuint (f->grabs, ==, 1);
    g_assert_cmpuint (f->released->len, ==, 0);
}

static void
retry_partial (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    answer_grab (f, 1, 0);
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    g_assert_cmpuint (f->grabs, ==, 2);
    g_assert_cmpuint (f->released->len, ==, 1);
    g_assert_cmpuint (g_array_index (f->released, guint32, 0), ==, 1);
    answer_grab (f, 2, 3);
}

static void
unchanged_pending (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    g_assert_cmpuint (f->grabs, ==, 1);
    answer_grab (f, 1, 2);
    g_assert_cmpuint (f->grabs, ==, 1);
    g_assert_cmpuint (f->released->len, ==, 0);
}

static void
disable_pending (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    const GPasteKeybindingAccelerator empty[] = { { NULL, NULL, NULL } };
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), empty);
    answer_grab (f, 1, 0);
    g_assert_cmpuint (f->grabs, ==, 1);
    g_assert_cmpuint (f->released->len, ==, 1);
    g_assert_cmpuint (g_array_index (f->released, guint32, 0), ==, 1);
}

/* `gnome-shell --replace` hands the name straight to the replacement, with no
 * moment in between where nobody owns it. g_bus_watch_name () reports it as a
 * vanish and an appear all the same, so what the shell that left was holding
 * for us goes the way it would have on a plain restart. */
static void
owner_handoff (Fixture *f, gconstpointer user_data)
{
    gboolean pending = GPOINTER_TO_UINT (user_data);

    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    g_assert_cmpuint (f->grabs, ==, 1);
    if (!pending)
        answer_grab (f, 1, 2);

    f->replacement = new_server (f, &f->replacement_registration);
    g_assert_cmpuint (g_paste_test_bus_name_call (f->replacement, "RequestName",
                                                  g_variant_new ("(su)", SHELL_NAME, 2u)), ==, 1);
    barrier (f);

    /* Count each destination separately: a shared count lets a re-grab sent
     * to the old owner masquerade as a successful recovery. Answer the new
     * owner first, leaving the abandoned old call outstanding. */
    g_assert_cmpuint (f->grabs, ==, 2);
    g_assert_cmpuint (f->replacement_releases, ==, 0);
    g_assert_cmpuint (f->released->len, ==, pending ? 0 : 2);
    guint index = pending ? 1 : 0;
    GDBusMethodInvocation *current = g_ptr_array_index (f->pending, index);
    g_assert_true (g_dbus_method_invocation_get_connection (current) == f->replacement);
    if (pending)
    {
        gpointer old = f->pending->pdata[0];
        f->pending->pdata[0] = f->pending->pdata[1];
        f->pending->pdata[1] = old;
    }
    answer_grab (f, 1, 2);

    if (pending)
    {
        answer_grab (f, 1, 2);
        g_assert_cmpuint (f->released->len, ==, 2);
    }
    g_assert_cmpuint (f->grabs, ==, 2);
    g_assert_cmpuint (f->replacement_releases, ==, 0);
}

static void
drop_client (Fixture *f, gconstpointer user_data)
{
    gboolean pending = GPOINTER_TO_UINT (user_data);
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    if (!pending)
        answer_grab (f, 1, 2);

    GPasteGnomeShellClient *weak = f->client;
    g_object_add_weak_pointer (G_OBJECT (weak), (gpointer *) &weak);
    g_clear_object (&f->client);
    barrier (f);
    g_assert_null (weak);
    if (pending)
        answer_grab (f, 1, 2);
    g_assert_cmpuint (f->released->len, ==, 2);
    g_assert_cmpuint (f->grabs, ==, 1);
}

static void
changed_pending (Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    const GPasteKeybindingAccelerator changed[] = {
        { "first", "<Control>c", "First" },
        { "second", "<Control>d", "Second" },
        { NULL, NULL, NULL },
    };
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), shortcuts);
    barrier (f);
    g_paste_keybinding_provider_grab_all (G_PASTE_KEYBINDING_PROVIDER (f->client), changed);
    barrier (f);
    g_assert_cmpuint (f->grabs, ==, 1);
    answer_grab (f, 1, 2);
    g_assert_cmpuint (f->grabs, ==, 2);
    g_assert_cmpuint (f->released->len, ==, 2);
    GDBusMethodInvocation *current = g_ptr_array_index (f->pending, 0);
    g_autoptr (GVariant) accels = g_variant_get_child_value (g_dbus_method_invocation_get_parameters (current), 0);
    const gchar *accel;
    guint mode, flags;
    g_variant_get_child (accels, 0, "(&suu)", &accel, &mode, &flags);
    g_assert_cmpstr (accel, ==, "<Control>c");
    answer_grab (f, 3, 4);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    /* A refused accelerator intentionally logs a warning. Keep criticals
     * fatal. */
    g_log_set_always_fatal (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);
    g_test_add ("/shell-provider/unchanged-complete", Fixture, NULL, setup, unchanged_complete, teardown);
    g_test_add ("/shell-provider/retry-partial", Fixture, NULL, setup, retry_partial, teardown);
    g_test_add ("/shell-provider/unchanged-pending", Fixture, NULL, setup, unchanged_pending, teardown);
    g_test_add ("/shell-provider/disable-pending", Fixture, NULL, setup, disable_pending, teardown);
    g_test_add ("/shell-provider/owner-handoff", Fixture, GUINT_TO_POINTER (0), setup, owner_handoff, teardown);
    g_test_add ("/shell-provider/owner-handoff-pending-grab", Fixture, GUINT_TO_POINTER (1), setup, owner_handoff, teardown);
    g_test_add ("/shell-provider/drop-pending-client", Fixture, GUINT_TO_POINTER (1), setup, drop_client, teardown);
    g_test_add ("/shell-provider/drop-bound-client", Fixture, GUINT_TO_POINTER (0), setup, drop_client, teardown);
    g_test_add ("/shell-provider/changed-pending", Fixture, NULL, setup, changed_pending, teardown);
    return g_test_run ();
}
