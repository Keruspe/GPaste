// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <gpaste-3/gpaste-util.h>

static GAsyncReadyCallback bus_callback;
static gpointer bus_data;
static gboolean refuse_bus;
static gboolean argument_destroyed;
static guint calls;
/* The activation token the platform data has to carry, or NULL for none. */
static const gchar *expected_token;

static void
fake_bus_get (GBusType            bus_type G_GNUC_UNUSED,
              GCancellable       *cancellable G_GNUC_UNUSED,
              GAsyncReadyCallback callback,
              gpointer            data)
{
    bus_callback = callback;
    bus_data = data;
}

/* Never a connection the code under test does anything with but hand to the
 * fake calls below and drop. */
static GDBusConnection *
fake_bus_get_finish (GAsyncResult *result G_GNUC_UNUSED,
                     GError      **error)
{
    if (refuse_bus)
    {
        g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "refused");
        return NULL;
    }

    return (GDBusConnection *) g_object_new (G_TYPE_OBJECT, NULL);
}

static GDBusConnection *
fake_bus_get_sync (GBusType      bus_type G_GNUC_UNUSED,
                   GCancellable *cancellable G_GNUC_UNUSED,
                   GError      **error)
{
    return fake_bus_get_finish (NULL, error);
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

    g_autoptr (GVariant) platform_data = g_variant_get_child_value (params, 2);
    g_autoptr (GVariantDict) dict = g_variant_dict_new (platform_data);
    const gchar *token = NULL;

    g_variant_dict_lookup (dict, "activation-token", "&s", &token);
    g_assert_cmpstr (token, ==, expected_token);
    ++calls;
}

static void
fake_connection_call (GDBusConnection    *connection G_GNUC_UNUSED,
                      const gchar        *name G_GNUC_UNUSED,
                      const gchar        *path G_GNUC_UNUSED,
                      const gchar        *interface G_GNUC_UNUSED,
                      const gchar        *method,
                      GVariant           *parameters,
                      const GVariantType *reply_type G_GNUC_UNUSED,
                      GDBusCallFlags      flags G_GNUC_UNUSED,
                      gint                timeout G_GNUC_UNUSED,
                      GCancellable       *cancellable G_GNUC_UNUSED,
                      GAsyncReadyCallback callback G_GNUC_UNUSED,
                      gpointer            data)
{
    check_parameters (method, parameters);
    /* The name the reply's warning would give, never answered here. */
    g_free (data);
}

static GVariant *
fake_connection_call_sync (GDBusConnection    *connection G_GNUC_UNUSED,
                           const gchar        *name G_GNUC_UNUSED,
                           const gchar        *path G_GNUC_UNUSED,
                           const gchar        *interface G_GNUC_UNUSED,
                           const gchar        *method,
                           GVariant           *parameters,
                           const GVariantType *reply_type G_GNUC_UNUSED,
                           GDBusCallFlags      flags G_GNUC_UNUSED,
                           gint                timeout G_GNUC_UNUSED,
                           GCancellable       *cancellable G_GNUC_UNUSED,
                           GError            **error G_GNUC_UNUSED)
{
    check_parameters (method, parameters);

    return g_variant_ref_sink (g_variant_new ("()"));
}

/* A reset GSettings refuses, as it does a key an administrator locked. */
static gboolean refuse_reset;

static void
fake_settings_reset (GPasteSettings *settings,
                     const gchar    *key)
{
    if (!refuse_reset)
        g_paste_settings_reset (settings, key);
}

#define g_paste_settings_reset fake_settings_reset
#define g_bus_get fake_bus_get
#define g_bus_get_finish fake_bus_get_finish
#define g_bus_get_sync fake_bus_get_sync
#define g_dbus_connection_call fake_connection_call
#define g_dbus_connection_call_sync fake_connection_call_sync
#include "../../src/libgpaste/gpaste-3/gpaste-util.c"
#undef g_dbus_connection_call_sync
#undef g_dbus_connection_call
#undef g_bus_get_sync
#undef g_bus_get_finish
#undef g_bus_get
#undef g_paste_settings_reset

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

    /* The asynchronous flavour warns about a bus it could not reach, which
     * g_test makes fatal: run it where the warning can be expected rather than
     * abort. */
    if (!c->sync && c->refused && !g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 10 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*Failed to reach the session bus: refused*");
        return;
    }
    if (g_test_subprocess ())
        g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    refuse_bus = c->refused;
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
        /* The caller lets go before the bus is reached: the argument has to be
         * held for the call still to come. */
        if (!c->floating)
            g_clear_pointer (&arg, g_variant_unref);
        g_assert_false (argument_destroyed);

        bus_callback (NULL, NULL, bus_data);
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

