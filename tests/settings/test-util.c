// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <gpaste-3/gpaste-util.h>

static GAsyncReadyCallback proxy_callback;
static gpointer proxy_data;
static gboolean refuse_proxy;
static gboolean argument_destroyed;
static guint calls;

static void
fake_proxy_new (GBusType            bus_type G_GNUC_UNUSED,
                GDBusProxyFlags     flags G_GNUC_UNUSED,
                GDBusInterfaceInfo *info G_GNUC_UNUSED,
                const gchar        *name G_GNUC_UNUSED,
                const gchar        *path G_GNUC_UNUSED,
                const gchar        *interface G_GNUC_UNUSED,
                GCancellable       *cancellable G_GNUC_UNUSED,
                GAsyncReadyCallback callback,
                gpointer            data)
{
    proxy_callback = callback;
    proxy_data = data;
}

static GDBusProxy *
fake_proxy_new_finish (GAsyncResult *result G_GNUC_UNUSED,
                       GError      **error)
{
    if (refuse_proxy)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "refused");
        return NULL;
    }

    return g_object_new (G_TYPE_DBUS_PROXY, NULL);
}

static GDBusProxy *
fake_proxy_new_sync (GBusType            bus_type G_GNUC_UNUSED,
                     GDBusProxyFlags     flags G_GNUC_UNUSED,
                     GDBusInterfaceInfo *info G_GNUC_UNUSED,
                     const gchar        *name G_GNUC_UNUSED,
                     const gchar        *path G_GNUC_UNUSED,
                     const gchar        *interface G_GNUC_UNUSED,
                     GCancellable       *cancellable G_GNUC_UNUSED,
                     GError            **error)
{
    return fake_proxy_new_finish (NULL, error);
}

/* What either call is handed, consumed as GDBus consumes it. */
static void
check_parameters (const gchar *method,
                  GVariant    *parameters)
{
    g_autoptr (GVariant) params = g_variant_ref_sink (parameters);
    g_autoptr (GVariant) args = g_variant_get_child_value (params, 1);
    g_autoptr (GVariant) boxed = g_variant_get_child_value (args, 0);
    g_autoptr (GVariant) arg = g_variant_get_variant (boxed);

    g_assert_cmpstr (method, ==, "ActivateAction");
    g_assert_cmpstr (g_variant_get_string (arg, NULL), ==, "payload");
    ++calls;
}

static void
fake_proxy_call (GDBusProxy         *proxy G_GNUC_UNUSED,
                 const gchar        *method,
                 GVariant           *parameters,
                 GDBusCallFlags      flags G_GNUC_UNUSED,
                 gint                timeout G_GNUC_UNUSED,
                 GCancellable       *cancellable G_GNUC_UNUSED,
                 GAsyncReadyCallback callback G_GNUC_UNUSED,
                 gpointer            data G_GNUC_UNUSED)
{
    check_parameters (method, parameters);
}

static GVariant *
fake_proxy_call_sync (GDBusProxy    *proxy G_GNUC_UNUSED,
                      const gchar   *method,
                      GVariant      *parameters,
                      GDBusCallFlags flags G_GNUC_UNUSED,
                      gint           timeout G_GNUC_UNUSED,
                      GCancellable  *cancellable G_GNUC_UNUSED,
                      GError       **error G_GNUC_UNUSED)
{
    check_parameters (method, parameters);

    return g_variant_ref_sink (g_variant_new ("()"));
}

#define g_dbus_proxy_new_for_bus fake_proxy_new
#define g_dbus_proxy_new_for_bus_finish fake_proxy_new_finish
#define g_dbus_proxy_new_for_bus_sync fake_proxy_new_sync
#define g_dbus_proxy_call fake_proxy_call
#define g_dbus_proxy_call_sync fake_proxy_call_sync
#include "../../src/libgpaste/gpaste-3/gpaste-util.c"
#undef g_dbus_proxy_call_sync
#undef g_dbus_proxy_call
#undef g_dbus_proxy_new_for_bus_sync
#undef g_dbus_proxy_new_for_bus_finish
#undef g_dbus_proxy_new_for_bus

