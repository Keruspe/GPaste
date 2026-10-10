// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-error.h>
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

static gchar *uploaded_and_copied;

/* What `upload-and-copy` has the daemon do, recorded rather than sent: the
 * verb is a thin call, and what matters is that it is UploadAndCopy it makes,
 * not Upload, and with the item it was given. */
static gchar *
fake_upload_and_copy_sync (GPasteClient *client G_GNUC_UNUSED,
                           const gchar  *uuid,
                           GError      **error G_GNUC_UNUSED)
{
    g_set_str (&uploaded_and_copied, uuid);

    return g_strdup ("https://paste.rs/x");
}

/* The uuid of the faked daemon's item @n, a hex digit. */
#define FAKE_UUID(n) "00000000-0000-4000-8000-00000000000" #n

/* The history the faked daemon answers --use-index lookups from, newest
 * first, and the item a copy puts at its head right after each answer, when
 * @copied_after_answer names one -- the interleaving one command line's
 * lookups have to be safe from. */
static GPtrArray *fake_history;
static const gchar *copied_after_answer;
static guint clients_created;
static guint index_lookups;
static guint history_reads;
static gchar *deleted_uuid;
static GStrv merged_uuids;
static gchar *added_text;
static gchar *search_pattern;

static void
set_fake_history (const gchar * const *uuids)
{
    g_clear_pointer (&fake_history, g_ptr_array_unref);
    fake_history = g_ptr_array_new_with_free_func (g_free);
    for (const gchar * const *uuid = uuids; *uuid; ++uuid)
        g_ptr_array_add (fake_history, g_strdup (*uuid));

    copied_after_answer = NULL;
    index_lookups = 0;
    history_reads = 0;
}

static void
fake_history_answered (void)
{
    if (copied_after_answer)
        g_ptr_array_insert (fake_history, 0, g_strdup (copied_after_answer));
}

static GPasteClientItem *
fake_item (guint index)
{
    return g_paste_client_item_new (g_ptr_array_index (fake_history, index), "value", G_PASTE_ITEM_KIND_TEXT, FALSE, NULL);
}

static GPasteClient *
fake_client_new_sync (GError **error G_GNUC_UNUSED)
{
    ++clients_created;
    return g_object_new (G_PASTE_TYPE_CLIENT, NULL);
}

static GPasteClientItem *
fake_get_item_at_index_sync (GPasteClient *client G_GNUC_UNUSED,
                             guint64       index,
                             GError      **error)
{
    ++index_lookups;

    if (index >= fake_history->len)
    {
        g_set_error_literal (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_INDEX, "invalid index received");
        return NULL;
    }

    GPasteClientItem *item = fake_item (index);

    fake_history_answered ();
    return item;
}

static GList *
fake_get_history_sync (GPasteClient *client G_GNUC_UNUSED,
                       GError      **error G_GNUC_UNUSED)
{
    GList *history = NULL;

    ++history_reads;
    for (guint i = fake_history->len; i > 0; --i)
        history = g_list_prepend (history, fake_item (i - 1));

    fake_history_answered ();
    return history;
}

static void
fake_delete_item_sync (GPasteClient *client G_GNUC_UNUSED,
                       const gchar  *uuid,
                       GError      **error G_GNUC_UNUSED)
{
    g_set_str (&deleted_uuid, uuid);
}

static gchar *
fake_merge_sync (GPasteClient        *client G_GNUC_UNUSED,
                 const gchar         *decoration G_GNUC_UNUSED,
                 const gchar         *separator G_GNUC_UNUSED,
                 const gchar * const *uuids,
                 GError             **error G_GNUC_UNUSED)
{
    g_set_strv (&merged_uuids, uuids);
    return g_strdup (FAKE_UUID (f));
}

static gchar *
fake_add_text_sync (GPasteClient *client G_GNUC_UNUSED,
                    const gchar  *text,
                    GError      **error G_GNUC_UNUSED)
{
    g_set_str (&added_text, text);
    return g_strdup (FAKE_UUID (f));
}

static GList *
fake_search_sync (GPasteClient *client G_GNUC_UNUSED,
                  const gchar  *pattern,
                  GError      **error G_GNUC_UNUSED)
{
    g_set_str (&search_pattern, pattern);
    return NULL;
}