/* A line is shown up to as many characters as asked, an ellipsis after what is
 * cut, a shorter one whole and every one for 0 -- GNOME Shell's element-size
 * reading 0 as no limit; folded either way. Characters, not bytes: a
 * multi-byte one is kept whole or not at all. */
static void
test_shown_line (void)
{
    g_autofree gchar *head = g_strnfill (G_PASTE_UTIL_SHOWN_CHARS - 1, 'a');
    g_autofree gchar *text = g_strconcat (head, "\u2028", "tail", NULL);
    g_autofree gchar *line = g_paste_util_shown_line (text, G_PASTE_UTIL_SHOWN_CHARS);
    g_autofree gchar *expected = g_strconcat (head, " …", NULL);
    g_autofree gchar *whole = g_paste_util_shown_line ("a\nb", G_PASTE_UTIL_SHOWN_CHARS);
    g_autofree gchar *exact = g_paste_util_shown_line ("a\nb", 3);
    g_autofree gchar *unbounded = g_paste_util_shown_line (text, 0);
    g_autofree gchar *wide = g_paste_util_shown_line ("😀😀😀", 2);

    g_assert_cmpstr (line, ==, expected);
    g_assert_cmpuint (g_utf8_strlen (line, -1), ==, G_PASTE_UTIL_SHOWN_CHARS + 1);
    g_assert_cmpstr (whole, ==, "a b");
    g_assert_cmpstr (exact, ==, "a b");
    g_assert_cmpuint (g_utf8_strlen (unbounded, -1), ==, G_PASTE_UTIL_SHOWN_CHARS + 4);
    g_assert_true (g_str_has_suffix (unbounded, " tail"));
    g_assert_cmpstr (wide, ==, "😀😀…");

    /* A sequence cut short, more bytes past the NUL after it: a text that is
     * not UTF-8 is walked no further than its end. */
    static const gchar cut_short[] = "ab\xE2\0XYZ";
    g_autofree gchar *walked = g_paste_util_shown_line (cut_short, 3);

    g_assert_cmpstr (walked, ==, "ab\xE2");
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

/* The migration gate is read back once opened: a reset GSettings refused is
 * no gate open, and says so, rather than a migration asked for that the
 * daemon's successor would find nothing to do for. */
static void
test_migration_gate_refused (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GError) error = NULL;
    guint64 revision = 0;

    g_paste_settings_set_storage_backend_revision (settings, 37);
    refuse_reset = TRUE;
    g_assert_false (g_paste_util_open_storage_migration (&revision, &error));
    refuse_reset = FALSE;
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_clear_error (&error);

    g_assert_true (g_paste_util_open_storage_migration (&revision, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (revision, ==, 37);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_REVISION_SETTING);
}

/* A launcher's Wayland activation token reaches the graphical tool, as GLib's
 * own gapplication tool hands it on. */
static void
test_activation_token (void)
{
    g_autoptr (GError) error = NULL;

    g_setenv ("XDG_ACTIVATION_TOKEN", "token", TRUE);
    expected_token = "token";
    calls = 0;
    refuse_bus = FALSE;
    g_assert_true (g_paste_util_activate_ui_sync ("action", argument_new (), &error));
    g_assert_no_error (error);
    g_assert_cmpuint (calls, ==, 1);
    expected_token = NULL;
    g_unsetenv ("XDG_ACTIVATION_TOKEN");
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/util/pid-file", test_pid_file);
    g_test_add_func ("/util/one-line", test_one_line);
    g_test_add_func ("/util/shown-line", test_shown_line);
    g_test_add_func ("/util/activation-token", test_activation_token);
    g_test_add_func ("/util/migration-gate/refused", test_migration_gate_refused);
    g_test_add_func ("/util/display-string/files", test_files_shown);
    g_test_add_func ("/util/display-string/kinds", test_display_string);
    for (gsize i = 0; i < G_N_ELEMENTS (activation_cases); ++i)
        g_test_add_data_func (activation_cases[i].path, &activation_cases[i], test_activation_argument);
    return g_paste_test_env_run ();
}
