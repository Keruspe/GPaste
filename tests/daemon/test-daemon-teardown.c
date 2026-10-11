// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-test-clipboard-provider.h"

#include <gpaste-test-env.h>
#include <gpaste-test-bus.h>
#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-global-shortcut-client.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/file.h>

/* Every write of extension-paused: GSettings raises no change for a value it
 * already holds, so this is the one place a write of the same value shows. */
static guint extension_paused_writes;

gboolean
g_settings_set_boolean (GSettings   *settings,
                        const gchar *key,
                        gboolean     value)
{
    typedef gboolean (*SetBoolean) (GSettings *, const gchar *, gboolean);
    SetBoolean original = (SetBoolean) dlsym (RTLD_NEXT, "g_settings_set_boolean");

    g_assert_nonnull (original);
    if (g_str_equal (key, G_PASTE_EXTENSION_PAUSED_SETTING))
        ++extension_paused_writes;
    return original (settings, key, value);
}

/* The private bus has no GlobalShortcuts portal, and a daemon that cannot reach
 * it warns, which g_test makes fatal. Its constructor is never answered here,
 * the reference on the daemon it is handed dropped at once. */
static void
no_portal (GAsyncReadyCallback callback G_GNUC_UNUSED,
           gpointer            user_data)
{
    g_object_unref (user_data);
}

/* The expiries a re-exec asked for, and those that have answered. */
static guint expiries;
static guint expired;

typedef struct
{
    GAsyncReadyCallback callback;
    gpointer            user_data;
} Expiry;

static void
on_counted_expiry (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
    g_autofree Expiry *expiry = user_data;

    ++expired;
    expiry->callback (source_object, result, expiry->user_data);
}

/* Every expiry a re-exec asks for, before handing it on. */
static void
counting_expiry (GPasteClipboardsManager *self,
                 GAsyncReadyCallback      callback,
                 gpointer                 user_data)
{
    Expiry *expiry = g_new (Expiry, 1);

    *expiry = (Expiry) { callback, user_data };
    ++expiries;
    g_paste_clipboards_manager_expire_password_async (self, on_counted_expiry, expiry);
}

/* Whether a storage worker is rewriting the store, as a case has it. */
static gboolean storage_busy;

static gboolean
test_storage_is_busy (void)
{
    return storage_busy;
}

#define g_paste_global_shortcut_client_new no_portal
#define g_paste_clipboards_manager_expire_password_async counting_expiry
#define g_paste_storage_is_busy test_storage_is_busy
#include <gpaste-daemon/gpaste-daemon.c>
#undef g_paste_storage_is_busy
#undef g_paste_clipboards_manager_expire_password_async
#undef g_paste_global_shortcut_client_new

typedef struct
{
    GPasteSettings  *settings;
    TestClipboard   *clipboard;
    TestClipboard   *primary;
    GPasteDaemon    *daemon;
    GDBusConnection *connection;
    guint            reexecutions;
    guint            stored_at_reexecution;
    guint            expired_at_reexecution;
} Fixture;

static gboolean
history_loaded (gconstpointer data,
                gconstpointer arg G_GNUC_UNUSED)
{
    return !g_paste_history_is_loading (((GPasteDaemon *) data)->history);
}

/* A daemon exported on the private bus, its history loaded and its screensaver
 * client built: that client's constructor holds a reference on the daemon until
 * it answers, and its answer expects a daemon still up, so a case taking the
 * daemon down waits for it first. */
static void
fixture_set_up (Fixture      *fixture,
                gconstpointer user_data G_GNUC_UNUSED)
{
    fixture->settings = g_paste_settings_new ();
    g_paste_settings_set_history_name (fixture->settings, "daemon-teardown");
    g_paste_settings_set_storage_backend (fixture->settings, G_PASTE_STORAGE_NOOP);
    fixture->clipboard = test_clipboard_new ();
    fixture->primary = test_clipboard_new ();
    fixture->daemon = g_paste_daemon_new (fixture->settings,
                                          G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard),
                                          G_PASTE_CLIPBOARD_PROVIDER (fixture->primary));
    fixture->connection = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));

    g_paste_test_bus_wait_until (history_loaded, fixture->daemon, NULL);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &fixture->daemon->screensaver, NULL);
    /* Both waits give up quietly: a case run on a daemon in neither state
     * would test a teardown the real one never gets to. */
    g_assert_false (g_paste_history_is_loading (fixture->daemon->history));
    g_assert_nonnull (fixture->daemon->screensaver);
    g_assert_true (g_paste_bus_object_register_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon), fixture->connection, NULL));
}