/* Exercise the CLI's actual fallback while keeping signals inside this test. */
#define main gpaste_client_main
#define kill fake_kill
#define g_paste_util_read_pid_file fake_pid
#define g_paste_util_reexecute_daemon fake_reexecute_daemon
#define g_paste_client_get_version fake_get_version
#define g_paste_client_upload_and_copy_sync fake_upload_and_copy_sync
#define g_paste_client_new_sync fake_client_new_sync
#define g_paste_client_get_item_at_index_sync fake_get_item_at_index_sync
#define g_paste_client_get_history_sync fake_get_history_sync
#define g_paste_client_delete_item_sync fake_delete_item_sync
#define g_paste_client_merge_sync fake_merge_sync
#define g_paste_client_add_text_sync fake_add_text_sync
#define g_paste_client_search_sync fake_search_sync
#include "../../src/client/gpaste-client.c"
#undef g_paste_client_search_sync
#undef g_paste_client_add_text_sync
#undef g_paste_client_merge_sync
#undef g_paste_client_delete_item_sync
#undef g_paste_client_get_history_sync
#undef g_paste_client_get_item_at_index_sync
#undef g_paste_client_new_sync
#undef g_paste_client_upload_and_copy_sync
#undef g_paste_client_get_version
#undef g_paste_util_reexecute_daemon
#undef g_paste_util_read_pid_file
#undef kill
#undef main

/* `gpaste-client` @args as a shell would run it, getopt () rewound first: it
 * keeps its place across calls, and every case parses a command line anew. */
static gint
run_client (const gchar * const *args)
{
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

    g_strv_builder_add (builder, "gpaste-client");
    g_strv_builder_addv (builder, (const gchar **) args);

    g_auto (GStrv) argv = g_strv_builder_end (builder);

    optind = 0;
    return gpaste_client_main ((gint) g_strv_length (argv), argv);
}

/* An index names a place in the history, and a single one is looked up there
 * on its own. */
static void
test_item_index (void)
{
    set_fake_history ((const gchar *[]) { FAKE_UUID (a), FAKE_UUID (b), FAKE_UUID (c), NULL });

    g_assert_cmpint (run_client ((const gchar *[]) { "--use-index", "delete", "1", NULL }), ==, EXIT_SUCCESS);
    g_assert_cmpstr (deleted_uuid, ==, FAKE_UUID (b));
    g_assert_cmpuint (index_lookups, ==, 1);
    g_assert_cmpuint (history_reads, ==, 0);
    g_clear_pointer (&deleted_uuid, g_free);
}

/* Anything but a complete unsigned decimal number names no item, and is
 * refused before anything is looked up -- a vertical tab in front included,
 * which g_ascii_string_to_unsigned () alone lets through -- and before a
 * client is built: refusing it needs no daemon started. */
static void
test_malformed_index (void)
{
    const gchar *malformed[] = { "abc", "1x", "", "-1", "+1", " 1", "1 ", "0x1", "\v1", "\v-1", "18446744073709551616" };

    set_fake_history ((const gchar *[]) { FAKE_UUID (a), FAKE_UUID (b), FAKE_UUID (c), NULL });
    clients_created = 0;

    for (guint i = 0; i < G_N_ELEMENTS (malformed); ++i)
    {
        Context ctx = {
            .argc = 1,
            .args = (const gchar *[]) { malformed[i], NULL },
            .use_index = TRUE,
        };
        g_autoptr (GError) error = NULL;

        g_assert_cmpint (g_paste_dispatch (2, "delete", &ctx, &error), ==, EXIT_FAILURE);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
        g_assert_null (deleted_uuid);
        g_assert_cmpuint (clients_created, ==, 0);
    }

    /* A merge's indexes are all checked before its one listing is read. */
    Context ctx = {
        .argc = 2,
        .args = (const gchar *[]) { "0", "abc", NULL },
        .use_index = TRUE,
    };
    g_autoptr (GError) error = NULL;

    g_assert_cmpint (g_paste_dispatch (3, "merge", &ctx, &error), ==, EXIT_FAILURE);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
    g_assert_null (merged_uuids);
    g_assert_cmpuint (index_lookups, ==, 0);
    g_assert_cmpuint (history_reads, ==, 0);
    g_assert_cmpuint (clients_created, ==, 0);
}

