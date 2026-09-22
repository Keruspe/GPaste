// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-settings.h>
#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-util.h>
#include <gpaste-test-env.h>
#include <signal.h>

static guint signals_sent;
static GError *reexec_error;

/* The re-exec the CLI asks for, refused with whatever @reexec_error says: the
 * method is what the migration gate has to outlive, and reaching a daemon to
 * have it refuse for real would take one. */
static gboolean
fake_reexecute_daemon (GPasteClient *client G_GNUC_UNUSED,
                       GError      **error)
{
    if (!reexec_error)
        return TRUE;

    /* Guarded like the call it stands in for: @error is optional there. */
    if (error)
        *error = g_error_copy (reexec_error);

    return FALSE;
}

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

static gchar *daemon_version;

/* The cached Version property, which is empty until the daemon has answered for
 * it and again once it leaves the bus. Faked rather than read off a real proxy,
 * so the empty case needs no daemon to go missing. */
static gchar *
fake_get_version (GPasteClient *client G_GNUC_UNUSED)
{
    return g_strdup (daemon_version);
}

/* Exercise the CLI's actual fallback while keeping signals inside this test. */
#define main gpaste_client_main
#define kill fake_kill
#define g_paste_util_read_pid_file fake_pid
#define g_paste_util_reexecute_daemon fake_reexecute_daemon
#define g_paste_client_get_version fake_get_version
#include "../../src/client/gpaste-client.c"
#undef g_paste_client_get_version
#undef g_paste_util_reexecute_daemon
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

/* `gpaste-client migrate` opens the migration gate, and what closes it again is
 * *both* routes to a re-exec having failed. A daemon too old for the method is
 * re-exec'd by signal instead, and the successor has to find the gate open: it
 * is the one that runs the migration the user asked for, and this prints a
 * success either way.
 *
 * @user_data says whether that fallback applies. A refusal the daemon made
 * itself has no second route, so the gate is closed and the revision the
 * migration was asked under is back.
 */
static void
test_migrate_gate (gconstpointer user_data)
{
    gboolean unsupported = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    Context ctx = { 0 };
    g_autoptr (GError) error = NULL;

    signals_sent = 0;
    reexec_error = (unsupported) ? g_error_new_literal (G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method")
                                 : g_error_new_literal (G_PASTE_ERROR, G_PASTE_ERROR_FAILED, "Expiry deadline");
    g_paste_settings_set_storage_backend_revision (settings, 37);

    g_assert_cmpint (g_paste_migrate (&ctx, &error), ==, (unsupported) ? EXIT_SUCCESS : EXIT_FAILURE);
    g_assert_cmpuint (signals_sent, ==, unsupported);
    g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, (unsupported) ? 0 : 37);
    if (unsupported)
        g_assert_no_error (error);
    else
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);

    g_clear_error (&reexec_error);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_REVISION_SETTING);
}

/* A daemon that has not answered for its version yet, or that is not on the bus
 * at all, leaves the cached property empty, and the verb has to report that
 * rather than print it: gcc compiles its printf ("%s\n", ...) into a puts (),
 * which unlike printf () does not spell a %NULL out. */
static void
test_daemon_version (gconstpointer user_data)
{
    Context ctx = { 0 };
    g_autoptr (GError) error = NULL;

    daemon_version = (gchar *) user_data;

    g_assert_cmpint (g_paste_daemon_version (&ctx, &error), ==, (daemon_version) ? EXIT_SUCCESS : EXIT_FAILURE);
    if (daemon_version)
        g_assert_no_error (error);
    else
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND);
}

/* Which command lines go near stdin at all. extract_pipe_data () reads it to
 * its end, and a script that leaves its own stdin open never provides one, so a
 * verb answered without a pipe must never be waiting behind one -- the flag
 * actions above all, which are answered before any verb is looked at. */
static void
test_reads_stdin (void)
{
    Context ctx = { 0 };

    /* Nothing but a pipe, and the three verbs whose last argument it stands in
     * for when that argument is missing. */
    g_assert_true (dispatch_reads_stdin (0, NULL, &ctx));
    g_assert_true (dispatch_reads_stdin (1, "add", &ctx));
    g_assert_true (dispatch_reads_stdin (1, "a", &ctx));
    g_assert_true (dispatch_reads_stdin (2, "add-password", &ctx));
    g_assert_true (dispatch_reads_stdin (2, "replace", &ctx));

    /* The argument is there, so there is nothing to take from a pipe. */
    g_assert_false (dispatch_reads_stdin (2, "add", &ctx));
    g_assert_false (dispatch_reads_stdin (3, "add-password", &ctx));
    g_assert_false (dispatch_reads_stdin (3, "replace", &ctx));
    g_assert_false (dispatch_reads_stdin (1, "history", &ctx));
    g_assert_false (dispatch_reads_stdin (1, "version", &ctx));

    /* --help and --version carry no verb, which is the shape the verb-less add
     * has too -- and they are answered before it. */
    ctx.help = TRUE;
    g_assert_false (dispatch_reads_stdin (0, NULL, &ctx));

    ctx.help = FALSE;
    ctx.version = TRUE;
    g_assert_false (dispatch_reads_stdin (0, NULL, &ctx));

    /* Each flag only the listing reads -- listing_flags_given ()'s list --
     * makes a verb-less line the listing, and leaves a verb's own pipe alone,
     * --use-index with replace <index> above all. */
    gboolean *listing_flags[] = { &ctx.favourites, &ctx.oneline, &ctx.raw, &ctx.reverse, &ctx.use_index, &ctx.zero };

    ctx.version = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS (listing_flags); ++i)
    {
        *listing_flags[i] = TRUE;
        g_assert_false (dispatch_reads_stdin (0, NULL, &ctx));
        g_assert_true (dispatch_reads_stdin (1, "add", &ctx));
        g_assert_true (dispatch_reads_stdin (2, "replace", &ctx));
        *listing_flags[i] = FALSE;
    }

    /* A flag the listing does not own leaves a verb-less line the pipe's. */
    ctx.timeout_given = TRUE;
    g_assert_true (dispatch_reads_stdin (0, NULL, &ctx));
    ctx.timeout_given = FALSE;
    ctx.decoration = "-";
    g_assert_true (dispatch_reads_stdin (0, NULL, &ctx));
    ctx.decoration = NULL;
    ctx.separator = ",";
    g_assert_true (dispatch_reads_stdin (0, NULL, &ctx));
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/client/reexec/refusal", test_refusal);
    g_test_add_func ("/client/dispatch/reads_stdin", test_reads_stdin);
    g_test_add_func ("/client/reexec/unsupported", test_unsupported);
    g_test_add_data_func ("/client/daemon_version/answered", "51.1", test_daemon_version);
    g_test_add_data_func ("/client/daemon_version/unanswered", NULL, test_daemon_version);
    g_test_add_data_func ("/client/migrate/gate_survives_fallback", GINT_TO_POINTER (TRUE), test_migrate_gate);
    g_test_add_data_func ("/client/migrate/gate_closed_on_refusal", GINT_TO_POINTER (FALSE), test_migrate_gate);
    return g_paste_test_env_run ();
}