static void
fixture_tear_down (Fixture      *fixture,
                   gconstpointer user_data G_GNUC_UNUSED)
{
    g_clear_object (&fixture->connection);
    g_clear_object (&fixture->daemon);
    g_clear_object (&fixture->primary);
    g_clear_object (&fixture->clipboard);
    g_clear_object (&fixture->settings);
}

/* The item shortcuts change nothing while the store is handed over, as every
 * method changing an item answers busy then: the change would be recorded
 * nowhere, and the reload ending the handover would undo it. */
static void
test_shortcuts_handed_over (Fixture      *fixture,
                            gconstpointer user_data G_GNUC_UNUSED)
{
    GPasteHistory *history = fixture->daemon->history;

    g_paste_history_add (history, g_paste_text_item_new ("kept"));
    g_paste_history_flush (history);
    keybinding_pop (NULL, history);
    keybinding_make_password (NULL, fixture->daemon);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_true (G_PASTE_IS_TEXT_ITEM (g_paste_history_get (history, 0)));
    g_paste_history_resume (history);
}

/* Disposing unregisters, so neither a second dispose nor the unregistration
 * GPasteBus makes when it lets go of the name finds an export to drop. */
static void
test_dispose_twice (Fixture      *fixture,
                    gconstpointer user_data G_GNUC_UNUSED)
{
    g_object_run_dispose (G_OBJECT (fixture->daemon));
    g_assert_null (fixture->daemon->skeleton);
    g_object_run_dispose (G_OBJECT (fixture->daemon));
    g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon));
}

/* The startup update, looked up through the field the daemon tracks it in. */
static GSource *
startup_update (GPasteDaemon *daemon)
{
    g_assert_cmpuint (daemon->startup_changed_id, !=, 0);

    return g_main_context_find_source_by_id (NULL, daemon->startup_changed_id);
}

/* Unregistering cancels the startup update. Asked of the source rather than
 * through a dispose: disposing clears the daemon's weak references first, and
 * a source left running would then find no daemon and do nothing, which looks
 * the same as a source that is gone. */
static void
test_unregister_cancels_startup (Fixture      *fixture,
                                 gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GSource) startup = g_source_ref (startup_update (fixture->daemon));

    g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon));
    g_assert_true (g_source_is_destroyed (startup));
}

/* The startup update holds the daemon weakly: a daemon let go of while the
 * update is pending goes then, not once the update has run. */
static void
test_release_pending_startup (Fixture      *fixture,
                              gconstpointer user_data G_GNUC_UNUSED)
{
    GPasteDaemon *daemon = fixture->daemon;

    g_assert_nonnull (startup_update (daemon));
    g_object_add_weak_pointer (G_OBJECT (daemon), (gpointer *) &daemon);
    g_clear_object (&fixture->daemon);
    g_assert_null (daemon);
}

static void
on_update (GDBusConnection *connection G_GNUC_UNUSED,
           const gchar     *sender G_GNUC_UNUSED,
           const gchar     *path G_GNUC_UNUSED,
           const gchar     *interface G_GNUC_UNUSED,
           const gchar     *signal G_GNUC_UNUSED,
           GVariant        *parameters G_GNUC_UNUSED,
           gpointer         user_data)
{
    guint *updates = user_data;

    ++*updates;
}

/* The startup update reaches the export's clients once it runs, and leaves
 * unregistering nothing of its own to cancel. */
static void
test_startup_update (Fixture      *fixture,
                     gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GDBusConnection) listener = g_paste_test_bus_connect (g_getenv ("DBUS_SESSION_BUS_ADDRESS"));
    guint updates = 0;
    guint subscription = g_dbus_connection_signal_subscribe (listener, NULL, G_PASTE_DAEMON_INTERFACE_NAME, "Update",
                                                             G_PASTE_DAEMON_OBJECT_PATH, NULL, G_DBUS_SIGNAL_FLAGS_NONE,
                                                             on_update, &updates, NULL);

    /* Looked up before the round trip, which runs whatever is due: a setup that
     * took over a second would otherwise have the update fire there and leave
     * nothing to look up. */
    g_autoptr (GSource) startup = g_source_ref (startup_update (fixture->daemon));

    g_paste_test_bus_round_trip (listener, fixture->connection);
    if (!g_source_is_destroyed (startup))
        g_source_set_ready_time (startup, 0);
    g_paste_test_bus_wait_for_count (&updates, 1);
    g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon));
    g_dbus_connection_signal_unsubscribe (listener, subscription);
}