/* A command that never talks to the daemon builds no client, which is what has
 * D-Bus start one -- and neither does a verb nobody knows, answered with the
 * help. */
static void
test_daemonless_commands (void)
{
    const gchar *lines[][2] = { { "version", NULL }, { "help", NULL }, { "--version", NULL }, { "--help", NULL } };

    clients_created = 0;

    for (guint i = 0; i < G_N_ELEMENTS (lines); ++i)
        g_assert_cmpint (run_client (lines[i]), ==, EXIT_SUCCESS);

    g_assert_cmpint (run_client ((const gchar *[]) { "no-such-verb", NULL }), ==, EXIT_FAILURE);
    g_assert_cmpuint (clients_created, ==, 0);
}

/* Every argument of merge names an item, and all of them are read off the same
 * listing: the copy landing after the first answer would otherwise have the
 * second index name the first item again. */
static void
test_merge_indexes (void)
{
    set_fake_history ((const gchar *[]) { FAKE_UUID (a), FAKE_UUID (b), FAKE_UUID (c), NULL });
    copied_after_answer = FAKE_UUID (d);

    g_assert_cmpint (run_client ((const gchar *[]) { "--use-index", "merge", "0", "1", NULL }), ==, EXIT_SUCCESS);
    g_assert_cmpstrv (merged_uuids, ((const gchar *[]) { FAKE_UUID (a), FAKE_UUID (b), NULL }));
    g_assert_cmpuint (history_reads, ==, 1);
    g_assert_cmpuint (index_lookups, ==, 0);
    g_clear_pointer (&merged_uuids, g_strfreev);
}

/* Past the end of the history, several indexes are refused the way a single
 * one is: nothing is merged. */
static void
test_merge_past_end (void)
{
    g_autoptr (GPasteClient) client = fake_client_new_sync (NULL);
    Context ctx = {
        .client = client,
        .argc = 2,
        .args = (const gchar *[]) { "0", "3", NULL },
        .use_index = TRUE,
    };
    g_autoptr (GError) error = NULL;

    set_fake_history ((const gchar *[]) { FAKE_UUID (a), FAKE_UUID (b), FAKE_UUID (c), NULL });

    g_assert_cmpint (g_paste_dispatch (3, "merge", &ctx, &error), ==, EXIT_FAILURE);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_INDEX);
    g_assert_null (merged_uuids);
}

/* Text, a pattern, a name or a path is no index, and an empty history has no
 * item to look any of them up as. */
static void
test_nonitem_arguments (void)
{
    set_fake_history ((const gchar *[]) { NULL });

    g_assert_cmpint (run_client ((const gchar *[]) { "--use-index", "add", "hello", NULL }), ==, EXIT_SUCCESS);
    g_assert_cmpint (run_client ((const gchar *[]) { "--use-index", "search", "hello", NULL }), ==, EXIT_SUCCESS);
    g_assert_cmpstr (added_text, ==, "hello");
    g_assert_cmpstr (search_pattern, ==, "hello");
    g_assert_cmpuint (index_lookups, ==, 0);
    g_assert_cmpuint (history_reads, ==, 0);
    g_clear_pointer (&added_text, g_free);
    g_clear_pointer (&search_pattern, g_free);
}

/* Every string a command could send the daemon has to be UTF-8, and is checked
 * before a client is even built: each argument of each verb that talks to the
 * daemon, the text piped in for one, and merge's decoration and separator. */