typedef struct
{
    const gchar *path;
    gboolean     sync;
    gboolean     floating;
    gboolean     refused;
} ActivationCase;

static const ActivationCase activation_cases[] = {
    { "/util/activate-ui/owned/accepted",         FALSE, FALSE, FALSE },
    { "/util/activate-ui/owned/refused",          FALSE, FALSE, TRUE  },
    { "/util/activate-ui/floating/accepted",      FALSE, TRUE,  FALSE },
    { "/util/activate-ui/floating/refused",       FALSE, TRUE,  TRUE  },
    { "/util/activate-ui-sync/owned/accepted",    TRUE,  FALSE, FALSE },
    { "/util/activate-ui-sync/owned/refused",     TRUE,  FALSE, TRUE  },
    { "/util/activate-ui-sync/floating/accepted", TRUE,  TRUE,  FALSE },
    { "/util/activate-ui-sync/floating/refused",  TRUE,  TRUE,  TRUE  },
};

static void
free_argument (gpointer data)
{
    argument_destroyed = TRUE;
    g_free (data);
}

/* A floating string whose last reference going says so. */
static GVariant *
argument_new (void)
{
    gchar *payload = g_strdup ("payload");

    return g_variant_new_from_data (G_VARIANT_TYPE_STRING, payload, sizeof "payload", TRUE, free_argument, payload);
}

static void
test_activation_argument (gconstpointer data)
{
    const ActivationCase *c = data;

    /* The asynchronous flavour warns about a refused proxy, which g_test makes
     * fatal: run it where the warning can be expected rather than abort. */
    if (!c->sync && c->refused && !g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 10 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to get D-Bus proxy: refused*");
        return;
    }
    if (g_test_subprocess ())
        g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    refuse_proxy = c->refused;
    argument_destroyed = FALSE;
    calls = 0;

    GVariant *arg = argument_new ();

    if (!c->floating)
        g_variant_ref_sink (arg);

    if (c->sync)
    {
        g_autoptr (GError) error = NULL;
        gboolean activated = g_paste_util_activate_ui_sync ("show-item", arg, &error);

        g_assert_true (activated == !c->refused);
        g_assert_true ((error != NULL) == c->refused);
    }
    else
    {
        g_paste_util_activate_ui ("show-item", arg);
        /* The caller lets go before the proxy is up: the argument has to be
         * held for the call still to come. */
        if (!c->floating)
            g_clear_pointer (&arg, g_variant_unref);
        g_assert_false (argument_destroyed);

        proxy_callback (NULL, NULL, proxy_data);
    }

    if (arg && !c->floating)
    {
        g_assert_false (argument_destroyed);
        g_variant_unref (arg);
    }
    g_assert_true (argument_destroyed);
    g_assert_cmpuint (calls, ==, !c->refused);
}

/* A pid file reads back as the positive decimal pid written in it, and as -1
 * otherwise: missing -- no failure, and not warned about -- or holding what
 * names no process, a 0 above all, which kill () reads as the caller's own
 * process group. */
static void
test_pid_file (void)
{
    g_autofree gchar *dir = g_strdup_printf ("%s/" PACKAGE_NAME "/PidTest", g_get_user_runtime_dir ());
    g_autofree gchar *path = g_build_filename (dir, "pid", NULL);
    const gchar *damaged[] = { "0", "-1", "abc", "", " 12", "12x" };

    g_assert_cmpint (g_paste_util_read_pid_file ("PidTest"), ==, -1);

    g_paste_util_write_pid_file ("PidTest");
    g_assert_cmpint (g_paste_util_read_pid_file ("PidTest"), ==, getpid ());

    for (guint i = 0; i < G_N_ELEMENTS (damaged); ++i)
    {
        g_assert_true (g_file_set_contents (path, damaged[i], -1, NULL));
        g_assert_cmpint (g_paste_util_read_pid_file ("PidTest"), ==, -1);
    }

    g_assert_cmpint (g_unlink (path), ==, 0);
    g_assert_cmpint (g_rmdir (dir), ==, 0);
}