static gboolean
on_reexecute_self (GPasteDaemon *daemon G_GNUC_UNUSED,
                   gpointer      user_data)
{
    Fixture *fixture = user_data;

    fixture->stored_at_reexecution = fixture->clipboard->stores + fixture->primary->stores;
    fixture->expired_at_reexecution = expired;
    ++fixture->reexecutions;
    return TRUE;
}

/* A re-exec has the passwords taken off and both selections stored before the
 * host is told to go: the expiry has answered by then, not merely been asked
 * for. */
static void
test_reexecute_stores_first (Fixture      *fixture,
                             gconstpointer user_data G_GNUC_UNUSED)
{
    expiries = 0;
    expired = 0;
    g_signal_connect (fixture->daemon, "reexecute-self", G_CALLBACK (on_reexecute_self), fixture);
    g_paste_daemon_reexecute (fixture->daemon);
    g_paste_test_bus_wait_for_count (&fixture->reexecutions, 1);
    g_assert_cmpuint (expiries, ==, 1);
    g_assert_cmpuint (fixture->clipboard->stores, ==, 1);
    g_assert_cmpuint (fixture->primary->stores, ==, 1);
    g_assert_cmpuint (fixture->stored_at_reexecution, ==, 2);
    g_assert_cmpuint (fixture->expired_at_reexecution, ==, 1);
}

/* The daemon g_paste_daemon_new_meta () builds stays up through a re-exec, so
 * no password's countdown is brought forward for one. */
static void
test_reexecute_in_process (Fixture      *fixture,
                           gconstpointer user_data G_GNUC_UNUSED)
{
    expiries = 0;
    fixture->daemon->in_process = TRUE;
    g_signal_connect (fixture->daemon, "reexecute-self", G_CALLBACK (on_reexecute_self), fixture);
    g_paste_daemon_reexecute (fixture->daemon);
    g_paste_test_bus_wait_for_count (&fixture->reexecutions, 1);
    g_assert_cmpuint (expiries, ==, 0);
}

static void
on_uploaded (GObject      *source_object G_GNUC_UNUSED,
             GAsyncResult *result,
             gpointer      user_data)
{
    *(GAsyncResult **) user_data = g_object_ref (result);
}

/* @uuid uploaded through @command, waited for: the url, or %NULL and @error. */
static gchar *
run_upload (Fixture     *fixture,
            const gchar *uuid,
            const gchar *command,
            GError     **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_settings_set_upload_command (fixture->settings, command);
    g_paste_daemon_upload (fixture->daemon, uuid, on_uploaded, &result);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_nonnull (result);

    return g_paste_daemon_upload_finish (fixture->daemon, result, error);
}

/* @text added to the history, for an upload to take: its uuid. */
static gchar *
add_text (Fixture     *fixture,
          const gchar *text)
{
    GPasteItem *item = g_paste_text_item_new (text);
    gchar *uuid = g_strdup (g_paste_item_get_uuid (item));

    g_paste_history_add (fixture->daemon->history, item);

    return uuid;
}

/* Upload takes a text item alone: a password's value is the placeholder
 * standing for it, which no command is handed. */
static void
test_upload_refuses_password (Fixture      *fixture,
                              gconstpointer user_data G_GNUC_UNUSED)
{
    GPasteItem *password = g_paste_password_item_new ("name", "secret", 0);
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (password));
    g_autoptr (GError) error = NULL;

    g_paste_history_add (fixture->daemon->history, password);

    g_autofree gchar *url = run_upload (fixture, uuid, "false", &error);

    g_assert_null (url);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_WRONG_ITEM_KIND);
}

/* An encrypted flavour whose passphrase a change is asked of: one this build
 * opens when @offered, one it lacks otherwise -- G_PASTE_STORAGE_NOOP when
 * there is none of that sort. */
static GPasteStorage
encrypted_flavour (gboolean offered)
{
    const GPasteStorage flavours[] = { G_PASTE_STORAGE_ENCRYPTED_FILE, G_PASTE_STORAGE_ENCRYPTED_SQLITE };

    for (guint i = 0; i < G_N_ELEMENTS (flavours); ++i)
    {
        if (g_paste_prompt_storage_is_offered (flavours[i]) == offered)
            return flavours[i];
    }

    return G_PASTE_STORAGE_NOOP;
}

