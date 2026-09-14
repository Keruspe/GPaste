// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>
#include <gpaste-test-env.h>
#include <signal.h>

static guint signals_sent;

static GPid
fake_pid (const gchar *name G_GNUC_UNUSED)
{
    return 123;
}

static gint
fake_kill (GPid pid,
           gint signal_number)
{
    g_assert_cmpint (pid, ==, 123);
    g_assert_cmpint (signal_number, ==, SIGUSR1);
    ++signals_sent;
    return 0;
}

/* Exercise the CLI's actual fallback while keeping signals inside this test. */
#define main gpaste_client_main
#define kill fake_kill
#define g_paste_util_read_pid_file fake_pid
#include "../../src/client/gpaste-client.c"
#undef g_paste_util_read_pid_file
#undef kill
#undef main

static void
test_refusal (void)
{
    g_autoptr (GError) error = g_error_new_literal (G_PASTE_ERROR, G_PASTE_ERROR_FAILED, "Expiry deadline");

    signals_sent = 0;
    g_assert_false (reexec_fallback (FALSE, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_cmpuint (signals_sent, ==, 0);
}

static void
test_unsupported (void)
{
    g_autoptr (GError) error = g_error_new_literal (G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");

    signals_sent = 0;
    g_assert_true (reexec_fallback (FALSE, &error));
    g_assert_no_error (error);
    g_assert_cmpuint (signals_sent, ==, 1);
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/client/reexec/refusal", test_refusal);
    g_test_add_func ("/client/reexec/unsupported", test_unsupported);
    return g_paste_test_env_run ();
}