static void
test_utf8_refused (void)
{
    gchar invalid[] = { (gchar) 0xff, '\0' };
    Context piped = { .pipe_data = invalid };
    Context decorated = { .argc = 2, .args = (const gchar *[]) { "0", "1", NULL }, .decoration = invalid };
    Context separated = { .argc = 2, .args = (const gchar *[]) { "0", "1", NULL }, .separator = invalid };

    clients_created = 0;

    for (guint64 i = 0; i < G_N_ELEMENTS (commands); ++i)
    {
        const Command *c = &commands[i];
        gint count = c->argc - 1 + ((c->extra_args == G_MAXINT) ? 0 : c->extra_args);

        if (!c->verb || !c->needs_client)
            continue;

        /* Each argument in turn, the others valid, at the most arguments the
         * verb takes, and at the fewest with a pipe standing in for the last. */
        for (gint position = 0; position < count; ++position)
        {
            g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

            for (gint j = 0; j < count; ++j)
                g_strv_builder_add (builder, (j == position) ? invalid : "0");

            g_auto (GStrv) args = g_strv_builder_end (builder);
            Context ctx = { .argc = count, .args = (const gchar **) args };
            g_autoptr (GError) error = NULL;

            g_assert_cmpint (g_paste_dispatch (count + 1, c->verb, &ctx, &error), ==, EXIT_FAILURE);
            g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
        }

        if (c->reads_pipe)
        {
            Context ctx = { .argc = c->argc - 1, .args = (const gchar *[]) { "0", NULL }, .pipe_data = invalid };
            g_autoptr (GError) error = NULL;

            g_assert_cmpint (g_paste_dispatch (c->argc, c->verb, &ctx, &error), ==, EXIT_FAILURE);
            g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
        }
    }

    g_autoptr (GError) error = NULL;

    g_assert_cmpint (g_paste_dispatch (0, NULL, &piped, &error), ==, EXIT_FAILURE);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_assert_cmpint (g_paste_dispatch (3, "merge", &decorated, &error), ==, EXIT_FAILURE);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_assert_cmpint (g_paste_dispatch (3, "merge", &separated, &error), ==, EXIT_FAILURE);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);

    g_assert_cmpuint (clients_created, ==, 0);
}

/* What is UTF-8 goes through, from an argument or a pipe alike. */
static void
test_utf8_accepted (void)
{
    Context piped = { .pipe_data = (gchar *) "pipé" };
    g_autoptr (GError) error = NULL;

    g_assert_cmpint (run_client ((const gchar *[]) { "add", "héllo", NULL }), ==, EXIT_SUCCESS);
    g_assert_cmpstr (added_text, ==, "héllo");

    g_assert_cmpint (g_paste_dispatch (0, NULL, &piped, &error), ==, EXIT_SUCCESS);
    g_assert_no_error (error);
    g_assert_cmpstr (added_text, ==, "pipé");
    g_clear_object (&piped.client);
    g_clear_pointer (&added_text, g_free);
}

static void
test_upload_and_copy (void)
{
    Context ctx = { .argc = 1, .args = (const gchar *[]) { "item", NULL } };
    g_autoptr (GError) error = NULL;

    g_assert_cmpint (g_paste_upload_and_copy (&ctx, &error), ==, EXIT_SUCCESS);
    g_assert_no_error (error);
    g_assert_cmpstr (uploaded_and_copied, ==, "item");
    g_clear_pointer (&uploaded_and_copied, g_free);
}

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
    g_test_add_func ("/client/upload-and-copy", test_upload_and_copy);
    g_test_add_func ("/client/dispatch/reads_stdin", test_reads_stdin);
    g_test_add_func ("/client/dispatch/daemonless", test_daemonless_commands);
    g_test_add_func ("/client/utf8/refused", test_utf8_refused);
    g_test_add_func ("/client/utf8/accepted", test_utf8_accepted);
    g_test_add_func ("/client/index/item", test_item_index);
    g_test_add_func ("/client/index/malformed", test_malformed_index);
    g_test_add_func ("/client/index/merge", test_merge_indexes);
    g_test_add_func ("/client/index/merge-past-end", test_merge_past_end);
    g_test_add_func ("/client/index/nonitem-arguments", test_nonitem_arguments);
    g_test_add_func ("/client/reexec/unsupported", test_unsupported);
    g_test_add_data_func ("/client/daemon_version/answered", "51.1", test_daemon_version);
    g_test_add_data_func ("/client/daemon_version/unanswered", NULL, test_daemon_version);
    g_test_add_data_func ("/client/migrate/gate_survives_fallback", GINT_TO_POINTER (TRUE), test_migrate_gate);
    g_test_add_data_func ("/client/migrate/gate_closed_on_refusal", GINT_TO_POINTER (FALSE), test_migrate_gate);
    return g_paste_test_env_run ();
}