/* A change of passphrase rewrites every store, which a daemon this one took
 * over from may still be writing: refused BUSY, the host never asked, until
 * this one holds the storage lock. */
static void
test_change_passphrase_holds_store (Fixture      *fixture,
                                    gconstpointer user_data G_GNUC_UNUSED)
{
    GPasteStorage storage = encrypted_flavour (TRUE);

    if (storage == G_PASTE_STORAGE_NOOP)
    {
        g_test_skip ("This build opens no encrypted storage");
        return;
    }

    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *lock = g_build_filename (dir, "lock", NULL);
    g_autoptr (GError) error = NULL;
    guint asked = 0;

    g_paste_settings_set_storage_backend (fixture->settings, storage);
    g_signal_connect_swapped (fixture->daemon, "change-passphrase", G_CALLBACK (g_paste_test_bus_count_emission), &asked);
    g_paste_storage_backend_unlock ();
    g_assert_true (g_paste_util_ensure_history_dir_exists ());

    gint held = g_open (lock, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

    g_assert_cmpint (held, >=, 0);
    g_assert_cmpint (flock (held, LOCK_EX | LOCK_NB), ==, 0);
    g_paste_daemon_change_passphrase (fixture->daemon, &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_assert_cmpuint (asked, ==, 0);

    g_close (held, NULL);
    g_clear_error (&error);
    g_paste_daemon_change_passphrase (fixture->daemon, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (asked, ==, 1);
}

/* A store this build cannot open has no passphrase it could change: refused,
 * the host never asked to flush and reload around a re-key it would refuse. */
static void
test_change_passphrase_unopenable (Fixture      *fixture,
                                   gconstpointer user_data G_GNUC_UNUSED)
{
    GPasteStorage storage = encrypted_flavour (FALSE);

    if (storage == G_PASTE_STORAGE_NOOP)
    {
        g_test_skip ("This build opens every encrypted storage");
        return;
    }

    g_autoptr (GError) error = NULL;
    guint asked = 0;

    g_paste_settings_set_storage_backend (fixture->settings, storage);
    g_signal_connect_swapped (fixture->daemon, "change-passphrase", G_CALLBACK (g_paste_test_bus_count_emission), &asked);
    g_paste_daemon_change_passphrase (fixture->daemon, &error);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_cmpuint (asked, ==, 0);
}

/* A command that does not stop writing is cut off once it has written more
 * than a url, rather than read until the deadline. */
static void
test_upload_bounds_answer (Fixture      *fixture,
                           gconstpointer user_data G_GNUC_UNUSED)
{
    g_autofree gchar *uuid = add_text (fixture, "uploaded");
    g_autoptr (GError) error = NULL;
    g_autofree gchar *url = run_upload (fixture, uuid, "yes https://example.com/", &error);

    g_assert_null (url);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
}

/* A command answering before it has read the whole item -- far more than a
 * pipe holds -- is judged by its exit and its answer, the write it left
 * unread saying nothing about the upload. */
static void
test_upload_unread_input (Fixture      *fixture,
                          gconstpointer user_data G_GNUC_UNUSED)
{
    g_autofree gchar *text = g_strnfill (1024 * 1024, 'x');
    g_autofree gchar *uuid = add_text (fixture, text);
    g_autoptr (GError) error = NULL;
    g_autofree gchar *url = run_upload (fixture, uuid, "sh -c 'head -c 1 >/dev/null; echo https://example.com/x'", &error);

    g_assert_no_error (error);
    g_assert_cmpstr (url, ==, "https://example.com/x");
}

/* The item goes to a command reading none of it yet -- far more of it than a
 * pipe holds -- without the daemon waiting for it to: the upload is started,
 * and the main loop free, long before the command reads. */
static void
test_upload_does_not_block (Fixture      *fixture,
                            gconstpointer user_data G_GNUC_UNUSED)
{
    g_autofree gchar *text = g_strnfill (1024 * 1024, 'x');
    g_autofree gchar *uuid = add_text (fixture, text);
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_upload_command (fixture->settings, "sh -c 'sleep 2; cat >/dev/null; echo https://example.com/x'");

    gint64 start = g_get_monotonic_time ();

    g_paste_daemon_upload (fixture->daemon, uuid, on_uploaded, &result);
    g_assert_cmpint (g_get_monotonic_time () - start, <, G_USEC_PER_SEC);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_nonnull (result);

    g_autofree gchar *url = g_paste_daemon_upload_finish (fixture->daemon, result, &error);

    g_assert_no_error (error);
    g_assert_cmpstr (url, ==, "https://example.com/x");
}

/* An answer is one url: whatever follows it on another line, or in an escape
 * sequence, fails the upload rather than going on the clipboard as the link. */
static void
test_upload_answer_is_one_url (Fixture      *fixture,
                               gconstpointer user_data G_GNUC_UNUSED)
{
    const gchar *commands[] = {
        "printf 'https://example.com/a\\necho injected\\n'",
        "printf 'https://example.com/a\\033]52;c;eA==\\a'",
        "printf 'https://example.com/\\342\\200\\256a'",
    };

    for (guint i = 0; i < G_N_ELEMENTS (commands); ++i)
    {
        g_autofree gchar *text = g_strdup_printf ("uploaded %u", i);
        g_autofree gchar *uuid = add_text (fixture, text);
        g_autoptr (GError) error = NULL;
        g_autofree gchar *url = run_upload (fixture, uuid, commands[i], &error);

        g_assert_null (url);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    }
}

/* An upload out when the daemon leaves the bus is given up on there, its
 * command killed: answered as cancelled at once rather than at the deadline,
 * it lets the daemon go with it. */
static void
test_upload_ends_with_export (Fixture      *fixture,
                              gconstpointer user_data G_GNUC_UNUSED)
{
    g_autofree gchar *uuid = add_text (fixture, "uploaded");
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;
    GPasteDaemon *daemon = fixture->daemon;

    g_paste_settings_set_upload_command (fixture->settings, "sleep 60");
    g_paste_daemon_upload (daemon, uuid, on_uploaded, &result);
    g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (daemon));
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_nonnull (result);

    g_autofree gchar *url = g_paste_daemon_upload_finish (daemon, result, &error);

    g_assert_null (url);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

    g_clear_object (&result);
    g_object_add_weak_pointer (G_OBJECT (daemon), (gpointer *) &daemon);
    g_clear_object (&fixture->daemon);
    g_assert_null (daemon);
}

static gboolean
is_cleared (gconstpointer pointer,
            gconstpointer arg G_GNUC_UNUSED)
{
    return !*((gconstpointer const *) pointer);
}

/* The shortcut's upload, given up on as the daemon leaves the bus, is no
 * failure to report: nothing is logged, a warning failing the case. */
static void
test_upload_shortcut_ends_quietly (Fixture      *fixture,
                                   gconstpointer user_data G_GNUC_UNUSED)
{
    g_autofree gchar *uuid = add_text (fixture, "uploaded");
    GPasteDaemon *daemon = fixture->daemon;

    g_paste_settings_set_upload_command (fixture->settings, "sleep 60");
    keybinding_upload (NULL, daemon);
    g_assert_nonnull (daemon->uploads);
    g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (daemon));

    /* The upload's task holds the daemon until its callback has run. */
    g_object_add_weak_pointer (G_OBJECT (daemon), (gpointer *) &daemon);
    g_clear_object (&fixture->daemon);
    g_paste_test_bus_wait_until (is_cleared, &daemon, NULL);
    g_assert_null (daemon);
}

/* A re-exec replaces the process, so the uploads out end before it, as cancelled;
 * the daemon gnome-shell hosts carries on through one, its uploads with it. */
static void
upload_through_reexec (Fixture *fixture,
                       gboolean in_process)
{
    g_autofree gchar *uuid = add_text (fixture, "uploaded");
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    fixture->daemon->in_process = in_process;
    g_signal_connect (fixture->daemon, "reexecute-self", G_CALLBACK (on_reexecute_self), fixture);
    g_paste_settings_set_upload_command (fixture->settings, "sleep 60");
    g_paste_daemon_upload (fixture->daemon, uuid, on_uploaded, &result);
    g_paste_daemon_reexecute (fixture->daemon);
    g_paste_test_bus_wait_for_count (&fixture->reexecutions, 1);

    if (in_process)
    {
        /* Long enough for a cancelled upload to have come back. */
        g_paste_test_bus_pump (500);
        g_assert_null (result);
        g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon));
    }

    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &result, NULL);
    g_assert_nonnull (result);
    g_assert_null (g_paste_daemon_upload_finish (fixture->daemon, result, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

static void
test_upload_ends_with_reexec (Fixture      *fixture,
                              gconstpointer user_data G_GNUC_UNUSED)
{
    upload_through_reexec (fixture, FALSE);
}

static void
test_upload_survives_in_process_reexec (Fixture      *fixture,
                                        gconstpointer user_data G_GNUC_UNUSED)
{
    upload_through_reexec (fixture, TRUE);
}

static void
on_reexecute_answered (GObject      *source_object G_GNUC_UNUSED,
                       GAsyncResult *result,
                       gpointer      user_data)
{
    *(GAsyncResult **) user_data = g_object_ref (result);
}

/* A re-exec asked over the bus while a storage worker rewrites the store is
 * refused, rather than answered for an exec the host defers until the worker
 * is done: no countdown is brought forward for it, the host is not asked, and
 * the upload out goes on. One without an invocation (SIGUSR1) is the host's to
 * defer, the upload left to the exec that follows. */
static void
test_reexecute_store_busy (Fixture      *fixture,
                           gconstpointer user_data G_GNUC_UNUSED)
{
    g_autofree gchar *uuid = add_text (fixture, "uploaded");
    g_autoptr (GAsyncResult) uploaded = NULL;
    g_autoptr (GAsyncResult) answered = NULL;
    g_autoptr (GError) error = NULL;

    expiries = 0;
    storage_busy = TRUE;
    g_signal_connect (fixture->daemon, "reexecute-self", G_CALLBACK (on_reexecute_self), fixture);
    g_paste_settings_set_upload_command (fixture->settings, "sleep 60");
    g_paste_daemon_upload (fixture->daemon, uuid, on_uploaded, &uploaded);

    g_dbus_connection_call (fixture->connection, g_dbus_connection_get_unique_name (fixture->connection),
                            G_PASTE_DAEMON_OBJECT_PATH, G_PASTE_DAEMON_INTERFACE_NAME, "Reexecute",
                            NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, on_reexecute_answered, &answered);
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &answered, NULL);
    g_assert_nonnull (answered);
    g_assert_null (g_dbus_connection_call_finish (fixture->connection, answered, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_assert_cmpuint (expiries, ==, 0);
    g_assert_cmpuint (fixture->reexecutions, ==, 0);

    g_paste_daemon_reexecute (fixture->daemon);
    g_paste_test_bus_wait_for_count (&fixture->reexecutions, 1);
    /* Long enough for a cancelled upload to have come back. */
    g_paste_test_bus_pump (500);
    g_assert_null (uploaded);

    storage_busy = FALSE;
    g_paste_bus_object_unregister_on_connection (G_PASTE_BUS_OBJECT (fixture->daemon));
    g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &uploaded, NULL);
    g_assert_nonnull (uploaded);
}

/* Under track-extension-state, the extension going pauses the tracking and its
 * coming back resumes what that paused -- and nothing else: it says it is there
 * to every daemon it finds, after a restart, a handoff or an unlock, and a
 * pause of the user's own outlives all of it, as does one made while it was
 * away, however the tracking was turned on and off again meanwhile. */
static void
test_extension_state (Fixture      *fixture,
                      gconstpointer user_data G_GNUC_UNUSED)
{
    GPasteSettings *settings = fixture->settings;
    GPasteDaemon *daemon = fixture->daemon;
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (daemon);
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_track_extension_state (settings, TRUE);
    g_paste_settings_set_track_changes (settings, TRUE);

    /* Away, and back. */
    g_paste_daemon_extension_state_changed (daemon, FALSE);
    g_assert_false (g_paste_settings_get_track_changes (settings));
    g_paste_daemon_extension_state_changed (daemon, TRUE);
    g_assert_true (g_paste_settings_get_track_changes (settings));

    /* The user pausing, then the extension saying it is there to a daemon
     * restarted under it, and going and coming as the shell rebases it. */
    g_paste_daemon_methods_set_active (&methods, FALSE, &error);
    g_assert_no_error (error);
    g_paste_daemon_extension_state_changed (daemon, TRUE);
    g_assert_false (g_paste_settings_get_track_changes (settings));
    g_paste_daemon_extension_state_changed (daemon, FALSE);
    g_paste_daemon_extension_state_changed (daemon, TRUE);
    g_assert_false (g_paste_settings_get_track_changes (settings));

    /* Away, the tracking turned on and off again from the preferences: the
     * pause is the user's by then. */
    g_paste_settings_set_track_changes (settings, TRUE);
    g_paste_daemon_extension_state_changed (daemon, FALSE);
    g_paste_settings_set_track_changes (settings, TRUE);
    g_paste_settings_set_track_changes (settings, FALSE);
    g_paste_daemon_extension_state_changed (daemon, TRUE);
    g_assert_false (g_paste_settings_get_track_changes (settings));

    /* Away, and the user asking for no tracking all the same. */
    g_paste_settings_set_track_changes (settings, TRUE);
    g_paste_daemon_extension_state_changed (daemon, FALSE);
    g_paste_daemon_methods_set_active (&methods, FALSE, &error);
    g_assert_no_error (error);
    g_paste_daemon_extension_state_changed (daemon, TRUE);
    g_assert_false (g_paste_settings_get_track_changes (settings));

    g_paste_settings_reset (settings, G_PASTE_TRACK_EXTENSION_STATE_SETTING);
    g_paste_settings_reset (settings, G_PASTE_TRACK_CHANGES_SETTING);
    g_paste_settings_reset (settings, G_PASTE_EXTENSION_PAUSED_SETTING);
}

/* A pause a GPaste with no "extension-paused" left -- the tracking off under
 * track-extension-state, which it resumed on every return of the extension --
 * is carried over as the extension's, once: its return resumes the tracking.
 * Once, the key written either way: a pause of the user's own made since is
 * never taken for one. */
static void
test_extension_pause_carried_over (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestClipboard) clipboard = test_clipboard_new ();
    g_autoptr (TestClipboard) primary = test_clipboard_new ();

    g_paste_settings_set_history_name (settings, "daemon-teardown");
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_NOOP);
    g_paste_settings_set_track_extension_state (settings, TRUE);

    /* Left off by that GPaste: resumed. Left on: the user pausing it under
     * the first daemon, the second keeps it paused. */
    for (guint left_on = 0; left_on < 2; ++left_on)
    {
        g_paste_settings_reset (settings, G_PASTE_EXTENSION_PAUSED_SETTING);
        g_paste_settings_set_track_changes (settings, left_on);

        g_autoptr (GPasteDaemon) first = g_paste_daemon_new (settings, G_PASTE_CLIPBOARD_PROVIDER (clipboard), G_PASTE_CLIPBOARD_PROVIDER (primary));
        const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (first);
        g_autoptr (GError) error = NULL;

        /* Its screensaver client built before it goes, as fixture_set_up ()
         * has its own. */
        g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &first->screensaver, NULL);

        if (left_on)
        {
            g_paste_daemon_methods_set_active (&methods, FALSE, &error);
            g_assert_no_error (error);
            g_object_run_dispose (G_OBJECT (first));

            g_autoptr (GPasteDaemon) second = g_paste_daemon_new (settings, G_PASTE_CLIPBOARD_PROVIDER (clipboard), G_PASTE_CLIPBOARD_PROVIDER (primary));

            g_paste_test_bus_wait_until (g_paste_test_bus_is_set, &second->screensaver, NULL);
            g_paste_daemon_extension_state_changed (second, TRUE);
            g_assert_false (g_paste_settings_get_track_changes (settings));
            g_object_run_dispose (G_OBJECT (second));
        }
        else
        {
            g_paste_daemon_extension_state_changed (first, TRUE);
            g_assert_true (g_paste_settings_get_track_changes (settings));
            g_object_run_dispose (G_OBJECT (first));
        }
    }

    g_paste_settings_reset (settings, G_PASTE_TRACK_EXTENSION_STATE_SETTING);
    g_paste_settings_reset (settings, G_PASTE_TRACK_CHANGES_SETTING);
    g_paste_settings_reset (settings, G_PASTE_EXTENSION_PAUSED_SETTING);
}