/* Every line break Pango honours is folded, the Unicode ones included. */
static void
test_one_line (void)
{
    g_autofree gchar *line = g_paste_util_one_line ("a\nb\rc\td\u2028e\u2029f\xc2\x85g");

    g_assert_cmpstr (line, ==, "a b c d e f g");
}

static void
assert_files_shown (const gchar *uris,
                    const gchar *expected)
{
    g_autofree gchar *shown = g_paste_util_display_string (uris, G_PASTE_ITEM_KIND_URIS);

    g_assert_cmpstr (shown, ==, expected);
    g_assert_true (g_utf8_validate (shown, -1, NULL));
}

/* Run in subprocesses, GLib reading $HOME and the filename encoding once:
 * with $HOME spelled without, then with, a trailing slash, then with names
 * set to be in Latin-1. */
static void
test_files_shown (void)
{
    if (!g_test_subprocess ())
    {
        g_auto (GStrv) envp = g_get_environ ();

        for (guint i = 0; i < 3; ++i)
        {
            g_auto (GStrv) child_envp = g_environ_setenv (g_strdupv (envp), "HOME", (i == 1) ? "/home/joe/" : "/home/joe", TRUE);

            child_envp = (i == 2) ? g_environ_setenv (child_envp, "G_FILENAME_ENCODING", "ISO-8859-1", TRUE)
                                  : g_environ_unsetenv (child_envp, "G_FILENAME_ENCODING");
            g_test_trap_subprocess_with_envp (NULL, (const gchar * const *) child_envp, 0, G_TEST_SUBPROCESS_DEFAULT);
            g_test_trap_assert_passed ();
        }
        return;
    }

    gboolean latin1 = !g_strcmp0 (g_getenv ("G_FILENAME_ENCODING"), "ISO-8859-1");

    /* A home file reads from "~", a whole leading component only. */
    assert_files_shown ("file:///home/joe/a", "[Files] ~/a");
    assert_files_shown ("file:///home/joe", "[Files] ~");
    assert_files_shown ("file:///home/joey/a", "[Files] /home/joey/a");
    /* Percent-decoded; one line, a trailing newline hanging nothing off it. */
    assert_files_shown ("file:///tmp/a%20b\nfile:///tmp/c\n", "[Files] /tmp/a b /tmp/c");
    /* Anything but a local file is shown as the uri it is. */
    assert_files_shown ("sftp://host/a%20b", "[Files] sftp://host/a%20b");
    /* With an authority naming this host, read as the same file. */
    assert_files_shown ("file://localhost/home/joe/a", "[Files] ~/a");
    /* A name not in UTF-8 is shown as text all the same: in the encoding names
     * are set to be in, or with what is not text replaced. */
    assert_files_shown ("file:///tmp/caf%E9", (latin1) ? "[Files] /tmp/caf\u00e9" : "[Files] /tmp/caf\uFFFD");
}

/* Each kind wears its decoration, text none, and a kind this library does not
 * know is shown as it arrived. */
static void
test_display_string (void)
{
    static const struct
    {
        GPasteItemKind kind;
        const gchar   *expected;
    } cases[] = {
        { G_PASTE_ITEM_KIND_TEXT,     "value"           },
        { G_PASTE_ITEM_KIND_IMAGE,    "[Image, value]"  },
        { G_PASTE_ITEM_KIND_COLOR,    "[Color] value"   },
        { G_PASTE_ITEM_KIND_PASSWORD, "[Password] value" },
        { (GPasteItemKind) 99,        "value"           },
    };

    for (gsize i = 0; i < G_N_ELEMENTS (cases); ++i)
    {
        g_autofree gchar *shown = g_paste_util_display_string ("value", cases[i].kind);

        g_assert_cmpstr (shown, ==, cases[i].expected);
    }
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/util/pid-file", test_pid_file);
    g_test_add_func ("/util/one-line", test_one_line);
    g_test_add_func ("/util/display-string/files", test_files_shown);
    g_test_add_func ("/util/display-string/kinds", test_display_string);
    for (gsize i = 0; i < G_N_ELEMENTS (activation_cases); ++i)
        g_test_add_data_func (activation_cases[i].path, &activation_cases[i], test_activation_argument);
    return g_paste_test_env_run ();
}