/* What the extension's pause is cleared on -- tracking turned on, an export, a
 * SetActive -- writes nothing when there is no pause: each write is a dconf
 * round trip and a change every process watching the schema hears. */
static void
test_extension_paused_written_when_set (Fixture      *fixture,
                                        gconstpointer user_data G_GNUC_UNUSED)
{
    GPasteSettings *settings = fixture->settings;
    const GPasteDaemonMethods methods = G_PASTE_DAEMON_METHODS (fixture->daemon);
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_track_changes (settings, FALSE);
    extension_paused_writes = 0;
    g_paste_settings_set_track_changes (settings, TRUE);
    g_paste_daemon_methods_set_active (&methods, TRUE, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (extension_paused_writes, ==, 0);

    /* A pause there is cleared, once. */
    g_paste_settings_set_extension_paused (settings, TRUE);
    g_paste_daemon_methods_set_active (&methods, TRUE, &error);
    g_assert_no_error (error);
    g_assert_false (g_paste_settings_get_extension_paused (settings));
    g_assert_cmpuint (extension_paused_writes, ==, 2);

    g_paste_settings_reset (settings, G_PASTE_TRACK_CHANGES_SETTING);
    g_paste_settings_reset (settings, G_PASTE_EXTENSION_PAUSED_SETTING);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    g_test_add ("/daemon/teardown/dispose-twice", Fixture, NULL, fixture_set_up, test_dispose_twice, fixture_tear_down);
    g_test_add ("/daemon/shortcuts/handed-over", Fixture, NULL, fixture_set_up, test_shortcuts_handed_over, fixture_tear_down);
    g_test_add_func ("/daemon/methods/extension-pause-carried-over", test_extension_pause_carried_over);
    g_test_add ("/daemon/teardown/unregister-cancels-startup", Fixture, NULL, fixture_set_up, test_unregister_cancels_startup, fixture_tear_down);
    g_test_add ("/daemon/teardown/release-pending-startup", Fixture, NULL, fixture_set_up, test_release_pending_startup, fixture_tear_down);
    g_test_add ("/daemon/teardown/startup-update", Fixture, NULL, fixture_set_up, test_startup_update, fixture_tear_down);
    g_test_add ("/daemon/reexecute/stores-first", Fixture, NULL, fixture_set_up, test_reexecute_stores_first, fixture_tear_down);
    g_test_add ("/daemon/reexecute/in-process", Fixture, NULL, fixture_set_up, test_reexecute_in_process, fixture_tear_down);
    g_test_add ("/daemon/reexecute/store-busy", Fixture, NULL, fixture_set_up, test_reexecute_store_busy, fixture_tear_down);
    g_test_add ("/daemon/methods/extension-state", Fixture, NULL, fixture_set_up, test_extension_state, fixture_tear_down);
    g_test_add ("/daemon/methods/extension-paused-written-when-set", Fixture, NULL, fixture_set_up, test_extension_paused_written_when_set, fixture_tear_down);
    g_test_add ("/daemon/change-passphrase/holds-store", Fixture, NULL, fixture_set_up, test_change_passphrase_holds_store, fixture_tear_down);
    g_test_add ("/daemon/change-passphrase/unopenable", Fixture, NULL, fixture_set_up, test_change_passphrase_unopenable, fixture_tear_down);
    g_test_add ("/daemon/upload/refuses-password", Fixture, NULL, fixture_set_up, test_upload_refuses_password, fixture_tear_down);
    g_test_add ("/daemon/upload/bounds-answer", Fixture, NULL, fixture_set_up, test_upload_bounds_answer, fixture_tear_down);
    g_test_add ("/daemon/upload/unread-input", Fixture, NULL, fixture_set_up, test_upload_unread_input, fixture_tear_down);
    g_test_add ("/daemon/upload/does-not-block", Fixture, NULL, fixture_set_up, test_upload_does_not_block, fixture_tear_down);
    g_test_add ("/daemon/upload/answer-is-one-url", Fixture, NULL, fixture_set_up, test_upload_answer_is_one_url, fixture_tear_down);
    g_test_add ("/daemon/upload/ends-with-export", Fixture, NULL, fixture_set_up, test_upload_ends_with_export, fixture_tear_down);
    g_test_add ("/daemon/upload/shortcut-ends-quietly", Fixture, NULL, fixture_set_up, test_upload_shortcut_ends_quietly, fixture_tear_down);
    g_test_add ("/daemon/upload/ends-with-reexec", Fixture, NULL, fixture_set_up, test_upload_ends_with_reexec, fixture_tear_down);
    g_test_add ("/daemon/upload/survives-in-process-reexec", Fixture, NULL, fixture_set_up, test_upload_survives_in_process_reexec, fixture_tear_down);

    return g_paste_test_env_run ();
}
