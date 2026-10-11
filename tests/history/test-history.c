// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <glib/gstdio.h>
#include <gpaste-test-env.h>
#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-update-enums.h>
#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-storage-migration.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef G_PASTE_ENABLE_SQLITE
#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-sqlite-backend.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <sqlite3.h>
#include <string.h>

#ifdef G_PASTE_ENABLE_ENCRYPTION
#include <sodium.h>
#endif
#endif

/* The timeout the storage round-trips carry on their password entry: a value no
 * default would produce, so reading one back proves it was persisted rather than
 * merely defaulted. */
#define PASSWORD_TIMEOUT 42

/* Build a fresh, empty history backed by an in-memory GSettings.
 * Growing-lines merging is disabled so distinct strings stay distinct. */
static GPasteHistory *
make_history (GPasteSettings **out_settings,
              guint64          max_history_size)
{
    /* A distinct history name per call keeps each test's on-disk file (under the
     * test environment's private XDG_DATA_HOME) separate, so the model's async
     * persistence can't leak state from one test into the next. */
    static guint counter = 0;
    g_autofree gchar *name = g_strdup_printf ("test-%u", counter++);

    GPasteSettings *settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_history_size (settings, max_history_size);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);

    GPasteHistory *history = g_paste_history_new (settings);

    /* Give the history its name (as the daemon does at startup): this loads the
     * configured history, which does not yet exist, and lets the model persist
     * itself without asserting on a NULL name. */
    g_paste_history_load (history, name);

    if (out_settings)
        *out_settings = settings;
    else
        g_object_unref (settings);

    return history;
}

/* GSettings refusing a write, as it does for a locked key: nothing stored,
 * nothing announced. Only the key named here is refused.
 *
 * Or, with @unstored, dconf with a database it cannot write: the write is
 * taken and announced, then given back once g_settings_sync () waits for it,
 * with a "changed" that comes later, from the main loop -- dconf's worker
 * thread announces it there, as give_back () does. */
static const gchar *refused_key = NULL;
static gboolean unstored = FALSE;
static GVariant *unstored_value = NULL;

static gboolean
refuse_write (GSettings   *settings,
              const gchar *key)
{
    if (!g_paste_str_equal (key, refused_key))
        return FALSE;
    if (!unstored)
        return TRUE;
    if (!unstored_value)
        unstored_value = g_settings_get_value (settings, key);
    return FALSE;
}

gboolean
g_settings_set_boolean (GSettings   *settings,
                        const gchar *key,
                        gboolean     value)
{
    typedef gboolean (*SetBoolean) (GSettings *, const gchar *, gboolean);
    SetBoolean original = (SetBoolean) dlsym (RTLD_NEXT, "g_settings_set_boolean");

    g_assert_nonnull (original);
    if (refuse_write (settings, key))
        return FALSE;
    return original (settings, key, value);
}

static gpointer
give_back (gpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GSettings) settings = g_settings_new (G_PASTE_SETTINGS_NAME);

    g_settings_set_value (settings, refused_key, unstored_value);

    return NULL;
}

void
g_settings_sync (void)
{
    typedef void (*Sync) (void);
    Sync original = (Sync) dlsym (RTLD_NEXT, "g_settings_sync");

    g_assert_nonnull (original);
    original ();
    if (!unstored_value)
        return;

    /* Owned here, the main context is where the worker's "changed" is queued
     * rather than run on the worker at once. */
    g_main_context_acquire (NULL);
    g_thread_join (g_thread_new ("give-back", give_back, NULL));
    g_main_context_release (NULL);
    g_clear_pointer (&unstored_value, g_variant_unref);
}

/* What dconf announces once it gives a write back, run. */
static void
drain (void)
{
    while (g_main_context_iteration (NULL, FALSE));
}

gboolean
g_settings_set_string (GSettings   *settings,
                       const gchar *key,
                       const gchar *value)
{
    typedef gboolean (*SetString) (GSettings *, const gchar *, const gchar *);
    SetString original = (SetString) dlsym (RTLD_NEXT, "g_settings_set_string");

    g_assert_nonnull (original);
    if (refuse_write (settings, key))
        return FALSE;
    return original (settings, key, value);
}

gboolean
g_settings_set_enum (GSettings   *settings,
                     const gchar *key,
                     gint         value)
{
    typedef gboolean (*SetEnum) (GSettings *, const gchar *, gint);
    SetEnum original = (SetEnum) dlsym (RTLD_NEXT, "g_settings_set_enum");

    g_assert_nonnull (original);
    if (refuse_write (settings, key))
        return FALSE;
    return original (settings, key, value);
}

static void
test_set_active_refused (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const GPasteDaemonMethods methods = { .settings = settings };
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_track_changes (settings, TRUE);
    refused_key = G_PASTE_TRACK_CHANGES_SETTING;

    /* Asking for the state it already has is no refusal, locked or not. */
    g_paste_daemon_methods_set_active (&methods, TRUE, &error);
    g_assert_no_error (error);

    g_paste_daemon_methods_set_active (&methods, FALSE, &error);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_true (g_paste_settings_get_track_changes (settings));
    g_clear_error (&error);
    drain ();
    g_assert_true (g_paste_settings_get_track_changes (settings));

    refused_key = NULL;
    g_paste_daemon_methods_set_active (&methods, FALSE, &error);
    g_assert_no_error (error);
    g_assert_false (g_paste_settings_get_track_changes (settings));

    g_paste_settings_reset (settings, G_PASTE_TRACK_CHANGES_SETTING);
}

/* A write dconf takes but cannot store is as much a refusal, though it is
 * announced first and given back only once the daemon waits for it. */
static void
test_set_active_unstored (void)
{
    unstored = TRUE;
    test_set_active_refused ();
    unstored = FALSE;
}

/* A history-name GSettings refuses to store leaves the history where it was,
 * which SwitchHistory says rather than answer OK; and DeleteHistory of the
 * current history, which has to switch away from it, deletes nothing then --
 * done, the next copy would have written it back. */
static void
test_switch_refused (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    const GPasteDaemonMethods methods = { .history = history, .settings = settings };
    g_autoptr (GError) error = NULL;
    const gchar *name = "switch-refused";

    g_paste_history_load (history, name);
    g_paste_settings_set_history_name (settings, name);

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xml");

    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    refused_key = G_PASTE_HISTORY_NAME_SETTING;
    g_paste_daemon_methods_switch_history (&methods, "switch-refused-elsewhere", &error);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_clear_error (&error);
    drain ();

    g_paste_daemon_methods_delete_history (&methods, name, &error);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    refused_key = NULL;
    drain ();

    g_assert_cmpstr (g_paste_history_get_current (history), ==, name);
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
    g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
}

/* As test_set_active_unstored (). */
static void
test_switch_unstored (void)
{
    unstored = TRUE;
    test_switch_refused ();
    unstored = FALSE;
}

static void
count_dropped (GPasteHistory *history G_GNUC_UNUSED,
               GPasteItem    *item G_GNUC_UNUSED,
               gpointer       user_data)
{
    ++*(guint *) user_data;
}

/* Deleting the current history leaves it for the default one, and the
 * passwords it held are announced as gone: a switch announces none, and one
 * still on a selection would stay there in clear. */
static void
test_delete_current_drops_passwords (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (GError) error = NULL;
    const gchar *name = "delete-current-passwords";
    guint dropped = 0;

    g_paste_history_load (history, name);
    g_paste_settings_set_history_name (settings, name);
    g_paste_history_add (history, g_paste_password_item_new ("bank", "the secret", 0));
    g_paste_history_add (history, g_paste_text_item_new ("a later copy"));
    g_signal_connect (history, "password-dropped", G_CALLBACK (count_dropped), &dropped);

    g_assert_true (g_paste_history_delete (history, name, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (g_paste_history_get_current (history), ==, G_PASTE_DEFAULT_HISTORY);
    g_assert_cmpuint (dropped, ==, 1);
    g_signal_handlers_disconnect_by_data (history, &dropped);
    g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
}

/* A current history whose delete fails is the current one still: it was left
 * for the delete's sake. */
static void
test_delete_current_failed_stays (void)
{
    if (!geteuid ())
    {
        g_test_skip ("root removes files from a directory it cannot write");
        return;
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (GError) error = NULL;
    const gchar *name = "delete-current-failed";
    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xml");

    g_paste_history_load (history, name);
    g_paste_settings_set_history_name (settings, name);
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
    /* Held first: taking it on the way would make the history directory
     * private again, undoing the chmod below. */
    g_assert_true (g_paste_storage_backend_try_lock ());
    g_assert_cmpint (g_chmod (dir, 0500), ==, 0);

    /* Its mode back before anything is asserted, as in test-storage-failures'
     * test_rekey_unlistable_images (). */
    gboolean deleted = g_paste_history_delete (history, name, &error);

    g_assert_cmpint (g_chmod (dir, 0700), ==, 0);
    g_assert_false (deleted);
    g_assert_nonnull (error);
    g_assert_cmpstr (g_paste_history_get_current (history), ==, name);
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
    g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
}

/* A backup of a history no store holds is refused as not found, rather than
 * reading it into an empty one and backing that up. */
static void
test_backup_missing_source (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    const GPasteDaemonMethods methods = { .history = history, .settings = settings };
    g_autoptr (GError) error = NULL;
    g_autofree gchar *source = g_paste_util_get_history_file_path ("backup-missing-source", "xml");
    g_autofree gchar *copy = g_paste_util_get_history_file_path ("backup-missing-copy", "xml");

    g_paste_history_load (history, "backup-missing-current");
    g_paste_daemon_methods_backup_history (&methods, "backup-missing-source", "backup-missing-copy", &error);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND);
    g_assert_false (g_file_test (source, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (copy, G_FILE_TEST_EXISTS));
}

/* Deleting a history no store holds is not found, as a uuid matching nothing
 * is, rather than whatever unlinking a missing file reports. */
static void
test_delete_missing_history (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    const GPasteDaemonMethods methods = { .history = history, .settings = settings };
    g_autoptr (GError) error = NULL;

    g_paste_history_load (history, "delete-missing-current");
    g_paste_daemon_methods_delete_history (&methods, "delete-missing-typo", &error);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND);
}

static void
test_add_password_name_conflict (void)
{
    g_autoptr (GPasteHistory) history = make_history (NULL, 10);
    GPasteItem *original = g_paste_password_item_new ("Work", "original secret", PASSWORD_TIMEOUT);
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (original));

    g_paste_history_add (history, original);
    g_paste_history_add (history, g_paste_text_item_new ("clipboard text"));

    /* No clipboard manager: a conflict must return before publishing anything. */
    const GPasteDaemonMethods methods = { .history = history };
    const gchar *values[] = { "replacement secret", "original secret" };

    for (guint i = 0; i < G_N_ELEMENTS (values); ++i)
    {
        g_autoptr (GError) error = NULL;
        g_autofree gchar *result = g_paste_daemon_methods_add_password (&methods, "Work", values[i], 0, &error);

        g_assert_null (result);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_ALREADY_EXISTS);
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
        g_assert_true (g_paste_history_get_by_uuid (history, uuid) == original);
        g_assert_cmpstr (g_paste_item_get_real_value (original), ==, "original secret");
        g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (original)), ==, PASSWORD_TIMEOUT);
        g_assert_cmpstr (g_paste_item_get_real_value (g_paste_history_get (history, 0)), ==, "clipboard text");
    }
}

/* Past the text cap with trim-items off, a file whose first bytes hold a NUL
 * begins no text: a TIFF, its header "II*\0", is added as the image it is
 * rather than refused as too long a text -- with images-support on, the
 * history keeping no image otherwise, which the next read would drop. */
static void
test_add_file_tiff (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    const GPasteDaemonMethods methods = { .clipboards_manager = manager, .history = history, .settings = settings };
    g_autofree gchar *dir = g_build_filename (g_get_user_cache_dir (), "add-file-tiff", NULL);
    g_autofree gchar *path = g_build_filename (dir, "image.tiff", NULL);
    /* Noise, so that the TIFF is more than the head read of it. */
    g_autoptr (GRand) rand = g_rand_new_with_seed (7);
    guint32 *pixels = g_malloc (64 * 64 * 4);

    for (gsize i = 0; i < 64 * 64; ++i)
        pixels[i] = g_rand_int (rand) | 0xff000000;

    g_autoptr (GBytes) pixel_bytes = g_bytes_new_take (pixels, 64 * 64 * 4);
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (64, 64, GDK_MEMORY_R8G8B8A8, pixel_bytes, 64 * 4);
    g_autoptr (GBytes) tiff = gdk_texture_save_to_tiff_bytes (texture);
    g_autoptr (GError) error = NULL;

    g_assert_cmpuint (g_bytes_get_size (tiff), >, 4096);
    g_assert_cmpmem (g_bytes_get_data (tiff, NULL), 4, "II*\0", 4);
    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, g_bytes_get_data (tiff, NULL), (gssize) g_bytes_get_size (tiff), NULL));
    g_paste_settings_set_trim_items (settings, FALSE);
    g_paste_settings_set_max_text_item_size (settings, 1024);
    g_paste_settings_set_images_support (settings, FALSE);
    g_assert_null (g_paste_daemon_methods_add_file (&methods, path, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_REJECTED);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_clear_error (&error);
    g_paste_settings_set_images_support (settings, TRUE);

    g_autofree gchar *uuid = g_paste_daemon_methods_add_file (&methods, path, &error);

    g_assert_no_error (error);
    g_assert_nonnull (uuid);
    g_assert_true (G_PASTE_IS_IMAGE_ITEM (g_paste_history_get (history, 0)));

    g_assert_cmpint (g_unlink (path), ==, 0);
    g_assert_cmpint (g_rmdir (dir), ==, 0);
}

/* The file g_file_get_contents () fails the case for being asked to read. */
static const gchar *unread_path = NULL;

gboolean
g_file_get_contents (const gchar *filename,
                     gchar      **contents,
                     gsize       *length,
                     GError     **error)
{
    typedef gboolean (*GetContents) (const gchar *, gchar **, gsize *, GError **);
    GetContents original = (GetContents) dlsym (RTLD_NEXT, "g_file_get_contents");

    g_assert_nonnull (original);
    g_assert_false (g_paste_str_equal (filename, unread_path));
    return original (filename, contents, length, error);
}

/* AddFile reads only a regular file the history could keep: a FIFO would hold
 * the daemon until something wrote to it, and a file over the memory cap is
 * refused unread, as unread_path holds it to, root or not. Past the
 * text cap with trim-items off, one beginning as a text is refused on its first
 * bytes, the NULs past them, which a whole read would find, never looked at;
 * with trimming on it is read, since what trimming leaves may fit. */
static void
test_add_file_refusals (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    /* The add selects what it adds: a manager with no clipboard to put it on. */
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    const GPasteDaemonMethods methods = { .clipboards_manager = manager, .history = history, .settings = settings };
    /* Under the test environment's own directories, which go with it whatever
     * this case ends in. */
    g_autofree gchar *dir = g_build_filename (g_get_user_cache_dir (), "add-file", NULL);
    g_autofree gchar *fifo = g_build_filename (dir, "fifo", NULL);
    g_autofree gchar *big = g_build_filename (dir, "big", NULL);
    g_autofree gchar *long_text = g_build_filename (dir, "long-text", NULL);
    g_autofree gchar *padded = g_build_filename (dir, "padded", NULL);
    g_autoptr (GError) error = NULL;

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_cmpint (mkfifo (fifo, 0600), ==, 0);
    g_assert_null (g_paste_daemon_methods_add_file (&methods, fifo, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    gint fd = g_open (big, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);

    g_assert_cmpint (fd, >=, 0);
    g_assert_cmpint (ftruncate (fd, 6 * 1024 * 1024), ==, 0);
    g_assert_true (g_close (fd, NULL));
    g_paste_settings_set_max_memory_usage (settings, 5);
    unread_path = big;
    g_assert_null (g_paste_daemon_methods_add_file (&methods, big, &error));
    unread_path = NULL;
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_REJECTED);
    g_clear_error (&error);

    g_autofree gchar *head = g_strnfill (4096, 'a');

    g_assert_true (g_file_set_contents (long_text, head, -1, NULL));
    g_assert_cmpint (truncate (long_text, 2 * 1024 * 1024), ==, 0);
    g_paste_settings_set_max_text_item_size (settings, 1024 * 1024);
    unread_path = long_text;
    g_assert_null (g_paste_daemon_methods_add_file (&methods, long_text, &error));
    unread_path = NULL;
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_REJECTED);
    g_clear_error (&error);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);

    g_autofree gchar *spaces = g_strnfill (1024 * 1024, ' ');
    g_autofree gchar *padded_text = g_strconcat (spaces, "kept", spaces, NULL);

    g_assert_true (g_file_set_contents (padded, padded_text, -1, NULL));
    g_paste_settings_set_trim_items (settings, TRUE);

    g_autofree gchar *uuid = g_paste_daemon_methods_add_file (&methods, padded, &error);

    g_assert_no_error (error);
    g_assert_nonnull (uuid);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "kept");

    g_assert_cmpint (g_unlink (fifo), ==, 0);
    g_assert_cmpint (g_unlink (big), ==, 0);
    g_assert_cmpint (g_unlink (long_text), ==, 0);
    g_assert_cmpint (g_unlink (padded), ==, 0);
    g_assert_cmpint (g_rmdir (dir), ==, 0);
}

/* Like make_history but without forcing a name into GSettings (which would emit
 * a deferred "changed" and reload), so the round-trip tests can drive the name
 * themselves through load()/load_async(). */
static GPasteHistory *
make_plain_history_capped (guint64 max_history_size)
{
    GPasteSettings *settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_history_size (settings, max_history_size);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);

    GPasteHistory *history = g_paste_history_new (settings);

    g_object_unref (settings);

    return history;
}

static GPasteHistory *
make_plain_history (void)
{
    return make_plain_history_capped (100);
}

static const gchar *
value_at (GPasteHistory *history,
          guint64        index)
{
    GPasteItem *item = g_paste_history_get (history, index);
    return item ? g_paste_item_get_value (item) : NULL;
}

/* One millisecond of main loop: run everything pending, then let the clock move
 * on. Every wait in this file is built out of this, so a flaky-timeout fix has
 * one place to land. */
static void
pump_once (void)
{
    while (g_main_context_iteration (NULL, FALSE))
        ;
    g_usleep (1000);
}

/* Drain the main context for @ms milliseconds, letting whatever the history
 * queued -- a save, a load, a signal -- actually run. */
static void
pump_for_ms (guint ms)
{
    for (guint i = 0; i < ms; ++i)
        pump_once ();
}

/* Drain the main context for up to @max_ms ms, stopping early once @history
 * reaches @expected_len entries. */
static gboolean
pump_until_length (GPasteHistory *history,
                   guint64        expected_len,
                   guint          max_ms)
{
    for (guint i = 0; i < max_ms; ++i)
    {
        if (g_paste_history_get_length (history) == expected_len)
            return TRUE;

        pump_once ();
    }
    return g_paste_history_get_length (history) == expected_len;
}

/* A test that provokes a warning runs its body in a child: the refusals under
 * test warn, and with structured logging (G_LOG_USE_STRUCTURED)
 * g_test_expect_message cannot swallow them, so the child drops warning
 * fatality -- a critical still aborts it -- and the parent matches them on the
 * child's stderr instead. A child
 * run only for directories of its own, expecting no warning, leaves this out,
 * so that one still fails it. */
#define G_PASTE_TEST_IN_SUBPROCESS  g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR)

#define G_PASTE_TEST_TRAP_SUBPROCESS(stderr_glob)                    \
    do {                                                             \
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT); \
        g_test_trap_assert_passed ();                                \
        g_test_trap_assert_stderr (stderr_glob);                     \
    } while (FALSE)

/* A migration prompt answered at once, with what the test put in it. */
#define TEST_TYPE_MIGRATION_PROMPT (test_migration_prompt_get_type ())
G_DECLARE_FINAL_TYPE (TestMigrationPrompt, test_migration_prompt, TEST, MIGRATION_PROMPT, GObject)

struct _TestMigrationPrompt
{
    GObject parent_instance;

    GPasteStorage chosen;
    gboolean      import;
    gboolean      cleanup;
    /* The backend the last request said the history is in. */
    GPasteStorage current;
};

static void test_migration_prompt_iface_init (GPastePromptInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestMigrationPrompt, test_migration_prompt, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_PASTE_TYPE_PROMPT, test_migration_prompt_iface_init))

static void
test_migration_prompt_migration (GPastePrompt        *prompt,
                                 GPastePromptRequest *request)
{
    TestMigrationPrompt *self = TEST_MIGRATION_PROMPT (prompt);

    self->current = g_paste_prompt_request_get_current (request);
    g_paste_prompt_request_reply_migration (request, self->chosen, self->import, self->cleanup);
}

static void
test_migration_prompt_iface_init (GPastePromptInterface *iface)
{
    iface->migration = test_migration_prompt_migration;
}

static void
test_migration_prompt_class_init (TestMigrationPromptClass *klass G_GNUC_UNUSED)
{
}

static void
test_migration_prompt_init (TestMigrationPrompt *self G_GNUC_UNUSED)
{
}

static void
on_migration_settled (GObject      *source G_GNUC_UNUSED,
                      GAsyncResult *result,
                      gpointer      user_data)
{
    g_set_object ((GAsyncResult **) user_data, result);
}

static void
run_migration (GPasteSettings      *settings,
               TestMigrationPrompt *prompt)
{
    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_paste_storage_migration_async (G_PASTE_PROMPT (prompt), settings, on_migration_settled, &result);

    /* Bounded, so a migration that never settles fails here rather than at
     * meson's timeout; an import into an encrypted flavour takes seconds. */
    gint64 deadline = g_get_monotonic_time () + 60 * G_USEC_PER_SEC;

    while (!result && g_get_monotonic_time () < deadline)
    {
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    }
    g_assert_nonnull (result);

    g_assert_true (g_paste_storage_migration_finish (result, &error));
    g_assert_no_error (error);
}

/* A storage-backend GSettings refuses to store leaves the daemon reading the
 * old backend, so the old data has to stay, and the revision with it -- until
 * the reply names the backend the setting holds, which settles under the lock
 * too. */
static void
test_migration_refused_backend (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        const gchar *name = g_paste_settings_get_history_name (settings);
        guint64 revision = G_PASTE_STORAGE_BACKEND_REVISION - 1;

        g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
        g_paste_settings_set_storage_backend_revision (settings, revision);

        {
            g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
            g_autolist (GPasteItem) items = g_list_prepend (NULL, g_paste_text_item_new ("kept"));

            g_paste_storage_backend_write_history (file, name, items);
        }

        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

        /* Importing where an import can go, and deleting the old data. */
#ifdef G_PASTE_ENABLE_SQLITE
        prompt->chosen = G_PASTE_STORAGE_SQLITE;
#else
        prompt->chosen = G_PASTE_STORAGE_NOOP;
#endif
        prompt->import = TRUE;
        prompt->cleanup = TRUE;
        refused_key = G_PASTE_STORAGE_BACKEND_SETTING;
        run_migration (settings, prompt);

        g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_FILE);
        g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, revision);

        g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) kept = NULL;
        gsize size;

        g_assert_true (g_paste_storage_backend_read_history (file, name, NULL, &kept, &size));
        g_assert_cmpuint (g_list_length (kept), ==, 1);
        g_assert_cmpstr (g_paste_item_get_value (kept->data), ==, "kept");

        /* "Keep as is". */
        prompt->chosen = G_PASTE_STORAGE_FILE;
        prompt->import = FALSE;
        prompt->cleanup = FALSE;
        run_migration (settings, prompt);
        refused_key = NULL;

        g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_FILE);
        g_assert_cmpuint (g_paste_settings_get_storage_backend_revision (settings), ==, G_PASTE_STORAGE_BACKEND_REVISION);
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not store the new storage backend*");
}

/* As test_set_active_unstored (). */
static void
test_migration_unstored_backend (void)
{
    unstored = TRUE;
    test_migration_refused_backend ();
    unstored = FALSE;
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* A migration that does not move, having never unlocked the source itself,
 * puts back the passphrase the process held: the encrypted store left as it
 * was is the one that passphrase opens, and dropping it would have it asked
 * for again -- or, dismissed, nothing stored for the session. */
static void
test_migration_refused_keeps_passphrase (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

        g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);
        g_paste_settings_set_storage_backend_revision (settings, G_PASTE_STORAGE_BACKEND_REVISION - 1);
        g_paste_storage_backend_set_passphrase ("held");

        prompt->chosen = G_PASTE_STORAGE_FILE;
        refused_key = G_PASTE_STORAGE_BACKEND_SETTING;
        run_migration (settings, prompt);
        refused_key = NULL;

        g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_ENCRYPTED_FILE);
        g_assert_cmpstr (g_paste_storage_backend_get_passphrase (), ==, "held");
        g_paste_storage_backend_set_passphrase (NULL);
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*Could not store the new storage backend*");
}
#endif

#ifdef G_PASTE_ENABLE_SQLITE
/* Under a storage-backend GSettings refuses to store, the history is in the
 * backend the setting holds, whatever the files say: that is the one the
 * daemon goes on running on, and what an import reads and a cleanup deletes --
 * not the copy a refused migration left in the backend it could not switch to. */
static void
test_migration_locked_backend_is_current (void)
{
    if (g_test_subprocess ())
    {
        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        const gchar *name = "locked-backend-is-current";

        g_paste_settings_set_history_name (settings, name);

        {
            g_autoptr (GPasteStorageBackend) refused = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
            g_autoptr (GPasteStorageBackend) in_use = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
            g_autolist (GPasteItem) old_items = g_list_prepend (NULL, g_paste_text_item_new ("stale"));
            g_autolist (GPasteItem) items = g_list_prepend (NULL, g_paste_text_item_new ("recent"));

            g_paste_storage_backend_write_history (refused, name, old_items);
            g_paste_storage_backend_write_history (in_use, name, items);
        }

        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

        /* "Keep as is". */
        prompt->chosen = G_PASTE_STORAGE_FILE;
        refused_key = G_PASTE_STORAGE_BACKEND_SETTING;
        run_migration (settings, prompt);
        refused_key = NULL;
        g_assert_cmpint (prompt->current, ==, G_PASTE_STORAGE_FILE);
        g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_FILE);

        g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*");
}

/* As test_set_active_unstored (). */
static void
test_migration_unstored_backend_is_current (void)
{
    unstored = TRUE;
    test_migration_locked_backend_is_current ();
    unstored = FALSE;
}
#endif

#ifdef G_PASTE_ENABLE_SQLITE
/* The history is in the flavour storage-backend names, once that was chosen: a
 * copy an earlier migration left in another -- the old data is deleted only when
 * asked -- is not it, whatever that flavour's precedence. */
static void
test_migration_detects_backend_in_use (void)
{
    if (g_test_subprocess ())
    {
        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        const gchar *name = "detects-backend-in-use";

        g_paste_settings_set_history_name (settings, name);

        {
            g_autoptr (GPasteStorageBackend) stale = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
            g_autoptr (GPasteStorageBackend) in_use = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
            g_autolist (GPasteItem) old_items = g_list_prepend (NULL, g_paste_text_item_new ("stale"));
            g_autolist (GPasteItem) items = g_list_prepend (NULL, g_paste_text_item_new ("recent"));

            g_paste_storage_backend_write_history (stale, name, old_items);
            g_paste_storage_backend_write_history (in_use, name, items);
        }

        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

        /* "Keep as is". */
        prompt->chosen = G_PASTE_STORAGE_FILE;
        g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
        run_migration (settings, prompt);
        g_assert_cmpint (prompt->current, ==, G_PASTE_STORAGE_FILE);

        g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*");
}
#endif

/* "None" chosen with the old data kept is still the backend in use: the history
 * that flavour left on disk is what the user kept, not one to go back to. */
static void
test_migration_none_is_current (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        const gchar *name = "none-kept-old-data";

        g_paste_settings_set_history_name (settings, name);

        {
            g_autoptr (GPasteStorageBackend) old = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
            g_autolist (GPasteItem) items = g_list_prepend (NULL, g_paste_text_item_new ("kept on disk"));

            g_paste_storage_backend_write_history (old, name, items);
        }

        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

        /* "Keep as is". */
        prompt->chosen = G_PASTE_STORAGE_NOOP;
        g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_NOOP);
        run_migration (settings, prompt);
        g_assert_cmpint (prompt->current, ==, G_PASTE_STORAGE_NOOP);
        g_assert_cmpint (g_paste_settings_get_storage_backend (settings), ==, G_PASTE_STORAGE_NOOP);

        g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
        g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*");
    g_test_trap_assert_stderr_unmatched ("*WARNING*");
    g_test_trap_assert_stderr_unmatched ("*CRITICAL*");
}

/* A configured history name that names no history is the default one to the
 * detection, as it is to the history the daemon then loads, rather than a path
 * handed on regardless. */
static void
test_migration_invalid_history_name (void)
{
    if (g_test_subprocess ())
    {
        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        g_autoptr (TestMigrationPrompt) prompt = g_object_new (TEST_TYPE_MIGRATION_PROMPT, NULL);

        g_paste_settings_set_history_name (settings, "../elsewhere");
        prompt->chosen = G_PASTE_STORAGE_FILE;
        run_migration (settings, prompt);
        g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*");
}

/* Read a history back through @backend, handing back an owned list: the size
 * out-parameter no test asserts on, the by-hand free and the empty-list reset
 * the read insists on all stay here rather than in every caller.
 *
 * Whether the backend could read at all is distinct from what it read: an empty
 * history is a perfectly good thing to read back, which is what the two
 * wrappers below are about. */
static GList *
read_history_full (GPasteStorageBackend *backend,
                   const gchar          *name,
                   gboolean             *ok)
{
    GList *loaded = NULL;
    gsize size = 0;
    gboolean read = g_paste_storage_backend_read_history (backend, name, NULL /* cancellable */, &loaded, &size);

    if (ok)
        *ok = read;

    return loaded;
}

static GList *
read_history (GPasteStorageBackend *backend,
              const gchar          *name)
{
    return read_history_full (backend, name, NULL);
}

/* Same, for the tests that need the read itself to have worked. */
static GList *
read_history_ok (GPasteStorageBackend *backend,
                 const gchar          *name)
{
    gboolean ok = FALSE;
    GList *loaded = read_history_full (backend, name, &ok);

    g_assert_true (ok);

    return loaded;
}

/* The history is on disk but must not be read back: a newer schema, a wrong
 * passphrase, a file we cannot decrypt. Refused means both -- the backend says
 * so, and nothing comes out of it. */
static void
assert_history_refused (GPasteStorageBackend *backend,
                        const gchar          *name)
{
    gboolean ok = TRUE;
    g_autolist (GPasteItem) loaded = read_history_full (backend, name, &ok);

    g_assert_false (ok);
    g_assert_null (loaded);
}

/* A real (1x1 transparent) PNG for the image item tests. Only the SQLite ones
 * take it as-is; the others build a distinguishable one with the colored
 * variant below. */
#ifdef G_PASTE_ENABLE_SQLITE
static GBytes *
test_png_bytes (void)
{
    gsize length = 0;
    guchar *data = g_base64_decode ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==",
                                    &length);

    return g_bytes_new_take (data, length);
}
#endif

/* A distinct 1x1 PNG per color. Images are content-addressed under a shared
 * images/ dir, so tests asserting on their cache file's (non-)existence must
 * not share pixels with each other. */
static GBytes *
test_png_bytes_colored (guchar r,
                        guchar g,
                        guchar b)
{
    const guchar pixel[4] = { r, g, b, 0xff };
    g_autoptr (GBytes) data = g_bytes_new (pixel, sizeof (pixel));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, data, 4);

    return gdk_texture_save_to_png_bytes (texture);
}

/* Scan a file's raw bytes for a plaintext needle. Binary-safe: g_strstr_len
 * stops at embedded NULs, which a database file is full of. */
static gboolean G_GNUC_UNUSED
file_contains (const gchar *path,
               const gchar *needle)
{
    g_autofree gchar *raw = NULL;
    gsize raw_len = 0;

    if (!g_file_get_contents (path, &raw, &raw_len, NULL))
        return FALSE;

    gsize needle_len = strlen (needle);

    if (raw_len < needle_len)
        return FALSE;

    for (gsize i = 0; i + needle_len <= raw_len; ++i)
    {
        if (!memcmp (raw + i, needle, needle_len))
            return TRUE;
    }

    return FALSE;
}

/* Capturing an image touches no file: the item carries its encoded PNG, and
 * *materializing* it is the storage backend's job. */
static void
test_image_capture_does_not_write (void)
{
    g_autoptr (GBytes) png = test_png_bytes_colored (1, 2, 3);
    g_autoptr (GError) error = NULL;
    g_autoptr (GdkTexture) texture = gdk_texture_new_from_bytes (png, &error);

    g_assert_nonnull (texture);

    g_autoptr (GPasteItem) item = g_paste_image_item_new (texture);

    g_assert_nonnull (item);
    g_assert_nonnull (g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (item)));
    g_assert_false (g_file_test (g_paste_item_get_value (item), G_FILE_TEST_EXISTS));
}

/* The plain file backend materializes an image's cache file from the item
 * bytes when writing its XML, and reads it back by path -- the item itself
 * knowing only its checksum until a backend hands it a file. */
static void
test_file_image_materialization (void)
{
    const gchar *name = "file-image";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes_colored (4, 5, 6);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    GList *items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));

    g_assert_nonnull (items->data);

    /* Captured, not persisted: its value says which image it is, and no file
     * holds it yet. */
    const gchar *checksum = g_paste_image_item_get_checksum (items->data);

    g_assert_cmpstr (g_paste_item_get_value (items->data), ==, checksum);
    g_assert_null (g_paste_image_item_get_cache_path (items->data));

    g_autofree gchar *cache_path = g_paste_file_backend_image_path (name, checksum);

    g_assert_false (g_file_test (cache_path, G_FILE_TEST_EXISTS));

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

    g_paste_storage_backend_write_history (backend, name, items);

    /* The write materialized the referenced file, byte-identical to the item. */
    g_autofree gchar *materialized = NULL;
    gsize materialized_len = 0;

    g_assert_true (g_file_get_contents (cache_path, &materialized, &materialized_len, NULL));
    g_assert_cmpmem (materialized, materialized_len,
                     g_bytes_get_data (png, NULL), g_bytes_get_size (png));

    g_autolist (GPasteItem) loaded = read_history (backend, name);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpint (g_paste_item_get_kind (loaded->data), ==, G_PASTE_ITEM_KIND_IMAGE);
    g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, checksum);
    /* Read back off that file, and it says so. */
    g_assert_cmpstr (g_paste_image_item_get_cache_path (loaded->data), ==, cache_path);

    g_list_free_full (items, g_object_unref);
}

/* A captured image names no file: its value is its checksum, and where its
 * bytes end up is the storage backend's to decide when it persists them. */
static void
test_history_image_names_no_file (void)
{
    g_autoptr (GPasteHistory) history = make_plain_history ();

    g_paste_history_load (history, "capture-test");

    g_autoptr (GBytes) png = test_png_bytes_colored (13, 14, 15);
    g_autoptr (GError) error = NULL;
    g_autoptr (GdkTexture) texture = gdk_texture_new_from_bytes (png, &error);

    g_assert_nonnull (texture);

    g_paste_history_add (history, g_paste_image_item_new (texture));

    GPasteItem *item = g_paste_history_get (history, 0);
    const gchar *checksum = g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item));

    g_assert_nonnull (checksum);
    g_assert_cmpstr (g_paste_item_get_value (item), ==, checksum);
    g_assert_null (g_paste_image_item_get_cache_path (G_PASTE_IMAGE_ITEM (item)));
}

/* Deleting a history takes the temporary a save cut short between its write and
 * its move left beside it: the whole history, in clear, which no listing shows
 * and nothing else would ever remove. */
static void
test_file_delete_takes_temporary (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("saved"));
    g_autofree gchar *document = g_paste_util_get_history_file_path ("delete-temporary", "xml");
    g_autofree gchar *temporary = g_strconcat (document, ".tmp", NULL);
    g_autoptr (GError) error = NULL;

    g_paste_storage_backend_write_history (backend, "delete-temporary", items);
    g_assert_true (g_file_test (document, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_set_contents (temporary, "a save cut short", -1, NULL));
    g_paste_storage_backend_delete_history (backend, "delete-temporary", &error);
    g_assert_no_error (error);
    g_assert_false (g_file_test (document, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (temporary, G_FILE_TEST_EXISTS));
}

static void
on_emptied (GObject      *source G_GNUC_UNUSED,
            GAsyncResult *result,
            gpointer      user_data)
{
    g_set_object ((GAsyncResult **) user_data, result);
}

/* EmptyHistory, waited for as its D-Bus answer is. */
static void
empty_history (const GPasteDaemonMethods *methods,
               const gchar               *name,
               GError                   **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_daemon_methods_empty_history_async (methods, name, on_emptied, &result);
    while (!result)
        g_main_context_iteration (NULL, TRUE);
    g_paste_daemon_methods_empty_history_finish (result, error);
}

/* Emptying a history this daemon does not hold writes an empty one with its
 * own backend, and leaves alone the images a file flavour still storing that
 * name reads: the old data a migration kept, say. With no storage in use,
 * nothing is written, which is no emptying to answer for. */
static void
test_empty_other_keeps_stored_images (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes_colored (19, 20, 21);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));
    g_autofree gchar *path = g_paste_file_backend_image_path ("kept-flavour", g_paste_image_item_get_checksum (items->data));

    {
        g_autoptr (GPasteStorageBackend) plain = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

        g_paste_storage_backend_write_history (plain, "kept-flavour", items);
    }

    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();

    /* Each flavour the build has emptying it in turn, the encrypted file one
     * writing its own store beside the plain one rather than over it. */
    static const GPasteStorage emptying[] = {
#ifdef G_PASTE_ENABLE_ENCRYPTION
        G_PASTE_STORAGE_ENCRYPTED_FILE,
#endif
#ifdef G_PASTE_ENABLE_SQLITE
        G_PASTE_STORAGE_SQLITE,
#endif
        G_PASTE_N_STORAGE,
    };

#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_paste_storage_backend_set_passphrase ("kept-flavour passphrase");
#endif
    for (const GPasteStorage *kind = emptying; *kind != G_PASTE_N_STORAGE; ++kind)
    {
        g_paste_settings_set_storage_backend (settings, *kind);

        g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
        const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
        g_autoptr (GError) error = NULL;

        g_paste_history_load (history, "empty-other-current");
        empty_history (&methods, "kept-flavour", &error);
        g_assert_no_error (error);
        g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
        g_paste_history_flush (history);
    }
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_paste_storage_backend_set_passphrase (NULL);
#endif

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_NOOP);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };
    g_autoptr (GError) error = NULL;

    g_paste_history_load (history, "empty-other-current");
    empty_history (&methods, "kept-flavour", &error);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

/* With no storage in use, a backup is no copy made: refused rather than
 * answered for. So are the deletion and the emptying of a history another
 * flavour still stores -- an encrypted store left locked, or what a migration
 * kept -- which the "no storage" backend cannot reach, whichever flavour that
 * is and whether or not it is the current history: the store stays, and so
 * does the session's model. With none stored, the
 * model is all there is to empty. */
static void
test_noop_refuses_unmade (void)
{
    static const GPasteStorage stored[] = {
        G_PASTE_STORAGE_FILE,
        G_PASTE_STORAGE_ENCRYPTED_FILE,
        G_PASTE_STORAGE_SQLITE,
        G_PASTE_STORAGE_ENCRYPTED_SQLITE,
    };
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_NOOP);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (GPasteDaemon3) skeleton = g_paste_daemon3_skeleton_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .skeleton = skeleton };

    g_paste_history_load (history, "noop-kept");
    g_paste_history_add (history, g_paste_text_item_new ("this session"));

    {
        g_autoptr (GError) error = NULL;

        g_paste_daemon_methods_backup_history (&methods, "noop-kept", "noop-copy", &error);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    }

    g_assert_true (g_paste_util_ensure_history_dir_exists ());

    /* Only whether it is there counts, not what it holds. */
    for (gsize i = 0; i < G_N_ELEMENTS (stored); ++i)
    {
        g_autofree gchar *kept = g_paste_util_get_history_file_path ("noop-kept", g_paste_storage_get_extension (stored[i]));
        g_autofree gchar *other = g_paste_util_get_history_file_path ("noop-other", g_paste_storage_get_extension (stored[i]));
        g_autoptr (GError) delete_error = NULL;
        g_autoptr (GError) other_error = NULL;
        g_autoptr (GError) empty_error = NULL;

        g_assert_true (g_file_set_contents (kept, "kept", -1, NULL));
        g_assert_true (g_file_set_contents (other, "kept", -1, NULL));

        g_paste_daemon_methods_delete_history (&methods, "noop-kept", &delete_error);
        g_assert_error (delete_error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
        empty_history (&methods, "noop-kept", &empty_error);
        g_assert_error (empty_error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
        g_assert_true (g_file_test (kept, G_FILE_TEST_EXISTS));
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
        g_paste_daemon_methods_delete_history (&methods, "noop-other", &other_error);
        g_assert_error (other_error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
        g_assert_true (g_file_test (other, G_FILE_TEST_EXISTS));

        g_assert_cmpint (g_unlink (kept), ==, 0);
        g_assert_cmpint (g_unlink (other), ==, 0);
    }

    g_autoptr (GError) error = NULL;

    empty_history (&methods, "noop-kept", &error);
    g_assert_no_error (error);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
}

/* The same image copied in two histories gets one file each: evicting or
 * deleting it from one history must never break the other. */
static void
test_file_image_per_history (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes_colored (16, 17, 18);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    GList *items_a = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));
    GList *items_b = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));

    g_assert_nonnull (items_a->data);
    g_assert_nonnull (items_b->data);

    g_autofree gchar *path_a = g_paste_file_backend_image_path ("per-history-a",
                                                                g_paste_image_item_get_checksum (items_a->data));
    g_autofree gchar *path_b = g_paste_file_backend_image_path ("per-history-b",
                                                                g_paste_image_item_get_checksum (items_b->data));

    /* The same pixels in two histories: one file each. */
    g_assert_cmpstr (path_a, !=, path_b);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

    g_paste_storage_backend_write_history (backend, "per-history-a", items_a);
    g_paste_storage_backend_write_history (backend, "per-history-b", items_b);

    g_assert_true (g_file_test (path_a, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (path_b, G_FILE_TEST_EXISTS));

    /* Deleting one history takes its images directory with it and leaves the
     * other history's copy alone. */
    g_paste_storage_backend_delete_history (backend, "per-history-a", NULL);

    g_assert_false (g_file_test (path_a, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (path_b, G_FILE_TEST_EXISTS));

    g_autolist (GPasteItem) loaded = read_history (backend, "per-history-b");

    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpint (g_paste_item_get_kind (loaded->data), ==, G_PASTE_ITEM_KIND_IMAGE);

    g_list_free_full (items_a, g_object_unref);
    g_list_free_full (items_b, g_object_unref);
}

/* Backing a history up writes the *same* items under another name: the backup
 * must own copies of the images instead of referencing the source's files,
 * while the items themselves stay anchored to their source history. */
static void
test_file_backup_owns_images (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes_colored (19, 20, 21);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    GList *items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));

    g_assert_nonnull (items->data);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

    g_paste_storage_backend_write_history (backend, "backup-src", items);
    /* What g_paste_history_save does for BackupHistory: same items, new name. */
    g_paste_storage_backend_write_history (backend, "backup-dst", items);

    /* The backup got its own copy, under its own name. */
    const gchar *checksum = g_paste_image_item_get_checksum (items->data);
    g_autofree gchar *src_path = g_paste_file_backend_image_path ("backup-src", checksum);
    g_autofree gchar *dst_path = g_paste_file_backend_image_path ("backup-dst", checksum);

    g_assert_true (g_file_test (src_path, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (dst_path, G_FILE_TEST_EXISTS));

    /* Deleting the source history leaves the backup fully readable. */
    g_paste_storage_backend_delete_history (backend, "backup-src", NULL);

    g_assert_false (g_file_test (src_path, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (dst_path, G_FILE_TEST_EXISTS));

    g_autolist (GPasteItem) loaded = read_history (backend, "backup-dst");

    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpint (g_paste_item_get_kind (loaded->data), ==, G_PASTE_ITEM_KIND_IMAGE);
    g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, checksum);
    /* Read out of the backup's own copy, not the source's. */
    g_assert_cmpstr (g_paste_image_item_get_cache_path (loaded->data), ==, dst_path);

    g_list_free_full (items, g_object_unref);
}

/* Dropping an image from a history takes the file the backend materialized for
 * it under the history's own images directory, once the write the removal
 * records has landed: that snapshot no longer references it, and the file
 * flavours sweep what theirs do not. */
static void
test_file_eviction_deletes_image (void)
{
    const gchar *name = "evict-image";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_settings_set_max_history_size (settings, 5);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);

    g_autoptr (GBytes) png = test_png_bytes_colored (25, 26, 27);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));

    g_assert_nonnull (items->data);

    g_autofree gchar *image_path = g_paste_file_backend_image_path (name,
                                                                    g_paste_image_item_get_checksum (items->data));

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

        g_paste_storage_backend_write_history (backend, name, items);
    }

    g_assert_true (g_file_test (image_path, G_FILE_TEST_EXISTS));

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, name);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);

    /* Nothing is checked between the removal and the flush: the saver may land
     * the write, and sweep, before the next line runs. That the file outlives
     * a write still in flight is /history-ordering/remove-image-during-write's
     * business, which holds the write to look. */
    g_paste_history_remove (history, 0);
    g_paste_history_flush (history);

    g_assert_false (g_file_test (image_path, G_FILE_TEST_EXISTS));
}

/* Write @n_texts text items followed by an image to @name through the file
 * backend, and hand back the path of the image's file. */
static gchar *
write_texts_and_image (GPasteSettings *settings,
                       const gchar    *name,
                       guint           n_texts,
                       guchar          shade)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autoptr (GBytes) png = test_png_bytes_colored (shade, shade, shade);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    g_autolist (GPasteItem) items = NULL;

    for (guint i = 0; i < n_texts; ++i)
    {
        g_autofree gchar *value = g_strdup_printf ("text %u", i);

        items = g_list_append (items, g_paste_text_item_new (value));
    }

    GPasteItem *image = g_paste_image_item_new_from_bytes (png, date, NULL);

    g_assert_nonnull (image);
    items = g_list_append (items, image);
    g_paste_storage_backend_write_history (backend, name, items);

    gchar *path = g_paste_file_backend_image_path (name, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (image)));

    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    return path;
}

/* An image evicted while the history is flushed keeps its file: the store,
 * which a flushed history does not write, still references it. Once recording
 * resumes, the next save sweeps it. */
static void
test_file_flushed_eviction_keeps_image (void)
{
    const gchar *name = "flushed-evict-image";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_settings_set_max_history_size (settings, 5);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);

    g_autofree gchar *image_path = write_texts_and_image (settings, name, 4, 31);
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, name);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 5);

    g_paste_history_flush (history);
    g_paste_history_add (history, g_paste_text_item_new ("evicts the image"));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 5);
    /* Again, for any write the add recorded to have landed, sweep and all. */
    g_paste_history_flush (history);
    g_assert_true (g_file_test (image_path, G_FILE_TEST_EXISTS));

    g_paste_history_resume (history);
    g_paste_history_add (history, g_paste_text_item_new ("saved"));
    g_paste_history_flush (history);
    g_assert_false (g_file_test (image_path, G_FILE_TEST_EXISTS));
}

/* An unreadable history is never written, so nothing sweeps the files its store
 * still references: the same image copied into it in the meantime, then
 * deleted, leaves the file it names alone. */
static void
test_file_unreadable_keeps_image (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "unreadable-image";
        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        g_paste_settings_set_images_support (settings, TRUE);

        g_autofree gchar *image_path = write_texts_and_image (settings, name, 0, 32);
        g_autofree gchar *history_path = g_paste_util_get_history_file_path (name, "xml");
        g_autofree gchar *contents = NULL;

        /* A version this GPaste does not read: the history is unreadable. */
        g_assert_true (g_file_get_contents (history_path, &contents, NULL, NULL));

        gchar *version = strstr (contents, "version=\"2.0\"");

        g_assert_nonnull (version);
        version[9] = '9';
        g_assert_true (g_file_set_contents (history_path, contents, -1, NULL));

        g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
        g_autoptr (GBytes) png = test_png_bytes_colored (32, 32, 32);
        g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);

        g_paste_history_load (history, name);
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
        g_paste_history_add (history, g_paste_image_item_new_from_bytes (png, date, NULL));
        g_paste_history_remove (history, 0);
        g_paste_history_flush (history);

        g_assert_true (g_file_test (image_path, G_FILE_TEST_EXISTS));
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*version*");
}

/* A read that skips a stored image -- images turned off, or an image stored
 * without a date -- or that stops short leaves its file alone: a read is no
 * write, and the history it read still references the file. Turned back on,
 * the image is read again. */
static void
test_file_skipped_image_kept_by_reads (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

        g_paste_settings_set_images_support (settings, TRUE);

        g_autofree gchar *filtered = write_texts_and_image (settings, "filtered-image", 1, 33);
        g_autofree gchar *truncated = write_texts_and_image (settings, "truncated-image", 1, 34);
        g_autofree gchar *undated = write_texts_and_image (settings, "undated-image", 1, 35);

        {
            g_autofree gchar *path = g_paste_util_get_history_file_path ("truncated-image", "xml");
            g_autofree gchar *contents = NULL;

            g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));

            gchar *footer = strstr (contents, "</history>");

            g_assert_nonnull (footer);
            g_assert_true (g_file_set_contents (path, contents, footer - contents, NULL));
        }

        {
            g_autofree gchar *path = g_paste_util_get_history_file_path ("undated-image", "xml");
            g_autofree gchar *contents = NULL;

            g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));

            gchar *date = strstr (contents, " date=\"");

            g_assert_nonnull (date);
            /* An attribute this version ignores, in place of the date. */
            memcpy (date + 1, "data", 4);
            g_assert_true (g_file_set_contents (path, contents, -1, NULL));
        }

        g_paste_settings_set_images_support (settings, FALSE);
        {
            g_autolist (GPasteItem) filtered_items = read_history_ok (backend, "filtered-image");

            g_assert_cmpuint (g_list_length (filtered_items), ==, 1);
        }

        {
            g_autolist (GPasteItem) partial = NULL;
            gsize size;

            g_assert_false (g_paste_storage_backend_read_history (backend, "truncated-image", NULL, &partial, &size));
        }

        g_paste_settings_set_images_support (settings, TRUE);
        /* Read with images on, so that its missing date is all that skips it. */
        {
            g_autolist (GPasteItem) undated_items = read_history_ok (backend, "undated-image");

            g_assert_cmpuint (g_list_length (undated_items), ==, 1);
        }

        g_assert_true (g_file_test (filtered, G_FILE_TEST_EXISTS));
        g_assert_true (g_file_test (truncated, G_FILE_TEST_EXISTS));
        g_assert_true (g_file_test (undated, G_FILE_TEST_EXISTS));

        g_autolist (GPasteItem) loaded = read_history_ok (backend, "filtered-image");

        g_assert_cmpuint (g_list_length (loaded), ==, 2);
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*Failed to parse history file*truncated-image.xml*");
}

/* With images turned off, a load skips the stored image, and the next save of
 * the history, which no longer references it, takes its file. */
static void
test_file_skipped_image_swept_by_save (void)
{
    const gchar *name = "skipped-image-swept";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);

    g_autofree gchar *image_path = write_texts_and_image (settings, name, 1, 36);
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_settings_set_images_support (settings, FALSE);
    g_paste_history_load (history, name);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_true (g_file_test (image_path, G_FILE_TEST_EXISTS));

    g_paste_history_add (history, g_paste_text_item_new ("saved"));
    g_paste_history_flush (history);
    g_assert_false (g_file_test (image_path, G_FILE_TEST_EXISTS));
}

/* What a full write sweeps is its own: under its history's images directory,
 * the files of its own flavour named after a checksum, which the snapshot does
 * not reference, and what a crash left of either flavour's image writes. Another
 * history's directory, the other file flavour's images of the same history, and
 * any file named otherwise stay, as does everything a database write leaves. */
static void
test_file_sweep_bounds (void)
{
    if (g_test_subprocess ())
    {
        const gchar *name = "sweep-bounds";
        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        g_paste_settings_set_images_support (settings, TRUE);

        g_autofree gchar *kept = write_texts_and_image (settings, name, 0, 37);
        g_autofree gchar *other = write_texts_and_image (settings, "sweep-bounds-other", 0, 38);
        g_autofree gchar *images = g_paste_file_backend_images_dir (name);
        g_autofree gchar *foreign = g_build_filename (images, "Screenshot.png", NULL);
        g_autofree gchar *encrypted_orphan = g_build_filename (images, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.pngs", NULL);
        g_autofree gchar *orphan = g_build_filename (images, "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210.png", NULL);
        g_autofree gchar *other_orphan = g_strconcat (other, ".bak", NULL);
        g_autofree gchar *plain_temporary = g_strconcat (orphan, ".tmp", NULL);
        g_autofree gchar *encrypted_temporary = g_build_filename (images, "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff.pngs.tmp", NULL);
        g_autofree gchar *foreign_temporary = g_strconcat (foreign, ".tmp", NULL);

        g_assert_true (g_file_set_contents (plain_temporary, "a screenshot in clear", -1, NULL));
        g_assert_true (g_file_set_contents (encrypted_temporary, "cut short", -1, NULL));
        g_assert_true (g_file_set_contents (foreign_temporary, "not ours", -1, NULL));
        g_assert_true (g_file_set_contents (foreign, "not ours", -1, NULL));
        g_assert_true (g_file_set_contents (encrypted_orphan, "the other flavour's", -1, NULL));
        g_assert_true (g_file_set_contents (orphan, "nobody's", -1, NULL));
        g_assert_true (g_file_set_contents (other_orphan, "another history's", -1, NULL));

#ifdef G_PASTE_ENABLE_SQLITE
        {
            g_autoptr (GPasteStorageBackend) database = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

            g_paste_storage_backend_write_history (database, name, NULL);
            g_assert_true (g_file_test (orphan, G_FILE_TEST_EXISTS));
        }
#endif

        {
            g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
            g_autolist (GPasteItem) items = read_history_ok (file, name);

            g_paste_storage_backend_write_history (file, name, items);
        }

        g_assert_true (g_file_test (kept, G_FILE_TEST_EXISTS));
        g_assert_false (g_file_test (orphan, G_FILE_TEST_EXISTS));
        g_assert_false (g_file_test (plain_temporary, G_FILE_TEST_EXISTS));
        g_assert_false (g_file_test (encrypted_temporary, G_FILE_TEST_EXISTS));
        g_assert_true (g_file_test (foreign_temporary, G_FILE_TEST_EXISTS));
        g_assert_true (g_file_test (encrypted_orphan, G_FILE_TEST_EXISTS));
        g_assert_true (g_file_test (foreign, G_FILE_TEST_EXISTS));
        g_assert_true (g_file_test (other, G_FILE_TEST_EXISTS));
        g_assert_true (g_file_test (other_orphan, G_FILE_TEST_EXISTS));

#ifdef G_PASTE_ENABLE_ENCRYPTION
        {
            g_autoptr (GPasteStorageBackend) encrypted = g_paste_file_backend_new_encrypted (settings, "passphrase");

            g_paste_storage_backend_write_history (encrypted, name, NULL);
        }

        g_assert_false (g_file_test (encrypted_orphan, G_FILE_TEST_EXISTS));
        g_assert_true (g_file_test (kept, G_FILE_TEST_EXISTS));
        g_assert_true (g_file_test (foreign, G_FILE_TEST_EXISTS));
#endif
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*");
}

/* The images GPaste 50 kept in one directory shared by every history go at
 * startup once no history references them, whichever spelling of the data
 * directory the reference has. A history it wrote keeps its own until it is
 * written again, which references a copy in its own directory. */
static void
test_file_legacy_images_sweep (void)
{
    const gchar *name = "legacy-images";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes_colored (42, 43, 44);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    g_autoptr (GPasteItem) image = g_paste_image_item_new_from_bytes (png, date, NULL);
    const gchar *checksum = g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (image));
    g_autoptr (GBytes) aliased_png = test_png_bytes_colored (45, 46, 47);
    g_autoptr (GPasteItem) aliased_image = g_paste_image_item_new_from_bytes (aliased_png, date, NULL);
    const gchar *aliased_checksum = g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (aliased_image));
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *shared = g_build_filename (history_dir, "images", NULL);
    g_autofree gchar *file_name = g_strconcat (checksum, ".png", NULL);
    g_autofree gchar *legacy = g_build_filename (shared, file_name, NULL);
    g_autofree gchar *aliased_file_name = g_strconcat (aliased_checksum, ".png", NULL);
    g_autofree gchar *aliased_legacy = g_build_filename (shared, aliased_file_name, NULL);
    /* The data directory as a symlinked home reaches it. */
    g_autofree gchar *alias = g_build_filename (g_get_user_data_dir (), "gpaste-alias", NULL);
    g_autofree gchar *aliased_reference = g_build_filename (alias, "images", aliased_file_name, NULL);
    g_autofree gchar *orphan = g_build_filename (shared, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef.png", NULL);
    g_autofree gchar *foreign = g_build_filename (shared, "Screenshot.png", NULL);
    g_autofree gchar *history = g_strdup_printf ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                                 "<history version=\"2.0\">\n"
                                                 "  <item kind=\"Image\" uuid=\"5c7a3c3e-legacy\" date=\"1234567890\" checksum=\"%s\">\n"
                                                 "    <value><![CDATA[%s]]></value>\n"
                                                 "  </item>\n"
                                                 "  <item kind=\"Image\" uuid=\"5c7a3c3e-aliased\" date=\"1234567890\" checksum=\"%s\">\n"
                                                 "    <value><![CDATA[%s]]></value>\n"
                                                 "  </item>\n"
                                                 "</history>\n",
                                                 checksum, legacy, aliased_checksum, aliased_reference);
    g_autofree gchar *history_path = g_paste_util_get_history_file_path (name, "xml");

    g_assert_cmpint (g_mkdir_with_parents (shared, 0700), ==, 0);
    g_assert_true (g_file_set_contents (legacy, g_bytes_get_data (png, NULL), (gssize) g_bytes_get_size (png), NULL));
    g_assert_true (g_file_set_contents (aliased_legacy, g_bytes_get_data (aliased_png, NULL), (gssize) g_bytes_get_size (aliased_png), NULL));
    g_assert_cmpint (symlink (history_dir, alias), ==, 0);
    g_assert_true (g_file_set_contents (orphan, "nobody's", -1, NULL));
    g_assert_true (g_file_set_contents (foreign, "not ours", -1, NULL));
    g_assert_true (g_file_set_contents (history_path, history, -1, NULL));

    g_paste_file_backend_sweep_legacy_images ();
    g_assert_true (g_file_test (legacy, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (aliased_legacy, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (orphan, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (foreign, G_FILE_TEST_EXISTS));

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_autolist (GPasteItem) items = read_history_ok (backend, name);

        g_assert_cmpuint (g_list_length (items), ==, 2);
        g_paste_storage_backend_write_history (backend, name, items);
    }

    g_paste_file_backend_sweep_legacy_images ();
    g_assert_false (g_file_test (legacy, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (aliased_legacy, G_FILE_TEST_EXISTS));
    g_assert_cmpint (g_unlink (alias), ==, 0);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) items = read_history_ok (backend, name);

    g_assert_cmpuint (g_list_length (items), ==, 2);
    g_assert_cmpstr (g_paste_image_item_get_checksum (items->data), ==, checksum);
    g_assert_cmpstr (g_paste_image_item_get_checksum (items->next->data), ==, aliased_checksum);
}

/* An image is found under its history's images directory, by the name its
 * checksum gives it, whatever path the document stored: one spelling the data
 * directory as it was reached before a home moved is no reason to drop it --
 * and its file survives the next write's sweep. */
static void
test_file_image_moved_home (void)
{
    const gchar *name = "image-moved-home";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autoptr (GBytes) png = test_png_bytes_colored (61, 62, 63);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));
    g_autofree gchar *checksum = g_strdup (g_paste_image_item_get_checksum (items->data));
    g_autofree gchar *image = g_paste_file_backend_image_path (name, checksum);
    g_autofree gchar *document = g_paste_util_get_history_file_path (name, "xml");
    g_autofree gchar *contents = NULL;
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *data_dir = g_path_get_dirname (history_dir);

    g_assert_true (g_paste_storage_backend_write_history (backend, name, items));
    g_assert_true (g_file_get_contents (document, &contents, NULL, NULL));

    g_auto (GStrv) parts = g_strsplit (contents, data_dir, -1);
    g_autofree gchar *moved = g_strjoinv ("/nonexistent/old-home", parts);

    g_assert_cmpuint (g_strv_length (parts), ==, 2);
    g_assert_true (g_file_set_contents (document, moved, -1, NULL));

    g_autolist (GPasteItem) loaded = read_history_ok (backend, name);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpstr (g_paste_image_item_get_checksum (loaded->data), ==, checksum);
    g_assert_true (g_paste_storage_backend_write_history (backend, name, loaded));
    g_assert_true (g_file_test (image, G_FILE_TEST_EXISTS));
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* An encrypted image whose side file is missing is read from the plaintext file
 * at its reference only when that file holds the pixels the history names:
 * anyone able to write the images directory could otherwise have the next save
 * encrypt pixels of their choosing as the user's own. */
static void
test_file_encrypted_fallback_verified (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "encrypted-fallback";
        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        g_paste_settings_set_images_support (settings, TRUE);

        g_autoptr (GPasteStorageBackend) encrypted = g_paste_file_backend_new_encrypted (settings, "passphrase");
        g_autoptr (GBytes) genuine = test_png_bytes_colored (48, 49, 50);
        g_autoptr (GBytes) forged = test_png_bytes_colored (51, 52, 53);
        g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new_from_bytes (genuine, date, NULL));
        g_autofree gchar *png = g_paste_file_backend_image_path (name, g_paste_image_item_get_checksum (items->data));
        g_autofree gchar *pngs = g_paste_file_backend_encrypted_path (png);

        g_paste_storage_backend_write_history (encrypted, name, items);
        g_assert_cmpint (g_unlink (pngs), ==, 0);

        g_assert_true (g_file_set_contents (png, g_bytes_get_data (forged, NULL), (gssize) g_bytes_get_size (forged), NULL));
        {
            g_autolist (GPasteItem) loaded = read_history_ok (encrypted, name);

            g_assert_null (loaded);
        }

        g_assert_true (g_file_set_contents (png, g_bytes_get_data (genuine, NULL), (gssize) g_bytes_get_size (genuine), NULL));
        {
            g_autolist (GPasteItem) loaded = read_history_ok (encrypted, name);

            g_assert_cmpuint (g_list_length (loaded), ==, 1);
        }
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*not the one its encrypted history names*");
}

/* Nor is an encrypted side file read under another image's name: it carries
 * nothing of the name, and one of the user's own encrypted images put in its
 * place would decrypt all the same. */
static void
test_file_encrypted_side_file_verified (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "encrypted-side-file";
        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        g_paste_settings_set_images_support (settings, TRUE);

        g_autoptr (GPasteStorageBackend) encrypted = g_paste_file_backend_new_encrypted (settings, "passphrase");
        g_autoptr (GBytes) kept = test_png_bytes_colored (54, 55, 56);
        g_autoptr (GBytes) swapped = test_png_bytes_colored (57, 58, 59);
        g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
        g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new_from_bytes (kept, date, NULL));

        items = g_list_append (items, g_paste_image_item_new_from_bytes (swapped, date, NULL));

        g_autofree gchar *kept_png = g_paste_file_backend_image_path (name, g_paste_image_item_get_checksum (items->data));
        g_autofree gchar *swapped_png = g_paste_file_backend_image_path (name, g_paste_image_item_get_checksum (items->next->data));
        g_autofree gchar *kept_pngs = g_paste_file_backend_encrypted_path (kept_png);
        g_autofree gchar *swapped_pngs = g_paste_file_backend_encrypted_path (swapped_png);
        g_autofree gchar *contents = NULL;
        gsize length = 0;

        g_paste_storage_backend_write_history (encrypted, name, items);
        g_assert_true (g_file_get_contents (kept_pngs, &contents, &length, NULL));
        g_assert_true (g_file_set_contents (swapped_pngs, contents, (gssize) length, NULL));

        g_autolist (GPasteItem) loaded = read_history_ok (encrypted, name);

        g_assert_cmpuint (g_list_length (loaded), ==, 1);
        g_assert_cmpstr (g_paste_image_item_get_checksum (loaded->data), ==, g_paste_image_item_get_checksum (items->data));
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*not the one its encrypted history names*");
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* An image whose pixels a round trip through PNG does not give back exactly --
 * floating point with alpha -- is read back from the encrypted flavour all the
 * same: its side file is checked against the PNG the document names, not
 * against pixels decoded and hashed anew. */
static void
test_file_encrypted_image_round_trip (void)
{
    const gchar *name = "encrypted-round-trip";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    /* As a copy offering a TIFF alone is captured: through GDK's own loader. */
    const guint width = 97, height = 61;
    g_autofree gfloat *pixels = g_new (gfloat, width * height * 4);
    g_autoptr (GRand) rand = g_rand_new_with_seed (11);

    for (guint i = 0; i < width * height * 4; ++i)
        pixels[i] = (gfloat) g_rand_double (rand);

    g_autoptr (GBytes) data = g_bytes_new (pixels, width * height * 4 * sizeof (gfloat));
    g_autoptr (GdkTexture) source = gdk_memory_texture_new (width, height, GDK_MEMORY_R32G32B32A32_FLOAT, data, width * 4 * sizeof (gfloat));
    g_autoptr (GBytes) tiff = gdk_texture_save_to_tiff_bytes (source);
    g_autoptr (GdkTexture) texture = gdk_texture_new_from_bytes (tiff, NULL);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new (texture));
    g_autofree gchar *checksum = g_strdup (g_paste_image_item_get_checksum (items->data));

    /* What makes the case: these pixels hash otherwise once through PNG. */
    {
        g_autoptr (GBytes) png = gdk_texture_save_to_png_bytes (texture);
        g_autoptr (GdkTexture) reloaded = gdk_texture_new_from_bytes (png, NULL);
        g_autofree gchar *reloaded_checksum = g_paste_image_item_compute_checksum (reloaded);

        g_assert_cmpstr (reloaded_checksum, !=, checksum);
    }

    {
        g_autoptr (GPasteStorageBackend) writer = g_paste_file_backend_new_encrypted (settings, "passphrase");

        g_assert_true (g_paste_storage_backend_write_history (writer, name, items));
    }

    /* Read by another backend, as the next start's would be. */
    g_autoptr (GPasteStorageBackend) reader = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) loaded = read_history_ok (reader, name);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpstr (g_paste_image_item_get_checksum (loaded->data), ==, checksum);
}
#endif

/* A plain history moved to the encrypted flavour the way a migration does it:
 * imported, then deleted. Its screenshots go in clear with it, their encrypted
 * side files staying -- unless a side file is missing, the plaintext then being
 * what the encrypted entry falls back on, until the next encrypted write puts
 * the side file there and sweeps it. */
static void
check_encrypting_migration_images (gboolean side_file_lost)
{
    const gchar *name = (side_file_lost) ? "encrypting-migration-lost" : "encrypting-migration";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autofree gchar *png = write_texts_and_image (settings, name, 0, (side_file_lost) ? 40 : 39);
    g_autofree gchar *pngs = g_paste_file_backend_encrypted_path (png);
    g_autoptr (GPasteStorageBackend) plain = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autoptr (GPasteStorageBackend) encrypted = g_paste_file_backend_new_encrypted (settings, "passphrase");
    g_autolist (GPasteItem) items = read_history_ok (plain, name);

    g_paste_storage_backend_write_history (encrypted, name, items);
    g_assert_true (g_file_test (pngs, G_FILE_TEST_EXISTS));
    if (side_file_lost)
        g_assert_cmpint (g_unlink (pngs), ==, 0);

    g_paste_storage_backend_delete_history (plain, name, NULL);
    g_assert_cmpint (g_file_test (png, G_FILE_TEST_EXISTS), ==, side_file_lost);

    g_autolist (GPasteItem) imported = read_history_ok (encrypted, name);

    g_assert_cmpuint (g_list_length (imported), ==, 1);
    g_paste_storage_backend_write_history (encrypted, name, imported);
    g_assert_true (g_file_test (pngs, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (png, G_FILE_TEST_EXISTS));
}

static void
test_file_encrypting_migration_images (void)
{
    check_encrypting_migration_images (FALSE);
}

static void
test_file_encrypting_migration_lost_side_file (void)
{
    check_encrypting_migration_images (TRUE);
}
#endif

#ifdef G_PASTE_ENABLE_SQLITE
/* The same eviction against a database: the image was never a file, so nothing
 * goes looking for one -- the item carries no cache path, and the backend that
 * stored it as a blob has nothing beside its store to clean up. */
static void
test_sqlite_eviction_deletes_no_image (void)
{
    const gchar *name = "sqlite-evict-image";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_settings_set_max_history_size (settings, 5);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

    g_autoptr (GBytes) png = test_png_bytes_colored (28, 29, 30);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));

    g_assert_nonnull (items->data);

    g_autofree gchar *image_path = g_paste_file_backend_image_path (name,
                                                                    g_paste_image_item_get_checksum (items->data));

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

        g_paste_storage_backend_write_history (backend, name, items);
    }

    /* Storing it wrote no file, here or anywhere. */
    g_assert_false (g_file_test (image_path, G_FILE_TEST_EXISTS));

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, name);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_null (g_paste_image_item_get_cache_path (G_PASTE_IMAGE_ITEM (g_paste_history_get (history, 0))));

    g_paste_history_remove (history, 0);

    g_assert_false (g_file_test (image_path, G_FILE_TEST_EXISTS));
}
#endif

/* GPasteClipboardContent is a tagged union, so clearing it has to leave the
 * union readable and not just mark it dead: the set_*_take() helpers assign
 * through g_set_str_take(), which reads @str to compare and free it before
 * storing. After a colour those bytes are four floats, which it would strcmp()
 * and g_free(). Every kind must therefore be able to precede every other. */
static void
test_content_kind_transitions (void)
{
    GPasteClipboardContent content = { .kind = CLIPBOARD_CONTENT_NONE };
    GdkRGBA red = { 1.0f, 0.0f, 0.0f, 1.0f };

    g_paste_clipboard_content_set_color (&content, &red);
    g_paste_clipboard_content_set_text (&content, "after a colour");
    g_assert_cmpstr (g_paste_clipboard_content_get_text (&content), ==, "after a colour");

    g_paste_clipboard_content_set_color (&content, &red);
    g_paste_clipboard_content_set_image_checksum (&content, "0badc0de");
    g_assert_cmpstr (g_paste_clipboard_content_get_image_checksum (&content), ==, "0badc0de");

    /* And the plain text -> text path, where the old string is a real one that
     * has to be freed rather than leaked. */
    g_paste_clipboard_content_set_text (&content, "one");
    g_paste_clipboard_content_set_text (&content, "two");
    g_assert_cmpstr (g_paste_clipboard_content_get_text (&content), ==, "two");

    /* Setting the same text again must survive g_set_str_take()'s equal-string
     * shortcut, which frees what it was handed and keeps what it had. */
    g_paste_clipboard_content_set_text (&content, "two");
    g_assert_cmpstr (g_paste_clipboard_content_get_text (&content), ==, "two");

    g_paste_clipboard_content_clear (&content);
    g_assert_true (g_paste_clipboard_content_is_empty (&content));
}

/* Setting an item's display string to what it already says must leave its size
 * alone. g_set_str_take() frees the string handed to it and keeps the old
 * pointer when the two compare equal, so measuring the argument rather than
 * what we ended up holding both reads freed memory and underflows the size —
 * after which the history believes it is permanently over max-memory-usage and
 * starts evicting. Reachable from D-Bus as `gpaste-client make-password <uuid>
 * foo` on a password already called foo, which is a live path: keeping your own
 * name is not a collision. */
static void
test_same_display_string_keeps_size (void)
{
    g_autoptr (GPasteItem) item = g_paste_password_item_new ("foo", "s3kr1t", 0);
    guint64 size = g_paste_item_get_size (item);

    g_assert_cmpuint (size, >, 0);

    for (guint i = 0; i < 3; ++i)
    {
        g_paste_password_item_set_name (G_PASTE_PASSWORD_ITEM (item), "foo");
        g_assert_cmpuint (g_paste_item_get_size (item), ==, size);
    }

    /* A real rename still has to move the size with it, and renaming back has
     * to land exactly where we started: the accounting is only right if it is
     * symmetric. */
    g_paste_password_item_set_name (G_PASTE_PASSWORD_ITEM (item), "foobar");
    g_assert_cmpuint (g_paste_item_get_size (item), >, size);

    g_paste_password_item_set_name (G_PASTE_PASSWORD_ITEM (item), "foo");
    g_assert_cmpuint (g_paste_item_get_size (item), ==, size);
}

static void
test_add_get_length (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);

    g_paste_history_add (history, g_paste_text_item_new ("first"));
    g_paste_history_add (history, g_paste_text_item_new ("second"));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    /* Most recently added is at the front. */
    g_assert_cmpstr (value_at (history, 0), ==, "second");
    g_assert_cmpstr (value_at (history, 1), ==, "first");
    g_assert_null (g_paste_history_get (history, 2));
}

static void
test_dedup_moves_to_front (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    g_paste_history_add (history, g_paste_text_item_new ("foo"));
    g_paste_history_add (history, g_paste_text_item_new ("bar"));
    /* Re-adding an existing (non-first) item must not duplicate it, just move
     * it back to the front. */
    g_paste_history_add (history, g_paste_text_item_new ("foo"));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (value_at (history, 0), ==, "foo");
    g_assert_cmpstr (value_at (history, 1), ==, "bar");
}

static void
test_add_equal_first_is_noop (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    g_paste_history_add (history, g_paste_text_item_new ("dup"));
    g_paste_history_add (history, g_paste_text_item_new ("dup"));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_cmpstr (value_at (history, 0), ==, "dup");
}

static void
test_size_enforcement (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    /* 5 is the schema-imposed minimum for max-history-size. */
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    for (guint i = 0; i < 7; ++i)
    {
        g_autofree gchar *text = g_strdup_printf ("item-%u", i);
        g_paste_history_add (history, g_paste_text_item_new (text));
    }

    /* Oldest entries are dropped once the configured max is exceeded. */
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 5);
    g_assert_cmpstr (value_at (history, 0), ==, "item-6");
    g_assert_cmpstr (value_at (history, 4), ==, "item-2");
}

static void
test_remove (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    g_paste_history_add (history, g_paste_text_item_new ("a"));
    g_paste_history_add (history, g_paste_text_item_new ("b"));
    g_paste_history_add (history, g_paste_text_item_new ("c"));

    /* Remove the middle entry (b is at index 1). */
    g_paste_history_remove (history, 1);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (value_at (history, 0), ==, "c");
    g_assert_cmpstr (value_at (history, 1), ==, "a");

    /* Out-of-range removal is a no-op. */
    g_paste_history_remove (history, 42);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
}

static void
test_remove_by_uuid (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    g_paste_history_add (history, g_paste_text_item_new ("x"));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (g_paste_history_get (history, 0)));
    g_paste_history_add (history, g_paste_text_item_new ("y"));

    g_assert_true (g_paste_history_remove_by_uuid (history, uuid));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_cmpstr (value_at (history, 0), ==, "y");

    /* Removing an unknown uuid reports failure and changes nothing. */
    g_assert_false (g_paste_history_remove_by_uuid (history, "does-not-exist"));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
}

static void
test_get_by_uuid (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    g_paste_history_add (history, g_paste_text_item_new ("hello"));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (g_paste_history_get (history, 0)));

    GPasteItem *item = g_paste_history_get_by_uuid (history, uuid);
    g_assert_nonnull (item);
    g_assert_cmpstr (g_paste_item_get_value (item), ==, "hello");

    g_assert_null (g_paste_history_get_by_uuid (history, "nope"));
}

/* The uuid index is a second structure alongside the item array, so it can drift
 * from it. Every operation that reshuffles the history has to leave a lookup
 * finding exactly what a walk would: removals from anywhere (including the
 * middle, which shifts every position after it), a select that moves an item to
 * the front, and a replace that swaps one item for another under a new uuid. */
static void
test_uuid_lookup_survives_reshuffling (void)
{
    const guint n_uuids = 6;
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);
    /* NULL-terminated and freed as a strv: g_autofree on an array would free
     * only its first element, since the cleanup is handed the array's address. */
    g_auto (GStrv) uuids = g_new0 (gchar *, n_uuids + 1);

    for (guint i = 0; i < n_uuids; ++i)
    {
        g_autofree gchar *value = g_strdup_printf ("item-%u", i);

        g_paste_history_add (history, g_paste_text_item_new (value));
        uuids[i] = g_strdup (g_paste_item_get_uuid (g_paste_history_get (history, 0)));
    }

    /* Newest first, so uuids[5] is at 0 and uuids[0] at 5. */
    for (guint i = 0; i < n_uuids; ++i)
    {
        GPasteItem *item = g_paste_history_get_by_uuid (history, uuids[i]);

        g_assert_nonnull (item);
        g_assert_cmpstr (g_paste_item_get_uuid (item), ==, uuids[i]);
        g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 5 - i)), ==, uuids[i]);
    }

    /* Remove from the middle: everything after it shifts down by one. */
    g_assert_true (g_paste_history_remove_by_uuid (history, uuids[3]));
    g_assert_null (g_paste_history_get_by_uuid (history, uuids[3]));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 5);

    /* Remove by position too, so both removal paths are covered. */
    g_paste_history_remove (history, 0);
    g_assert_null (g_paste_history_get_by_uuid (history, uuids[5]));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 4);

    /* Selecting the oldest survivor moves it to the front without losing it. */
    g_assert_true (g_paste_history_select (history, uuids[0]));
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 0)), ==, uuids[0]);
    g_assert_nonnull (g_paste_history_get_by_uuid (history, uuids[0]));

    /* A replace retires the old uuid and publishes the new one. */
    g_autofree gchar *replaced_uuid = g_paste_history_replace (history, uuids[0], "replaced");
    g_assert_null (g_paste_history_get_by_uuid (history, uuids[0]));

    GPasteItem *replaced = g_paste_history_get (history, 0);
    g_assert_cmpstr (g_paste_item_get_value (replaced), ==, "replaced");
    g_assert_cmpstr (g_paste_item_get_uuid (replaced), ==, replaced_uuid);
    g_assert_true (g_paste_history_get_by_uuid (history, g_paste_item_get_uuid (replaced)) == replaced);

    /* Whatever is left must still be reachable both ways, and nothing else. */
    for (guint i = 0; i < g_paste_history_get_length (history); ++i)
    {
        GPasteItem *item = g_paste_history_get (history, i);

        g_assert_true (g_paste_history_get_by_uuid (history, g_paste_item_get_uuid (item)) == item);
    }
    g_assert_null (g_paste_history_get_by_uuid (history, "not-a-uuid"));
}

/* Eviction under max-memory-usage repeatedly drops the biggest non-active item
 * and re-elects a new one. Both halves lean on the uuid index and on borrowed
 * uuid strings belonging to items being freed, so drive it hard enough that it
 * has to evict several times in a row rather than just once. */
static void
test_memory_eviction_evicts_repeatedly (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    /* 1 MiB each, and the schema floors max-memory-usage at 5 MiB, so twelve of
     * them leave plenty to evict. Each stays well under the budget on its own,
     * which is what add() rejects outright. */
    g_autofree gchar *big = g_strnfill (1024 * 1024, 'x');

    for (guint i = 0; i < 12; ++i)
    {
        g_autofree gchar *value = g_strdup_printf ("%u%s", i, big);

        g_paste_history_add (history, g_paste_text_item_new (value));
    }

    guint64 before = g_paste_history_get_length (history);
    g_assert_cmpuint (before, ==, 12);

    /* 5 MiB against ~12 MiB: several have to go, one eviction after another.
     * The first (active) item is never a candidate, so one always survives. */
    g_paste_settings_set_max_memory_usage (settings, 5);

    guint64 after = g_paste_history_get_length (history);
    g_assert_cmpuint (after, <, before);
    g_assert_cmpuint (after, >, 0);

    /* Whatever survived must still be consistent both ways. */
    for (guint i = 0; i < after; ++i)
    {
        GPasteItem *item = g_paste_history_get (history, i);

        g_assert_nonnull (item);
        g_assert_true (g_paste_history_get_by_uuid (history, g_paste_item_get_uuid (item)) == item);
    }

    /* And the history still works afterwards. */
    g_paste_history_add (history, g_paste_text_item_new ("after"));
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "after");
}

/* A uuid the tests can hand back to the history: the model owns the string, so
 * copy it before whatever is about to shuffle the items around. */
static gchar *
dup_uuid_at (GPasteHistory *history,
             guint64        index)
{
    GPasteItem *item = g_paste_history_get (history, index);

    g_assert_nonnull (item);

    return g_strdup (g_paste_item_get_uuid (item));
}

/* An image of a single colour, @side pixels square: what it weighs once decoded
 * is its pixels, while the PNG it keeps when idle is next to nothing. */
static GPasteItem *
uniform_image (guint side)
{
    gsize length = (gsize) side * side * 4;
    g_autofree guchar *pixels = g_malloc (length);

    memset (pixels, 0x80, length);

    g_autoptr (GBytes) bytes = g_bytes_new_take (g_steal_pointer (&pixels), length);
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (side, side, GDK_MEMORY_R8G8B8A8, bytes, side * 4);

    return g_paste_image_item_new (texture);
}

/* A search finds an image by what its row shows, never by the checksum its
 * value is. */
static void
test_search_image_by_what_it_shows (void)
{
    g_autoptr (GPasteHistory) history = make_history (NULL, 10);
    GPasteItem *image = uniform_image (10);
    g_autofree gchar *checksum_part = g_strndup (g_paste_item_get_value (image), 8);

    g_paste_history_add (history, image);

    g_auto (GStrv) by_checksum = g_paste_history_search (history, checksum_part);
    g_auto (GStrv) by_size = g_paste_history_search (history, "10 × 10");

    g_assert_cmpuint (g_strv_length (by_checksum), ==, 0);
    g_assert_cmpuint (g_strv_length (by_size), ==, 1);
}

/* A password is found by its name, whatever its case: its value is not what
 * anyone knows it by. A query that is no regular expression, or one too long, is
 * the caller's to hear of -- refused, not journalled -- and an escaped one finds
 * the text typed. */
static void
test_search_queries (void)
{
    g_autoptr (GPasteHistory) history = make_history (NULL, 10);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const GPasteDaemonMethods methods = { .history = history, .settings = settings };
    g_autoptr (GError) error = NULL;

    g_paste_history_add (history, g_paste_password_item_new ("MyBank", "secret", 0));
    g_paste_history_add (history, g_paste_text_item_new ("c++ (gcc"));
    g_paste_history_add (history, g_paste_text_item_new ("cxx gcc"));

    g_auto (GStrv) by_name = g_paste_history_search (history, "MyBank");
    g_auto (GStrv) by_lowercase = g_paste_history_search (history, "mybank");
    g_autofree gchar *escaped = g_regex_escape_string ("c++ (gcc", -1);
    g_auto (GStrv) literal = g_paste_history_search (history, escaped);

    g_assert_cmpuint (g_strv_length (by_name), ==, 1);
    g_assert_cmpuint (g_strv_length (by_lowercase), ==, 1);
    g_assert_cmpuint (g_strv_length (literal), ==, 1);

    /* Nor by the placeholder its value is: an asterisk typed, escaped, finds
     * no password named otherwise. */
    g_autofree gchar *asterisk = g_regex_escape_string ("*", -1);
    g_auto (GStrv) by_placeholder = g_paste_history_search (history, asterisk);

    g_assert_cmpuint (g_strv_length (by_placeholder), ==, 0);

    g_assert_null (g_paste_daemon_methods_search (&methods, "c++ (gcc", &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_autofree gchar *long_query = g_strnfill (G_PASTE_HISTORY_MAX_SEARCH_LENGTH + 1, 'a');

    g_assert_null (g_paste_daemon_methods_search (&methods, long_query, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
    g_clear_error (&error);

    g_autoptr (GVariant) found = g_paste_daemon_methods_search (&methods, escaped, &error);

    g_assert_no_error (error);
    g_assert_cmpuint (g_variant_n_children (found), ==, 1);
}

/* Merge joins text: an image's value is a checksum and a password's a
 * placeholder, so either is refused, and nothing is added. */
static void
test_merge_refuses_image_and_password (void)
{
    g_autoptr (GPasteHistory) history = make_history (NULL, 10);

    g_paste_history_add (history, g_paste_text_item_new ("text"));

    g_autofree gchar *text = dup_uuid_at (history, 0);

    g_paste_history_add (history, uniform_image (4));

    g_autofree gchar *image = dup_uuid_at (history, 0);

    g_paste_history_add (history, g_paste_password_item_new ("name", "secret", 0));

    g_autofree gchar *password = dup_uuid_at (history, 0);

    /* No clipboard manager: the refusal comes before anything is added. */
    const GPasteDaemonMethods methods = { .history = history };
    const gchar *with_image[] = { text, image, NULL };
    const gchar *with_password[] = { text, password, NULL };
    const gchar * const *merges[] = { with_image, with_password };

    for (guint i = 0; i < G_N_ELEMENTS (merges); ++i)
    {
        g_autoptr (GError) error = NULL;
        g_autofree gchar *merged = g_paste_daemon_methods_merge (&methods, "", "\n", merges[i], &error);

        g_assert_null (merged);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_WRONG_ITEM_KIND);
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);
    }
}

/* Selecting an idle image decodes it again, and the history's total has to
 * count that texture once: counted twice, the memory cap evicts entries to pay
 * for memory nothing uses -- here every one but the head, the image weighing
 * just over half the cap. */
static void
test_select_counts_image_once (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_NOOP);
    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_history_size (settings, 100);
    /* The schema's floor. */
    g_paste_settings_set_max_memory_usage (settings, 5);
    g_assert_cmpuint (g_paste_settings_get_max_memory_usage (settings), ==, 5);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "select-counts-image-once");

    /* 2.6 MB decoded against 5 MiB. */
    GPasteItem *image = uniform_image (820);
    g_autofree gchar *image_uuid = g_strdup (g_paste_item_get_uuid (image));

    g_paste_history_add (history, image);

    for (guint i = 0; i < 5; ++i)
    {
        g_autofree gchar *value = g_strdup_printf ("text %u", i);

        g_paste_history_add (history, g_paste_text_item_new (value));
    }

    g_autofree gchar *text_uuid = dup_uuid_at (history, 0);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 6);

    /* There and back again, twice: the texture is decoded and dropped each
     * time, which has to net out. */
    for (guint i = 0; i < 2; ++i)
    {
        g_assert_true (g_paste_history_select (history, image_uuid));
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 6);
        g_assert_true (g_paste_history_select (history, text_uuid));
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 6);
    }

    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_SETTING);
    g_paste_settings_reset (settings, G_PASTE_MAX_MEMORY_USAGE_SETTING);
}

/* The size cap still fires at max, but it takes the last *non-favourite*: a
 * pinned item sitting at the very bottom outlives everything added over it,
 * while the ordinary items around it keep rotating out. */
static void
test_favourite_survives_size_cap (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    g_paste_history_add (history, g_paste_text_item_new ("pinned"));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_assert_true (g_paste_history_set_favourite (history, uuid, TRUE));

    for (guint i = 0; i < 10; ++i)
    {
        g_autofree gchar *text = g_strdup_printf ("item-%u", i);
        g_paste_history_add (history, g_paste_text_item_new (text));
    }

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 5);
    g_assert_cmpstr (value_at (history, 0), ==, "item-9");
    /* Pushed to the bottom by everything newer, and still there. */
    g_assert_cmpstr (value_at (history, 4), ==, "pinned");
    g_assert_nonnull (g_paste_history_get_by_uuid (history, uuid));
}

/* Un-pinning puts the item back under the cap. Over budget with nothing else to
 * drop, the cap has been waiting for exactly one candidate. */
static void
test_unfavourite_makes_evictable (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);
    g_autofree gchar *oldest = NULL;

    /* Every one pinned, so the cap has no candidate at all and the history sits
     * above it (see test_all_favourites_grow_past_cap). */
    for (guint i = 0; i < 7; ++i)
    {
        g_autofree gchar *text = g_strdup_printf ("item-%u", i);

        g_paste_history_add (history, g_paste_text_item_new (text));

        g_autofree gchar *uuid = dup_uuid_at (history, 0);

        g_assert_true (g_paste_history_set_favourite (history, uuid, TRUE));

        if (!i)
            oldest = g_steal_pointer (&uuid);
    }

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 7);

    /* Still %TRUE: the item is gone because being evictable is what the caller
     * just asked for. */
    g_assert_true (g_paste_history_set_favourite (history, oldest, FALSE));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 6);
    g_assert_null (g_paste_history_get_by_uuid (history, oldest));
}

/* Nothing but favourites means nothing the cap may drop, so the history grows
 * past it rather than let one go. */
static void
test_all_favourites_grow_past_cap (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    for (guint i = 0; i < 8; ++i)
    {
        g_autofree gchar *text = g_strdup_printf ("item-%u", i);

        g_paste_history_add (history, g_paste_text_item_new (text));

        g_autofree gchar *uuid = dup_uuid_at (history, 0);

        g_assert_true (g_paste_history_set_favourite (history, uuid, TRUE));
    }

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 8);
}

/* Removing the head puts the item below it in its place, and the active item
 * is not the memory cap's to evict, even when it is the biggest one there is
 * -- which, idle a moment before, it was elected as. */
static void
test_memory_cap_after_head_removal (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    g_autofree gchar *large = g_strnfill (6 * 1024 * 1024, 'X');

    g_paste_history_add (history, g_paste_text_item_new ("tail"));
    g_paste_history_add (history, g_paste_text_item_new (large));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_paste_history_add (history, g_paste_text_item_new ("head"));
    g_paste_history_remove (history, 0);
    g_paste_settings_set_max_memory_usage (settings, 5);
    g_assert_cmpuint (g_paste_settings_get_max_memory_usage (settings), ==, 5);

    g_assert_nonnull (g_paste_history_get_by_uuid (history, uuid));
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 0)), ==, uuid);
}

/* A replacement weighing more than what it replaced runs the memory cap there
 * and then, and the cap evicts what it may to make room for it -- never the
 * replacement itself, here the biggest item there is, which would answer the
 * edit with its loss. */
static void
test_memory_cap_after_replacement (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    g_autofree gchar *medium = g_strnfill (2 * 1024 * 1024, 'M');
    g_autofree gchar *large = g_strnfill (4 * 1024 * 1024, 'L');

    g_paste_settings_set_max_memory_usage (settings, 5);
    g_paste_history_add (history, g_paste_text_item_new ("old"));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_paste_history_add (history, g_paste_text_item_new (medium));

    g_autofree gchar *medium_uuid = dup_uuid_at (history, 0);

    g_paste_history_add (history, g_paste_text_item_new ("head"));

    g_autofree gchar *replacement = g_paste_history_replace (history, uuid, large);

    g_assert_nonnull (replacement);
    g_assert_nonnull (g_paste_history_get_by_uuid (history, replacement));
    g_assert_null (g_paste_history_get_by_uuid (history, medium_uuid));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (g_paste_item_get_real_value (g_paste_history_get (history, 0)), ==, "head");
}

/* Contents weighing more than the cap on their own are refused, as adding them
 * is, and the item stays as it was: through the D-Bus method, that refusal is
 * .Error.Rejected, the item being still there. */
static void
test_replace_refused_over_cap (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    g_autofree gchar *huge = g_strnfill (6 * 1024 * 1024, 'H');

    g_paste_settings_set_max_memory_usage (settings, 5);
    g_paste_history_add (history, g_paste_text_item_new ("old"));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_paste_history_add (history, g_paste_text_item_new ("head"));
    g_assert_null (g_paste_history_replace (history, uuid, huge));

    const GPasteDaemonMethods methods = { .history = history };
    g_autoptr (GError) error = NULL;
    g_autofree gchar *refused = g_paste_daemon_methods_replace (&methods, uuid, huge, &error);

    g_assert_null (refused);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_REJECTED);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (g_paste_item_get_real_value (g_paste_history_get_by_uuid (history, uuid)), ==, "old");
}

/* The replacement is spared by the cap its own replace runs, and only by that
 * one: the next copy calling for an eviction takes the biggest item there is,
 * the replacement here, rather than something smaller. */
static void
test_memory_cap_after_replacement_spares_once (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    g_autofree gchar *medium = g_strnfill (1536 * 1024, 'M');
    g_autofree gchar *large = g_strnfill (2560 * 1024, 'L');
    g_autofree gchar *copied = g_strnfill (1536 * 1024, 'C');

    g_paste_settings_set_max_memory_usage (settings, 5);
    g_paste_history_add (history, g_paste_text_item_new (medium));

    g_autofree gchar *medium_uuid = dup_uuid_at (history, 0);

    g_paste_history_add (history, g_paste_text_item_new ("small"));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_paste_history_add (history, g_paste_text_item_new ("head"));

    g_autofree gchar *replacement = g_paste_history_replace (history, uuid, large);

    g_assert_nonnull (replacement);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);

    g_paste_history_add (history, g_paste_text_item_new (copied));

    g_assert_null (g_paste_history_get_by_uuid (history, replacement));
    g_assert_nonnull (g_paste_history_get_by_uuid (history, medium_uuid));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);
}

/* A favourite weighing more than max-memory-usage stays, the cap leaving it
 * alone, and selecting it makes it the head like any other: the selection it is
 * announced for is that item's. */
static void
test_select_favourite_over_cap (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    g_autofree gchar *large = g_strnfill (6 * 1024 * 1024, 'L');

    g_paste_history_add (history, g_paste_text_item_new (large));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_assert_true (g_paste_history_set_favourite (history, uuid, TRUE));
    g_paste_history_add (history, g_paste_text_item_new ("head"));
    g_paste_settings_set_max_memory_usage (settings, 5);
    g_assert_cmpuint (g_paste_settings_get_max_memory_usage (settings), ==, 5);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_true (g_paste_history_select (history, uuid));
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 0)), ==, uuid);
}

/* An image selected after the cap was lowered under its decoded size weighs
 * more than the cap once active: no eviction could bring the history under it,
 * so none is made -- rather than every other entry lost for nothing. */
static void
test_select_image_over_cap (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 20);
    /* 6.4 MB decoded, a few hundred bytes of PNG. */
    const guint width = 1600, height = 1000;
    g_autofree guchar *pixels = g_malloc0 (width * height * 4);
    g_autoptr (GBytes) data = g_bytes_new_take (g_steal_pointer (&pixels), width * height * 4);
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (width, height, GDK_MEMORY_R8G8B8A8, data, width * 4);

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_history_add (history, g_paste_image_item_new (texture));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    for (guint i = 0; i < 10; ++i)
    {
        g_autofree gchar *text = g_strdup_printf ("text %u", i);

        g_paste_history_add (history, g_paste_text_item_new (text));
    }
    g_paste_settings_set_max_memory_usage (settings, 5);
    g_assert_cmpuint (g_paste_settings_get_max_memory_usage (settings), ==, 5);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 11);

    g_assert_true (g_paste_history_select (history, uuid));
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 0)), ==, uuid);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 11);
}

/* The memory cap evicts the biggest item it *may* evict, and a favourite is
 * never one -- so the election has to skip it and take the runner-up. */
static void
test_favourite_survives_memory_cap (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    /* Biggest first, so it is the one the election would otherwise pick; it
     * ends up deepest, well away from the untracked active slot. */
    g_autofree gchar *huge = g_strnfill (4 * 1024 * 1024, 'x');

    g_paste_history_add (history, g_paste_text_item_new (huge));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_assert_true (g_paste_history_set_favourite (history, uuid, TRUE));

    g_autofree gchar *big = g_strnfill (1024 * 1024, 'y');

    for (guint i = 0; i < 8; ++i)
    {
        g_autofree gchar *value = g_strdup_printf ("%u%s", i, big);

        g_paste_history_add (history, g_paste_text_item_new (value));
    }

    /* ~12 MiB against 5: the cap evicts until it fits, and every eviction has
     * to pass over the 4 MiB favourite. */
    g_paste_settings_set_max_memory_usage (settings, 5);

    g_assert_cmpuint (g_paste_history_get_length (history), <, 9);
    g_assert_nonnull (g_paste_history_get_by_uuid (history, uuid));

    /* Un-pinning hands the cap the biggest candidate it has ever had. */
    g_assert_true (g_paste_history_set_favourite (history, uuid, FALSE));
    g_assert_null (g_paste_history_get_by_uuid (history, uuid));
}

/* The flag has to survive a save/load round trip, or pinning would only last
 * as long as the daemon does. */
static void
test_favourite_roundtrip (void)
{
    const gchar *name = "favourite-roundtrip";

    {
        g_autoptr (GPasteHistory) writer = make_plain_history ();
        g_paste_history_load (writer, name);

        g_paste_history_add (writer, g_paste_text_item_new ("pinned"));
        g_paste_history_add (writer, g_paste_text_item_new ("plain"));

        g_autofree gchar *uuid = dup_uuid_at (writer, 1);

        g_assert_true (g_paste_history_set_favourite (writer, uuid, TRUE));

        g_paste_history_flush (writer);
    }

    {
        g_autoptr (GPasteHistory) reader = make_plain_history ();
        g_paste_history_load_async (reader, name);

        g_assert_true (pump_until_length (reader, 2, 5000));
        g_assert_false (g_paste_item_is_favourite (g_paste_history_get (reader, 0)));
        g_assert_true (g_paste_item_is_favourite (g_paste_history_get (reader, 1)));
    }
}

/* Replacing an item swaps in an object built from the new contents, and the pin
 * belongs to the entry rather than to that object: editing what a pinned item
 * says, or turning it into a password, is not the user letting go of it. */
static void
test_favourite_survives_replace (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    g_paste_history_add (history, g_paste_text_item_new ("before"));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_assert_true (g_paste_history_set_favourite (history, uuid, TRUE));

    g_autofree gchar *after_uuid = g_paste_history_replace (history, uuid, "after");

    g_assert_cmpstr (value_at (history, 0), ==, "after");
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 0)), ==, after_uuid);
    g_assert_true (g_paste_item_is_favourite (g_paste_history_get (history, 0)));

    g_autofree gchar *secret_uuid = g_paste_history_make_password (history, after_uuid, "secret", 0);

    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 0)), ==, secret_uuid);
    g_assert_true (g_paste_item_is_favourite (g_paste_history_get (history, 0)));
}

/* MakePassword handed a password's own uuid keeps the entry where it stands,
 * under that uuid, rather than minting a new one: the uuid that went in comes
 * back, the name and the timeout are written and the value is not. That is
 * what Edit Password drives on every save. */
/* A name heavy enough to put a password past the memory cap is refused, nothing
 * changed, as Replace refuses contents that heavy: spared by the cap, it would
 * evict every other entry to make room for itself. */
static void
test_make_password_heavy_name (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    const GPasteDaemonMethods methods = { .history = history, .settings = settings };
    g_autofree gchar *name = g_strnfill (3 * 1024 * 1024, 'n');
    g_autoptr (GError) error = NULL;

    g_paste_settings_set_max_memory_usage (settings, 5);
    g_paste_history_add (history, g_paste_text_item_new ("kept"));
    g_paste_history_add (history, g_paste_text_item_new ("secret"));

    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (g_paste_history_get (history, 0)));

    g_assert_null (g_paste_history_make_password (history, uuid, name, 0));
    g_assert_null (g_paste_daemon_methods_make_password (&methods, uuid, name, 0, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_REJECTED);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_true (G_PASTE_IS_TEXT_ITEM (g_paste_history_get (history, 0)));
    g_paste_settings_reset (settings, G_PASTE_MAX_MEMORY_USAGE_SETTING);
}

static void
test_make_password_keeps_uuid (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    g_paste_history_add (history, g_paste_text_item_new ("s3kr1t"));

    g_autofree gchar *text_uuid = dup_uuid_at (history, 0);
    g_autofree gchar *uuid = g_paste_history_make_password (history, text_uuid, "gmail", PASSWORD_TIMEOUT);

    /* A text item is replaced, so that one is an item of its own. */
    g_assert_cmpstr (uuid, !=, text_uuid);

    g_autofree gchar *same_uuid = g_paste_history_make_password (history, uuid, "webmail", PASSWORD_TIMEOUT + 1);

    g_assert_cmpstr (same_uuid, ==, uuid);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);

    GPasteItem *password = g_paste_history_get (history, 0);

    g_assert_cmpint (g_paste_item_get_kind (password), ==, G_PASTE_ITEM_KIND_PASSWORD);
    g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (password)), ==, "webmail");
    g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (password)), ==, PASSWORD_TIMEOUT + 1);
    /* Never its value: that is the one thing this may not touch. */
    g_assert_cmpstr (g_paste_item_get_real_value (password), ==, "s3kr1t");
    g_assert_nonnull (g_paste_history_get_password (history, "webmail"));

    /* Keeping the name it already goes by is not a collision with itself: the
     * timeout may be all the caller came to change. */
    g_autofree gchar *again = g_paste_history_make_password (history, uuid, "webmail", 0);

    g_assert_cmpstr (again, ==, uuid);
    g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (g_paste_history_get (history, 0))), ==, 0);
}

/* Nameless passwords all read as G_PASTE_PASSWORD_ITEM_NO_NAME, which is no name
 * anybody picked: it is the one a history may hold several entries under. The
 * make-password shortcut, which has none to hand over, is what makes them. */
static void
test_nameless_passwords_may_coexist (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    g_paste_history_add (history, g_paste_text_item_new ("first secret"));

    g_autofree gchar *first_text = dup_uuid_at (history, 0);
    g_autofree gchar *first = g_paste_history_make_password (history, first_text, NULL, 0);

    g_assert_nonnull (first);

    g_paste_history_add (history, g_paste_text_item_new ("second secret"));

    g_autofree gchar *second_text = dup_uuid_at (history, 0);
    /* Spelled out rather than left to the %NULL coercion: a caller handing the
     * placeholder itself is asking for the same nameless password. */
    g_autofree gchar *second = g_paste_history_make_password (history, second_text, G_PASTE_PASSWORD_ITEM_NO_NAME, 0);

    g_assert_nonnull (second);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (g_paste_history_get (history, 0))), ==, G_PASTE_PASSWORD_ITEM_NO_NAME);
    g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (g_paste_history_get (history, 1))), ==, G_PASTE_PASSWORD_ITEM_NO_NAME);
}

/* The same secret read off a selection twice is one exposure the history already
 * has: a password manager copied from again produces a nameless password with
 * the same value, and telling those apart fills the history with `******` rows
 * nothing distinguishes -- the value they differ by being the one nothing may
 * display. This is the head dedup, which every tracked copy goes through. */
static void
test_nameless_passwords_with_same_value_dedup (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    g_paste_history_add (history, g_paste_password_item_new (NULL, "s3kr1t", 0));
    g_paste_history_add (history, g_paste_password_item_new (NULL, "s3kr1t", 0));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);

    /* Spelled out rather than left to the %NULL coercion, since that is what
     * `AddPassword` hands over when the client has no name to give. */
    g_paste_history_add (history, g_paste_password_item_new (G_PASTE_PASSWORD_ITEM_NO_NAME, "s3kr1t", 0));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);

    /* A different secret is a different exposure, nameless or not. */
    g_paste_history_add (history, g_paste_password_item_new (NULL, "other", 0));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
}

/* Deeper in the history than the head, which is the other dedup scan an add
 * runs: a nameless password copied again is moved back to the front rather than
 * added a second time. */
static void
test_nameless_password_dedup_past_the_head (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    g_paste_history_add (history, g_paste_password_item_new (NULL, "s3kr1t", 0));
    g_paste_history_add (history, g_paste_text_item_new ("in between"));
    g_paste_history_add (history, g_paste_password_item_new (NULL, "s3kr1t", 0));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpint (g_paste_item_get_kind (g_paste_history_get (history, 0)), ==, G_PASTE_ITEM_KIND_PASSWORD);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 1)), ==, "in between");
}

/* Selecting is the one path where two entries may be equal without being the
 * same object: a history holding two nameless passwords with one value -- one an
 * older GPaste wrote, or one made the way this test does -- has to move the entry
 * the user actually activated, rather than let the head answer for its likeness
 * and leave the row where it sat. */
static void
test_select_moves_a_password_the_head_matches (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    /* Through make_password, an add of the second one being deduped into the
     * first: what it makes is a password where a text item stood. */
    g_paste_history_add (history, g_paste_text_item_new ("s3kr1t"));

    g_autofree gchar *deeper_text = dup_uuid_at (history, 0);
    g_autofree gchar *deeper = g_paste_history_make_password (history, deeper_text, NULL, 0);

    g_paste_history_add (history, g_paste_text_item_new ("s3kr1t"));

    g_autofree gchar *head_text = dup_uuid_at (history, 0);
    g_autofree gchar *head = g_paste_history_make_password (history, head_text, NULL, 0);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 0)), ==, head);

    g_assert_true (g_paste_history_select (history, deeper));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 0)), ==, deeper);
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 1)), ==, head);
}

/* A name is the user's own record of a secret, and two records are two items
 * whatever they hold: naming one is what says it is worth keeping apart, so a
 * named password never matches -- not another entry under the same name, not the
 * nameless one holding the very same value. */
static void
test_named_passwords_never_match (void)
{
    g_autoptr (GPasteItem) named = g_paste_password_item_new ("gmail", "s3kr1t", 0);
    g_autoptr (GPasteItem) same_name = g_paste_password_item_new ("gmail", "s3kr1t", 0);
    g_autoptr (GPasteItem) nameless = g_paste_password_item_new (NULL, "s3kr1t", 0);
    g_autoptr (GPasteItem) other_nameless = g_paste_password_item_new (NULL, "s3kr1t", 0);
    g_autoptr (GPasteItem) text = g_paste_text_item_new ("s3kr1t");

    g_assert_true (g_paste_item_equals (named, named));
    g_assert_false (g_paste_item_equals (named, same_name));

    /* Symmetric, g_paste_item_equals () dispatching on the first alone. */
    g_assert_false (g_paste_item_equals (named, nameless));
    g_assert_false (g_paste_item_equals (nameless, named));

    g_assert_true (g_paste_item_equals (nameless, other_nameless));

    /* A password and a text differ in kind, however the value reads. */
    g_assert_false (g_paste_item_equals (nameless, text));
    g_assert_false (g_paste_item_equals (text, nameless));
}

/* The name is part of what a password weighs, so renaming one has to move the
 * history's own total with it. Left out, the longer name is subtracted once
 * more than it was ever added when the item goes -- and that total is unsigned,
 * so it wraps and the memory cap starts evicting everything that arrives
 * afterwards. */
static void
test_rename_keeps_history_size (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    g_paste_history_add (history, g_paste_text_item_new ("s3kr1t"));

    g_autofree gchar *text_uuid = dup_uuid_at (history, 0);
    g_autofree gchar *uuid = g_paste_history_make_password (history, text_uuid, "pw", 0);
    g_autofree gchar *renamed = g_paste_history_make_password (history, uuid, "a much longer password name", 0);

    g_assert_cmpstr (renamed, ==, uuid);

    /* The only entry goes, taking exactly what it weighs with it. */
    g_assert_true (g_paste_history_remove_by_uuid (history, uuid));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);

    /* A wrapped total is permanently over max-memory-usage, which shows as the
     * cap evicting everything but the active item. */
    g_paste_history_add (history, g_paste_text_item_new ("one"));
    g_paste_history_add (history, g_paste_text_item_new ("two"));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
}

/* Rich text is a set of alternative representations of the text an item already
 * shows, so dropping them leaves the entry itself alone: same uuid, same place,
 * same value, same pin. What does move is the history's own memory total, which
 * is the sum of what its items weigh, and with it which item the memory cap
 * would reach for next. */
static void
test_strip_rich_text (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);
    /* Eight MiB of flavours to draw the two big items from, so that what a strip
     * takes off the books is bigger than the smallest memory cap
     * "max-memory-usage" allows, five. */
    g_autofree gchar *html = g_strnfill (8 * 1024 * 1024, 'h');

    /* A pinned rich item, to be stripped for what the strip must not disturb:
     * pinning is what exempts an item from both caps, and the election never
     * looks at one, so this is the only item whose pin can be watched. */
    GPasteItem *pinned = g_paste_text_item_new ("pinned");

    g_paste_item_add_special_value (pinned, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                     g_bytes_new_static ("<p/>", 4)));
    g_paste_history_add (history, pinned);

    /* The runner-up: what the election has to find once the item above it stops
     * outweighing it. Over the cap on its own, so which of the two the cap takes
     * says whether the strip re-elected or left the old winner standing. */
    GPasteItem *runner_up = g_paste_text_item_new ("runner-up");

    g_paste_item_add_special_value (runner_up, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                        g_bytes_new (html, 6 * 1024 * 1024)));
    g_paste_history_add (history, runner_up);

    GPasteItem *rich = g_paste_text_item_new ("rich");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new (html, 8 * 1024 * 1024)));
    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_XML,
                                                                   g_bytes_new_static ("<x/>", 4)));
    g_paste_history_add (history, rich);

    /* A plain item on top, because the active slot is out of the election's
     * reach: the rich one has to be pushed out of it before its weight counts.
     */
    g_paste_history_add (history, g_paste_text_item_new ("later"));

    g_autofree gchar *pinned_uuid = dup_uuid_at (history, 3);

    g_assert_true (g_paste_history_set_favourite (history, pinned_uuid, TRUE));
    g_assert_true (g_paste_history_strip_rich_text (history, pinned_uuid, NULL));
    g_assert_true (g_paste_item_is_favourite (g_paste_history_get (history, 3)));
    g_assert_null (g_paste_item_get_special_values (g_paste_history_get (history, 3)));

    g_autofree gchar *uuid = dup_uuid_at (history, 1);

    g_assert_cmpuint (g_slist_length ((GSList *) g_paste_item_get_special_values (g_paste_history_get (history, 1))), ==, 2);

    /* A saving worker keeps this reference and may already have borrowed the
     * MIME list. Stripping must leave both valid until the worker releases it. */
    g_autoptr (GPasteItem) saving = g_paste_history_dup (history, 1);
    const GSList *saving_values = g_paste_item_get_special_values (saving);

    g_assert_true (g_paste_history_strip_rich_text (history, uuid, NULL));

    GPasteItem *stripped = g_paste_history_get (history, 1);

    g_assert_true (stripped != saving);
    g_assert_true (g_paste_item_get_special_values (saving) == saving_values);
    g_assert_cmpuint (g_slist_length ((GSList *) saving_values), ==, 2);
    g_assert_cmpuint (g_bytes_get_size (g_paste_binary_data_get_bytes (saving_values->data)), ==, 4);
    g_assert_cmpstr (g_paste_item_get_uuid (stripped), ==, uuid);
    g_assert_cmpstr (g_paste_item_get_value (stripped), ==, "rich");
    g_assert_null (g_paste_item_get_special_values (stripped));

    /* Stripping again has nothing to do and still succeeds: what a caller asked
     * for is true either way, and no listing can tell it which case it is in. A
     * uuid the history does not hold, and one naming an item of another kind,
     * are the two failures there are. */
    g_assert_true (g_paste_history_strip_rich_text (history, uuid, NULL));

    /* @found is what the daemon answers WrongItemKind rather than NOT_FOUND on:
     * a uuid nothing goes by is the one refusal that is not about the kind. */
    gboolean found = TRUE;

    g_assert_false (g_paste_history_strip_rich_text (history, "e8a95b3d-33ee-4b1e-8ee3-9c22c9635b8d", &found));
    g_assert_false (found);

    /* What it weighs is what an item of that text alone weighs: the flavours are
     * off the item, not merely out of its list. Left on, it over-reports for the
     * rest of its life and the memory cap evicts around a weight nothing is
     * holding. */
    g_autoptr (GPasteItem) plain = g_paste_text_item_new ("rich");

    g_assert_cmpuint (g_paste_item_get_size (stripped), ==, g_paste_item_get_size (plain));

    /* What came off the item came off the history's total, and the strip named
     * the runner-up as what the cap may now take. One eviction is all a cap of
     * five has to make: the runner-up's six MiB is the whole of what the history
     * still weighs. Too little off the total, or an old winner left standing,
     * and the stripped item goes with it; too much, and the total wraps -- it is
     * unsigned -- which reads as permanently over the cap and takes everything
     * the pin does not hold back. */
    g_paste_settings_set_max_memory_usage (settings, 5 /* MiB */);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 1)), ==, uuid);

    /* And the history takes items again afterwards, rather than evicting each
     * one as it arrives to pay for a weight nothing is holding. */
    g_assert_true (g_paste_history_remove_by_uuid (history, uuid));
    g_paste_history_add (history, g_paste_text_item_new ("one"));
    g_paste_history_add (history, g_paste_text_item_new ("two"));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 4);
}

/* A store sees the strip as a replace, so what comes back off disk has to have
 * lost the flavours too: an item read back still carrying them would put them
 * on the clipboard again after a restart. This one flushes, which rewrites the
 * whole history whatever happened to it; that the strip *notifies* a store at
 * all is what test_sqlite_strip_rich_text () is for. */
static void
test_strip_rich_text_persists (void)
{
    const gchar *name = "strip-rich-text";

    {
        g_autoptr (GPasteHistory) writer = make_plain_history ();
        g_paste_history_load (writer, name);

        GPasteItem *rich = g_paste_text_item_new ("rich");

        g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                       g_bytes_new_static ("<b>rich</b>", 11)));
        g_paste_history_add (writer, rich);

        g_autofree gchar *uuid = dup_uuid_at (writer, 0);

        g_assert_true (g_paste_history_strip_rich_text (writer, uuid, NULL));

        g_paste_history_flush (writer);
    }

    {
        g_autoptr (GPasteHistory) reader = make_plain_history ();
        g_paste_history_load_async (reader, name);

        g_assert_true (pump_until_length (reader, 1, 5000));
        g_assert_cmpstr (value_at (reader, 0), ==, "rich");
        g_assert_null (g_paste_item_get_special_values (g_paste_history_get (reader, 0)));
    }
}

/* Editing an item and turning one into a password drop the rich text without
 * being asked to, both of them minting an item that never had any. That is not
 * an accident to be relied on quietly: it is the reason StripRichText is the
 * only path that needed adding. */
static void
test_edit_and_make_password_drop_rich_text (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    GPasteItem *edited = g_paste_text_item_new ("edited");

    g_paste_item_add_special_value (edited, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                     g_bytes_new_static ("<b>edited</b>", 13)));
    g_paste_history_add (history, edited);

    g_autofree gchar *edited_uuid = dup_uuid_at (history, 0);
    g_autofree gchar *replaced = g_paste_history_replace (history, edited_uuid, "plain");

    g_assert_nonnull (replaced);
    g_assert_null (g_paste_item_get_special_values (g_paste_history_get (history, 0)));

    GPasteItem *secret = g_paste_text_item_new ("s3kr1t");

    g_paste_item_add_special_value (secret, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                     g_bytes_new_static ("<b>s3kr1t</b>", 13)));
    g_paste_history_add (history, secret);

    g_autofree gchar *secret_uuid = dup_uuid_at (history, 0);
    g_autofree gchar *password = g_paste_history_make_password (history, secret_uuid, "pw", 0);

    g_assert_nonnull (password);
    g_assert_null (g_paste_item_get_special_values (g_paste_history_get (history, 0)));
}

/* A line that grows as it is typed replaces the entry it grew out of, and a
 * duplicate coming back drops the older copy for the new one. Either way what
 * arrives takes a pinned entry's place, so it takes the pin too -- otherwise a
 * pinned item could be un-pinned by nothing more than retyping it. */
static void
test_favourite_survives_growing_line (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    g_paste_settings_set_growing_lines (settings, TRUE);

    g_paste_history_add (history, g_paste_text_item_new ("gro"));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_assert_true (g_paste_history_set_favourite (history, uuid, TRUE));

    g_paste_history_add (history, g_paste_text_item_new ("growing"));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_cmpstr (value_at (history, 0), ==, "growing");
    g_assert_true (g_paste_item_is_favourite (g_paste_history_get (history, 0)));
}

static void
test_favourite_survives_dedup (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 5);

    g_paste_history_add (history, g_paste_text_item_new ("pinned"));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_assert_true (g_paste_history_set_favourite (history, uuid, TRUE));

    g_paste_history_add (history, g_paste_text_item_new ("other"));
    /* The same text again: the older copy goes, the new one leads. */
    g_paste_history_add (history, g_paste_text_item_new ("pinned"));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (value_at (history, 0), ==, "pinned");
    g_assert_true (g_paste_item_is_favourite (g_paste_history_get (history, 0)));
}

/* A history stored with more items than the cap now allows -- max-history-size
 * lowered between sessions -- is trimmed as it is read rather than on the next
 * clipboard change. The backends deliberately read a favourite back whatever it
 * costs, counting only what their cap could evict, so what comes off disk can
 * sit over the cap once the favourites are counted; the caps count them, and the
 * two have to agree on what the history holds. Left to the next change, the rows
 * over the cap would show until it came and then vanish all at once.
 */
static void
test_load_applies_the_caps (void)
{
    const gchar *name = "load-capped";

    {
        g_autoptr (GPasteHistory) writer = make_plain_history_capped (100);
        g_paste_history_load (writer, name);

        for (guint i = 0; i < 8; ++i)
        {
            g_autofree gchar *text = g_strdup_printf ("item-%u", i);
            g_paste_history_add (writer, g_paste_text_item_new (text));
        }

        /* The oldest, so it sits well past the cap the reader will apply. */
        g_autofree gchar *uuid = dup_uuid_at (writer, 7);

        g_assert_true (g_paste_history_set_favourite (writer, uuid, TRUE));

        g_paste_history_flush (writer);
    }

    {
        g_autoptr (GPasteHistory) reader = make_plain_history_capped (5);

        g_paste_history_load_async (reader, name);

        /* The read admits 5 non-favourites plus the pinned one, six in all; the
         * cap then takes the last item it may, leaving five counting the pin. */
        g_assert_true (pump_until_length (reader, 5, 5000));
        g_assert_cmpstr (value_at (reader, 0), ==, "item-7");
        g_assert_cmpstr (value_at (reader, 4), ==, "item-0");
        g_assert_true (g_paste_item_is_favourite (g_paste_history_get (reader, 4)));
    }
}

/* A flushed history is a handed-over one: persistence is off, so a trim could
 * not be saved -- and it would delete the images the successor daemon's own
 * store still points at. A switch landing inside that window loads whatever is
 * on disk and leaves it alone.
 */
static void
test_load_leaves_a_flushed_history_alone (void)
{
    const gchar *name = "load-flushed";

    {
        g_autoptr (GPasteHistory) writer = make_plain_history_capped (100);
        g_paste_history_load (writer, name);

        for (guint i = 0; i < 8; ++i)
        {
            g_autofree gchar *text = g_strdup_printf ("item-%u", i);
            g_paste_history_add (writer, g_paste_text_item_new (text));
        }

        g_autofree gchar *uuid = dup_uuid_at (writer, 7);

        g_assert_true (g_paste_history_set_favourite (writer, uuid, TRUE));

        g_paste_history_flush (writer);
    }

    {
        g_autoptr (GPasteHistory) reader = make_plain_history_capped (5);

        g_paste_history_flush (reader);
        g_paste_history_load_async (reader, name);

        /* Six: the five non-favourites the read admits plus the pinned one, with
         * the cap that would have taken it to five deliberately not applied. */
        g_assert_true (pump_until_length (reader, 6, 5000));
        g_assert_true (g_paste_item_is_favourite (g_paste_history_get (reader, 5)));
    }
}

/* What a uris item holds, in the terms GetUris answers in. Any scheme reaches
 * one -- what makes a uris item is GDK deserialising the clipboard offer into a
 * GdkFileList, and text/uri-list carries smb:// and trash:// as happily as
 * file:// -- so the reply is uris rather than paths, which g_file_get_path()
 * could not have given for the last two.
 */
static void
test_uris_item_answers_its_uris (void)
{
    g_autoptr (GPasteItem) item = g_paste_uris_item_new_from_str ("file:///tmp/a\nsmb://server/share/b\ntrash:///c");

    g_assert_nonnull (item);

    g_auto (GStrv) uris = g_paste_uris_item_get_uris (G_PASTE_URIS_ITEM (item));

    g_assert_cmpuint (g_strv_length (uris), ==, 3);
    g_assert_cmpstr (uris[0], ==, "file:///tmp/a");
    g_assert_cmpstr (uris[1], ==, "smb://server/share/b");
    g_assert_cmpstr (uris[2], ==, "trash:///c");
}

static void
test_select_moves_to_front (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    g_paste_history_add (history, g_paste_text_item_new ("a"));
    g_autofree gchar *uuid_a = g_strdup (g_paste_item_get_uuid (g_paste_history_get (history, 0)));
    g_paste_history_add (history, g_paste_text_item_new ("b"));
    g_paste_history_add (history, g_paste_text_item_new ("c"));

    g_assert_true (g_paste_history_select (history, uuid_a));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);
    g_assert_cmpstr (value_at (history, 0), ==, "a");

    g_assert_false (g_paste_history_select (history, "missing"));
}

static void
test_empty (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 100);

    g_paste_history_add (history, g_paste_text_item_new ("a"));
    g_paste_history_add (history, g_paste_text_item_new ("b"));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);

    g_assert_true (g_paste_history_empty (history, NULL));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_assert_null (g_paste_history_get (history, 0));
}

static void
test_save_load_roundtrip (void)
{
    const gchar *name = "roundtrip";

    /* Write through the async saver: add two items, then flush so the
     * background write deterministically reaches disk. */
    {
        g_autoptr (GPasteHistory) writer = make_plain_history ();
        g_paste_history_load (writer, name);

        g_paste_history_add (writer, g_paste_text_item_new ("alpha"));
        g_paste_history_add (writer, g_paste_text_item_new ("beta"));

        g_paste_history_flush (writer);
    }

    /* Read it back asynchronously into a fresh history. */
    {
        g_autoptr (GPasteHistory) reader = make_plain_history ();
        g_paste_history_load_async (reader, name);

        g_assert_true (pump_until_length (reader, 2, 5000));
        g_assert_cmpstr (value_at (reader, 0), ==, "beta");
        g_assert_cmpstr (value_at (reader, 1), ==, "alpha");
    }
}

static gint
compare_entries (gconstpointer a,
                 gconstpointer b,
                 gpointer      user_data G_GNUC_UNUSED)
{
    return g_strcmp0 (*(const gchar * const *) a, *(const gchar * const *) b);
}

/* Every entry of the history directory with its size and modification time,
 * the directory's own included, so that two listings differ once anything in
 * it was written, created or removed. */
static GStrv
history_dir_listing (void)
{
    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autoptr (GDir) listing = g_dir_open (dir, 0, NULL);
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    GStatBuf st;

    if (g_stat (dir, &st) == 0)
        g_strv_builder_take (builder, g_strdup_printf (". %" G_GINT64_FORMAT ".%09ld", (gint64) st.st_mtim.tv_sec, st.st_mtim.tv_nsec));

    for (const gchar *entry; listing && (entry = g_dir_read_name (listing));)
    {
        g_autofree gchar *path = g_build_filename (dir, entry, NULL);

        if (g_stat (path, &st) == 0)
        {
            g_strv_builder_take (builder, g_strdup_printf ("%s %" G_GINT64_FORMAT " %" G_GINT64_FORMAT ".%09ld", entry,
                                                           (gint64) st.st_size, (gint64) st.st_mtim.tv_sec, st.st_mtim.tv_nsec));
        }
    }

    GStrv entries = g_strv_builder_end (builder);

    g_sort_array (entries, g_strv_length (entries), sizeof (gchar *), compare_entries, NULL);

    return entries;
}

/* A history nobody has named has no store, so its changes are kept in memory
 * and persisted nowhere: the saver files every write under a history's name.
 * Several of them in a row, and a flush applying what is queued on the spot,
 * write nothing at all. The process and data directory are this case's alone:
 * other cases' named histories can still have saves in flight while the main
 * loop is pumped. */
static void
test_unnamed_history_persists_nothing (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("");
        return;
    }

    g_autoptr (GPasteHistory) history = make_plain_history ();
    g_auto (GStrv) before = history_dir_listing ();

    g_assert_cmpuint (g_strv_length (before), ==, 0);

    for (guint i = 0; i < 3; ++i)
    {
        g_autofree gchar *value = g_strdup_printf ("unnamed %u", i);

        g_paste_history_add (history, g_paste_text_item_new (value));
    }

    g_paste_history_flush (history);
    pump_for_ms (10);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);

    g_auto (GStrv) after = history_dir_listing ();

    g_assert_cmpstrv (after, before);
}

/* g_paste_history_flush() must get every pending change to disk synchronously,
 * without the caller having to pump the main loop: an exit/handover path relies
 * on the on-disk history being complete the moment flush() returns. */
static void
test_flush_persists_synchronously (void)
{
    const gchar *name = "flush-sync";

    {
        g_autoptr (GPasteHistory) writer = make_plain_history ();
        g_paste_history_load (writer, name);

        g_paste_history_add (writer, g_paste_text_item_new ("one"));
        g_paste_history_add (writer, g_paste_text_item_new ("two"));

        /* Note: no main-loop pumping here, unlike the roundtrip test. */
        g_paste_history_flush (writer);
    }

    {
        g_autoptr (GPasteHistory) reader = make_plain_history ();
        g_paste_history_load_async (reader, name);

        g_assert_true (pump_until_length (reader, 2, 5000));
        g_assert_cmpstr (value_at (reader, 0), ==, "two");
        g_assert_cmpstr (value_at (reader, 1), ==, "one");
    }
}

/* After a flush the history stops persisting changes, so a successor daemon that
 * has taken over the on-disk state is not overwritten by our late edits. */
static void
test_flush_stops_recording (void)
{
    const gchar *name = "flush-stop";

    {
        g_autoptr (GPasteHistory) writer = make_plain_history ();
        g_paste_history_load (writer, name);

        g_paste_history_add (writer, g_paste_text_item_new ("kept"));
        g_paste_history_flush (writer);

        /* This change happens after the flush: it stays in memory but must never
         * reach the disk. Pump the loop so an (erroneous) async write would run. */
        g_paste_history_add (writer, g_paste_text_item_new ("dropped"));

        pump_for_ms (100);
    }

    {
        g_autoptr (GPasteHistory) reader = make_plain_history ();
        g_paste_history_load_async (reader, name);

        g_assert_true (pump_until_length (reader, 1, 5000));
        g_assert_cmpstr (value_at (reader, 0), ==, "kept");
    }
}

/* A delete must not race whoever owns the store now: once the history has been
 * flushed for a handover (to a successor daemon, or to a storage migration
 * rewriting it), deleting is refused outright rather than half-done. */
static void
test_delete_refused_after_flush (void)
{
    const gchar *name = "delete-after-flush";

    g_autoptr (GPasteHistory) history = make_plain_history ();
    g_paste_history_load (history, name);

    g_paste_history_add (history, g_paste_text_item_new ("kept"));
    g_paste_history_flush (history);

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xml");
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    g_autoptr (GError) error = NULL;

    g_assert_false (g_paste_history_delete (history, name, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    /* A handover that did not happen after all (a re-exec whose exec failed)
     * resumes recording, and with it the ability to delete. */
    g_paste_history_resume (history);

    g_autoptr (GError) resumed_error = NULL;

    g_assert_true (g_paste_history_delete (history, name, &resumed_error));
    g_assert_no_error (resumed_error);
    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
}

/* Version 1.0 held an item's value as text directly inside <item>; it is no
 * longer read. What matters is that dropping it did not turn an old history into
 * a *destroyable* one: it has to be refused exactly like a version from the
 * future, leaving the file byte-for-byte intact so it can still be converted
 * with an older GPaste. Silently reading it as empty would be the worst outcome,
 * since the next clipboard change would then overwrite it. */
static void
test_file_v1_refused_and_preserved (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "file-v1";
        const gchar *contents =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<history version=\"1.0\">\n"
            "  <item kind=\"Text\">ancient</item>\n"
            "  <item kind=\"Text\">also ancient</item>\n"
            "</history>\n";

        g_assert_true (g_paste_util_ensure_history_dir_exists ());

        g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xml");

        g_assert_true (g_file_set_contents (path, contents, -1, NULL));

        {
            g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
            /* Refused, not empty-but-fine. */
            assert_history_refused (backend, name);
        }

        g_autoptr (GPasteHistory) history = make_plain_history ();

        g_paste_history_load (history, name);
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);

        /* Recording is off for it, so a subsequent
         * add must not reach the file. */
        g_paste_history_add (history, g_paste_text_item_new ("usurper"));

        pump_for_ms (100);

        g_autofree gchar *raw = NULL;

        g_assert_true (g_file_get_contents (path, &raw, NULL, NULL));
        g_assert_cmpstr (raw, ==, contents);
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*History version 1.0 is no longer supported*");
    /* Nor is anything the history holds copied into the journal. */
    g_test_trap_assert_stderr_unmatched ("*ancient*");
}

/* A value holding what CDATA and the escaping itself are made of comes back as
 * it went in, through a file and an encrypted one alike. */
static void
test_file_value_round_trip (void)
{
    const gchar *value = "a ]]> b & c > d &amp; e &gt; f &&gt;";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new (value));
    g_autoptr (GPasteStorageBackend) plain = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

    g_paste_storage_backend_write_history (plain, "value-round-trip", items);
    {
        g_autolist (GPasteItem) loaded = read_history_ok (plain, "value-round-trip");

        g_assert_cmpuint (g_list_length (loaded), ==, 1);
        g_assert_cmpstr (g_paste_item_get_real_value (loaded->data), ==, value);
    }

#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_autoptr (GPasteStorageBackend) encrypted = g_paste_file_backend_new_encrypted (settings, "passphrase");

    g_paste_storage_backend_write_history (encrypted, "value-round-trip", items);
    {
        g_autolist (GPasteItem) loaded = read_history_ok (encrypted, "value-round-trip");

        g_assert_cmpuint (g_list_length (loaded), ==, 1);
        g_assert_cmpstr (g_paste_item_get_real_value (loaded->data), ==, value);
    }
#endif
}

/* What a plain history document reads as: written as is under @name, then read
 * back, its read having to be @readable, and its items' values joined by '|'. */
static gchar *
read_document (const gchar *name,
               const gchar *items,
               gboolean     readable)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xml");
    g_autofree gchar *document = g_strconcat ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<history version=\"2.0\">\n", items, "</history>\n", NULL);
    gboolean ok = FALSE;

    g_paste_settings_set_images_support (settings, TRUE);
    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    g_assert_true (g_file_set_contents (path, document, -1, NULL));

    g_autolist (GPasteItem) loaded = read_history_full (backend, name, &ok);
    g_autoptr (GStrvBuilder) values = g_strv_builder_new ();

    g_assert_cmpint (ok, ==, readable);
    for (const GList *l = loaded; l; l = l->next)
        g_strv_builder_add (values, g_paste_item_get_real_value (l->data));

    g_auto (GStrv) joined = g_strv_builder_end (values);

    return g_strjoinv ("|", joined);
}

/* A count answers what a read finds, though it builds no value: whitespace alone
 * kept as a value, the indentation around one not, an alternative with no plain
 * value beside it no item, nor an empty value. */
static void
test_file_count_matches_read (void)
{
    if (!g_test_subprocess ())
    {
        G_PASTE_TEST_TRAP_SUBPROCESS ("*no usable kind or value*");
        return;
    }

    G_PASTE_TEST_IN_SUBPROCESS;

    const gchar *name = "count-matches-read";
    g_autofree gchar *values = read_document (name,
                                              "  <item kind=\"Text\"><value><![CDATA[a]]></value></item>\n"
                                              "  <item kind=\"Text\"><value><![CDATA[\n\n]]></value></item>\n"
                                              "  <item kind=\"Text\"><value>\n    <![CDATA[indented]]>\n  </value></item>\n"
                                              "  <item kind=\"Text\"><value mime=\"text-html\"><![CDATA[PGI+YjwvYj4=]]></value></item>\n"
                                              "  <item kind=\"Text\"><value><![CDATA[rich]]></value><value mime=\"text-html\"><![CDATA[PGI+cmljaDwvYj4=]]></value></item>\n"
                                              "  <item kind=\"Text\"><value></value></item>\n",
                                              TRUE);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_auto (GStrv) read = g_strsplit (values, "|", -1);

    g_assert_cmpuint (g_strv_length (read), ==, 4);
    g_assert_cmpuint (g_paste_storage_backend_count_history (backend, name), ==, g_strv_length (read));
}

/* What the parser makes of a document no GPaste wrote. A value under a mime this
 * build does not know is dropped, not taken for the plain value; a value of
 * whitespace alone is kept, the indentation around a CDATA section is not. An
 * element out of place makes the document unreadable rather than read
 * around, which an </item> re-adding the item before it, or a nested
 * <history> closing the document early, would have been, and so does a value
 * not in UTF-8, which GMarkup reads on past without its item, or markup in a
 * value; an element this version does not know is read around, nothing in it
 * taken for the item's own. An image date
 * GDateTime cannot hold, or a checksum that is not a SHA-256, is refused as
 * data -- the checksum then recomputed -- and no history name the format
 * refuses is listed. A password timeout that is no number makes the document
 * unreadable, read as 0 being a password that never comes off the clipboard. */
static void
test_file_parser_malformed (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        g_autofree gchar *mime = read_document ("parser-mime",
                                                "  <item kind=\"Text\"><value><![CDATA[the plain text]]></value>"
                                                "<value mime=\"text-from-the-future\"><![CDATA[QkFTRTY0]]></value></item>\n",
                                                TRUE);
        g_autofree gchar *blank = read_document ("parser-blank",
                                                 "  <item kind=\"Text\"><value><![CDATA[\n\n\n\n]]></value></item>\n"
                                                 "  <item kind=\"Text\"><value>\n    <![CDATA[indented]]>\n  </value></item>\n",
                                                 TRUE);
        g_autofree gchar *trailing = read_document ("parser-trailing",
                                                    "  <item kind=\"Text\"><value><![CDATA[kept]]>never logged</value></item>\n",
                                                    TRUE);
        g_autofree gchar *nested_item = read_document ("parser-nested-item",
                                                       "  <item kind=\"Text\"><value>a</value><item kind=\"Text\"><value>b</value></item></item>\n",
                                                       FALSE);
        g_autofree gchar *nested_history = read_document ("parser-nested-history",
                                                          "  <item kind=\"Text\"><value>a</value></item><history></history>\n"
                                                          "  <item kind=\"Text\"><value>b</value></item>\n",
                                                          FALSE);
        g_autofree gchar *notes = read_document ("parser-unknown-in-item",
                                                 "  <item kind=\"Text\"><value><![CDATA[own]]></value>"
                                                 "<notes><value><![CDATA[a note]]></value></notes></item>\n",
                                                 TRUE);
        g_autofree gchar *wrapped = read_document ("parser-unknown-wrapping",
                                                   "  <item kind=\"Text\"><future><value>inside</value></future>"
                                                   "<value>own too</value></item>\n",
                                                   TRUE);

        g_assert_cmpstr (notes, ==, "own");
        g_assert_cmpstr (wrapped, ==, "own too");
        g_free (read_document ("parser-markup-in-value",
                               "  <item kind=\"Text\"><value>before<b>bold</b>after</value></item>\n",
                               FALSE));
        g_free (read_document ("parser-bad-utf8",
                               "  <item kind=\"Text\"><value><![CDATA[a value damaged \xc3]]></value></item>\n"
                               "  <item kind=\"Text\"><value><![CDATA[another value damaged \xff]]></value></item>\n"
                               "  <item kind=\"Text\"><value><![CDATA[intact]]></value></item>\n",
                               FALSE));
        g_free (read_document ("parser-entity",
                               "  <item kind=\"Password\" name=\"&an-entity-unlogged;\"><value>x</value></item>\n",
                               FALSE));
        g_free (read_document ("parser-timeout",
                               "  <item kind=\"Password\" name=\"bank\" timeout=\"30s\"><value>x</value></item>\n",
                               FALSE));
        g_autofree gchar *date = read_document ("parser-date",
                                                "  <item kind=\"Image\" date=\"99999999999999\"><value>/nowhere.png</value></item>\n",
                                                TRUE);

        g_assert_cmpstr (mime, ==, "the plain text");
        g_assert_cmpstr (blank, ==, "\n\n\n\n|indented");
        g_assert_cmpstr (trailing, ==, "kept");
        g_assert_cmpstr (nested_item, ==, "b");
        g_assert_cmpstr (nested_history, ==, "a");
        g_assert_cmpstr (date, ==, "");

        g_autoptr (GBytes) png = test_png_bytes_colored (45, 46, 47);
        g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
        g_autofree gchar *png_path = g_build_filename (history_dir, "parser-checksum.png", NULL);

        g_assert_true (g_file_set_contents (png_path, g_bytes_get_data (png, NULL), (gssize) g_bytes_get_size (png), NULL));

        g_autofree gchar *image = g_strdup_printf ("  <item kind=\"Image\" date=\"1234567890\" checksum=\"../../escaped&quot;x\"><value>%s</value></item>\n", png_path);
        g_autofree gchar *checksum = read_document ("parser-checksum", image, TRUE);

        g_assert_cmpuint (strlen (checksum), ==, 64);
        g_assert_cmpuint (strspn (checksum, "0123456789abcdef"), ==, 64);

        /* A date is restored as the decimal it was checked as, a leading zero
         * included. */
        {
            g_autoptr (GPasteSettings) images = g_paste_settings_new ();
            g_autoptr (GPasteStorageBackend) file = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, images);
            g_autofree gchar *padded_path = g_paste_util_get_history_file_path ("parser-padded-date", "xml");
            g_autofree gchar *padded = g_strdup_printf ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<history version=\"2.0\">\n"
                                                        "  <item kind=\"Image\" date=\"01234567890\"><value>%s</value></item>\n"
                                                        "</history>\n", png_path);
            gboolean ok = FALSE;

            g_paste_settings_set_images_support (images, TRUE);
            g_assert_true (g_file_set_contents (padded_path, padded, -1, NULL));

            g_autolist (GPasteItem) loaded = read_history_full (file, "parser-padded-date", &ok);

            g_assert_true (ok);
            g_assert_cmpuint (g_list_length (loaded), ==, 1);
            g_assert_cmpint (g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (loaded->data)), ==, 1234567890);
        }

        g_autofree gchar *stray = g_build_filename (history_dir, ".xml", NULL);

        g_assert_true (g_file_set_contents (stray, "", -1, NULL));

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
        g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, NULL);

        g_assert_nonnull (names);
        g_assert_false (g_strv_contains ((const gchar * const *) names, ""));
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*Unknown mime*");
    g_test_trap_assert_stderr_unmatched ("*CRITICAL*");
    g_test_trap_assert_stderr_unmatched ("*never logged*");
    g_test_trap_assert_stderr_unmatched ("*value damaged*");
    g_test_trap_assert_stderr_unmatched ("*an-entity-unlogged*");
}

/* A history written by a newer GPaste (an unknown <history version=>) is a read
 * *failure*, not an empty one: every item is skipped, so passing it off as empty
 * would have the very next clipboard change overwrite it. Once loaded it must
 * therefore stop persistence — for itself alone, and only until it is gone. */
static void
test_file_version_guard (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "file-newer";
        const gchar *contents =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<history version=\"9.0\">\n"
            "  <item kind=\"Text\"><value>precious</value></item>\n"
            "  <item kind=\"Text\"><value>also precious</value></item>\n"
            "</history>\n";

        g_assert_true (g_paste_util_ensure_history_dir_exists ());

        g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xml");

        g_assert_true (g_file_set_contents (path, contents, -1, NULL));

        {
            g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
            assert_history_refused (backend, name);
        }

        g_autoptr (GPasteHistory) history = make_plain_history ();

        g_paste_history_load (history, name);
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);

        /* Recording is off for it: pump the loop so an (erroneous) async write
         * would run, then check the file is byte-for-byte what we wrote. */
        g_paste_history_add (history, g_paste_text_item_new ("usurper"));

        pump_for_ms (100);

        g_autofree gchar *raw = NULL;

        g_assert_true (g_file_get_contents (path, &raw, NULL, NULL));
        g_assert_cmpstr (raw, ==, contents);

        /* ...for itself alone: the next history loaded stands on its own. */
        const gchar *sibling = "file-newer-sibling";

        g_paste_history_load (history, sibling);
        g_paste_history_add (history, g_paste_text_item_new ("kept"));
        g_paste_history_flush (history);
        g_paste_history_resume (history);

        g_autofree gchar *sibling_path = g_paste_util_get_history_file_path (sibling, "xml");
        g_autofree gchar *sibling_raw = NULL;

        g_assert_true (g_file_get_contents (sibling_path, &sibling_raw, NULL, NULL));
        g_assert_nonnull (g_strstr_len (sibling_raw, -1, "kept"));

        /* ...and only until it is gone: deleting the unreadable history unlinks
         * the very file the protection was for, so it must lift with it rather
         * than shut persistence down for the rest of the process. */
        g_paste_history_load (history, name);

        g_autoptr (GError) error = NULL;

        g_assert_true (g_paste_history_delete (history, name, &error));
        g_assert_no_error (error);
        g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));

        g_paste_history_add (history, g_paste_text_item_new ("after delete"));
        g_paste_history_flush (history);

        g_autofree gchar *after = NULL;

        g_assert_true (g_file_get_contents (path, &after, NULL, NULL));
        g_assert_nonnull (g_strstr_len (after, -1, "after delete"));

        return;
    }

    /* The glob also pins the parse location the warning carries: an encrypted
     * history is parsed from a decrypted in-memory buffer, so the enclosing
     * element and the byte offsets are what actually identify the spot. */
    G_PASTE_TEST_TRAP_SUBPROCESS ("*Unknown history version: 9.0 in file *file-newer.xml*"
                                  " at line *, column * (byte *), inside <history> opened at line *");
}

static void
record_dropped (GPasteHistory *history G_GNUC_UNUSED,
                GPasteItem    *item,
                gpointer       user_data)
{
    g_ptr_array_add (user_data, g_strdup (g_paste_item_get_real_value (item)));
}

/* Every way a password's entry leaves the model announces it, and its position
 * is not the question: the cleartext may be sitting on a selection, where the
 * entry that recorded it is now gone and no countdown need ever have been running
 * (GPasteHistory::password-dropped). What the selections then carry is the
 * clipboards manager's to answer (the /clipboard/dropped_password cases).
 *
 * An entry merely moving is no departure: the value stays in the history, so
 * selecting a password says nothing about any selection giving it up. */
static void
test_password_dropped (void)
{
    g_autoptr (GPtrArray) dropped = g_ptr_array_new_with_free_func (g_free);

    {
        g_autoptr (GPasteHistory) history = make_history (NULL, 10);

        g_signal_connect (history, "password-dropped", G_CALLBACK (record_dropped), dropped);

        g_paste_history_add (history, g_paste_password_item_new (NULL, "deleted secret", 0));
        g_paste_history_add (history, g_paste_text_item_new ("a text"));
        g_assert_cmpuint (dropped->len, ==, 0);

        g_paste_history_remove (history, 1);
        g_assert_cmpuint (dropped->len, ==, 1);
        g_assert_cmpstr (g_ptr_array_index (dropped, 0), ==, "deleted secret");

        g_paste_history_add (history, g_paste_password_item_new (NULL, "emptied secret", 0));
        g_assert_true (g_paste_history_select (history, g_paste_item_get_uuid (g_paste_history_get (history, 1))));
        g_assert_cmpuint (dropped->len, ==, 1);

        g_assert_true (g_paste_history_empty (history, NULL));
        g_assert_cmpuint (dropped->len, ==, 2);
        g_assert_cmpstr (g_ptr_array_index (dropped, 1), ==, "emptied secret");
        g_signal_handlers_disconnect_by_data (history, dropped);
    }

    /* A cap evicting one is a departure like any other: nobody asked for it, and
     * that is exactly why nothing else would take the cleartext off. */
    {
        g_autoptr (GPasteHistory) history = make_history (NULL, 5);

        g_signal_connect (history, "password-dropped", G_CALLBACK (record_dropped), dropped);

        g_paste_history_add (history, g_paste_password_item_new (NULL, "evicted secret", 0));
        for (guint i = 0; i < 5; ++i)
        {
            g_autofree gchar *text = g_strdup_printf ("copy %u", i);

            g_paste_history_add (history, g_paste_text_item_new (text));
        }
        g_assert_cmpuint (dropped->len, ==, 3);
        g_assert_cmpstr (g_ptr_array_index (dropped, 2), ==, "evicted secret");
        g_signal_handlers_disconnect_by_data (history, dropped);
    }

    /* A reload whose store holds the password no more -- a migration to a plain
     * flavour imports none -- is its departure too. */
    {
        g_autoptr (GPasteSettings) settings = NULL;
        g_autoptr (GPasteHistory) history = make_history (&settings, 10);

        g_signal_connect (history, "password-dropped", G_CALLBACK (record_dropped), dropped);

        g_paste_history_add (history, g_paste_text_item_new ("a kept text"));
        g_paste_history_add (history, g_paste_password_item_new (NULL, "reloaded secret", 0));
        pump_for_ms (50);
        g_paste_history_flush (history);
        g_paste_history_reload_backend (history);
        g_assert_true (pump_until_length (history, 1, 5000));
        g_assert_cmpuint (dropped->len, ==, 4);
        g_assert_cmpstr (g_ptr_array_index (dropped, 3), ==, "reloaded secret");
        g_signal_handlers_disconnect_by_data (history, dropped);
    }

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* A reload whose store keeps it -- an encrypted flavour stores passwords,
     * as a passphrase change rewrites them -- is none, as a switch is none. */
    {
        g_paste_storage_backend_set_passphrase ("reload passphrase");

        /* Set before the history is made, so that it loads from that flavour:
         * the settings all share the one memory backend. */
        {
            g_autoptr (GPasteSettings) encrypted = g_paste_settings_new ();

            g_paste_settings_set_storage_backend (encrypted, G_PASTE_STORAGE_ENCRYPTED_FILE);
        }

        g_autoptr (GPasteHistory) history = make_history (NULL, 10);

        g_signal_connect (history, "password-dropped", G_CALLBACK (record_dropped), dropped);

        g_paste_history_add (history, g_paste_text_item_new ("a kept text"));
        g_paste_history_add (history, g_paste_password_item_new (NULL, "kept secret", 0));
        g_paste_history_flush (history);
        g_paste_history_reload_backend (history);
        g_assert_true (pump_until_length (history, 2, 5000));
        g_assert_cmpstr (g_paste_item_get_real_value (g_paste_history_get (history, 0)), ==, "kept secret");
        g_assert_cmpuint (dropped->len, ==, 4);
        g_paste_storage_backend_set_passphrase (NULL);
        g_signal_handlers_disconnect_by_data (history, dropped);
    }
#endif
}

/* A reload's passwords left out of what it carries are announced when the
 * history is emptied before its load lands, as that load would have. */
static void
test_reload_then_empty_drops_password (void)
{
    g_autoptr (GPasteHistory) history = make_history (NULL, 10);
    g_autoptr (GPtrArray) dropped = g_ptr_array_new_with_free_func (g_free);

    g_signal_connect (history, "password-dropped", G_CALLBACK (record_dropped), dropped);
    g_paste_history_add (history, g_paste_text_item_new ("a text"));
    g_paste_history_add (history, g_paste_password_item_new (NULL, "reloaded secret", 0));
    pump_for_ms (50);
    g_paste_history_flush (history);
    g_paste_history_reload_backend (history);
    g_assert_true (g_paste_history_empty (history, NULL));
    pump_for_ms (50);
    g_assert_cmpuint (dropped->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (dropped, 0), ==, "reloaded secret");
    g_signal_handlers_disconnect_by_data (history, dropped);
}

/* A load waited for as the daemon's main loop would. */
static gboolean
pump_until_loaded (GPasteHistory *history)
{
    for (guint i = 0; i < 5000 && g_paste_history_is_loading (history); ++i)
        pump_once ();
    return !g_paste_history_is_loading (history);
}

/* So are they when the history, the one history-name names, is deleted before
 * that load lands -- leaving it for the default history first lets go of them
 * -- and when a second reload comes before it, which gives that load up. */
static void
test_reload_then_delete_drops_password (gconstpointer data)
{
    gboolean twice = GPOINTER_TO_INT (data);
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    g_autoptr (GPtrArray) dropped = g_ptr_array_new_with_free_func (g_free);

    g_paste_settings_set_history_name (settings, g_paste_history_get_current (history));
    g_signal_connect (history, "password-dropped", G_CALLBACK (record_dropped), dropped);
    g_paste_history_add (history, g_paste_text_item_new ("a text"));
    g_paste_history_add (history, g_paste_password_item_new (NULL, "reloaded secret", 0));
    pump_for_ms (50);
    g_paste_history_flush (history);
    g_paste_history_reload_backend (history);

    if (twice)
    {
        g_paste_history_flush (history);
        g_paste_history_reload_backend (history);
        g_assert_true (pump_until_loaded (history));
    }
    else
    {
        g_autoptr (GError) error = NULL;

        g_assert_true (g_paste_history_delete (history, NULL, &error));
        g_assert_no_error (error);
        g_assert_true (pump_until_loaded (history));
    }

    pump_for_ms (50);
    g_assert_cmpuint (dropped->len, ==, 1);
    g_assert_cmpstr (g_ptr_array_index (dropped, 0), ==, "reloaded secret");
    g_signal_handlers_disconnect_by_data (history, dropped);
    g_paste_settings_reset (settings, G_PASTE_HISTORY_NAME_SETTING);
    g_paste_history_flush (history);
}

/* A history whose store holds @value alone, pinned or not, and a reader of it
 * whose load is out: nothing reaches it before the main loop runs. */
static GPasteHistory *
loading_history (GPasteSettings **settings,
                 const gchar     *value,
                 gboolean         pinned)
{
    g_autofree gchar *name = NULL;

    {
        g_autoptr (GPasteHistory) seed = make_history (settings, 10);

        name = g_strdup (g_paste_history_get_current (seed));
        g_paste_history_add (seed, g_paste_text_item_new (value));
        if (pinned)
        {
            g_autofree gchar *uuid = dup_uuid_at (seed, 0);

            g_assert_true (g_paste_history_set_favourite (seed, uuid, TRUE));
        }
        g_paste_history_flush (seed);
    }

    GPasteHistory *history = g_paste_history_new (*settings);

    g_paste_history_load_async (history, name);
    return history;
}

/* Whether a name is taken is a question about every password the history holds,
 * which a load out has yet to bring back: a named AddPassword or MakePassword,
 * and a DeletePassword, are refused BUSY until it lands. */
static void
test_password_names_wait_for_load (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = loading_history (&settings, "stored", FALSE);
    const GPasteDaemonMethods methods = { .history = history };
    g_autoptr (GError) error = NULL;

    g_assert_true (g_paste_history_is_loading (history));
    g_assert_null (g_paste_daemon_methods_add_password (&methods, "Work", "secret", 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error (&error);
    g_assert_null (g_paste_daemon_methods_make_password (&methods, "00000000-0000-4000-8000-000000000000", "Work", 0, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error (&error);
    g_paste_daemon_methods_delete_password (&methods, "Work", &error);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
    g_clear_error (&error);

    g_assert_true (pump_until_loaded (history));
    g_paste_daemon_methods_delete_password (&methods, "Work", &error);
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_NOT_FOUND);
}

static void
record_selected (GPasteHistory *history G_GNUC_UNUSED,
                 GPasteItem    *item,
                 gpointer       user_data)
{
    g_set_str ((gchar **) user_data, (item) ? g_paste_item_get_real_value (item) : NULL);
}

/* What is added while a load is out ends up on top of what it read, and so on
 * the selections: the read head is not published over the last copy. */
static void
test_load_selects_last_add (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = loading_history (&settings, "stored", FALSE);
    g_autofree gchar *selected = NULL;

    g_signal_connect (history, "selected", G_CALLBACK (record_selected), &selected);
    g_paste_history_add (history, g_paste_text_item_new ("copied during the load"));
    g_assert_true (pump_until_loaded (history));
    g_assert_cmpstr (value_at (history, 0), ==, "copied during the load");
    g_assert_cmpstr (selected, ==, "copied during the load");
    g_signal_handlers_disconnect_by_data (history, &selected);
}

/* A switch to the history in use, which a settings backend may announce as a
 * change all the same, is no switch: nothing only the model holds is dropped,
 * a plain flavour's password included. */
static void
count_switches (GPasteHistory *history G_GNUC_UNUSED,
                const gchar   *name G_GNUC_UNUSED,
                gpointer       user_data)
{
    ++*(guint *) user_data;
}

static void
test_switch_to_current_keeps_model (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    guint switches = 0;

    g_signal_connect (history, "switch", G_CALLBACK (count_switches), &switches);
    g_paste_settings_set_history_name (settings, g_paste_history_get_current (history));
    g_paste_history_add (history, g_paste_text_item_new ("stored"));
    g_paste_history_add (history, g_paste_password_item_new (NULL, "in the model alone", 0));
    g_object_notify (G_OBJECT (settings), "history-name");
    g_assert_true (pump_until_loaded (history));

    g_assert_cmpuint (switches, ==, 0);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_true (G_PASTE_IS_PASSWORD_ITEM (g_paste_history_get (history, 0)));

    g_signal_handlers_disconnect_by_data (history, &switches);
}

/* A password copied while a load is out is the head the load leaves, and no
 * selection is told to take the head the load read: published over the
 * password, it would take the copy just made off the clipboard. */
static void
test_load_keeps_password_selected (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = loading_history (&settings, "stored head", FALSE);
    g_autofree gchar *selected = NULL;

    g_signal_connect (history, "selected", G_CALLBACK (record_selected), &selected);
    g_paste_history_add (history, g_paste_password_item_new (NULL, "copied secret", 0));
    g_assert_true (pump_until_loaded (history));
    g_assert_true (G_PASTE_IS_PASSWORD_ITEM (g_paste_history_get (history, 0)));
    g_assert_null (selected);
    g_signal_handlers_disconnect_by_data (history, &selected);
}

#ifdef G_PASTE_ENABLE_SQLITE
/* An add made while a load is out, equal to the head it reads, takes that
 * head's place in the store too: the database keeps one row of it, not the
 * read head's beside the add's. */
static void
test_load_add_replaces_stored_head (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    const gchar *name = "load-add-replaces-stored-head";

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

    {
        g_autoptr (GPasteHistory) seed = g_paste_history_new (settings);

        g_paste_history_load (seed, name);
        g_paste_history_add (seed, g_paste_text_item_new ("older"));
        g_paste_history_add (seed, g_paste_text_item_new ("twin"));
        g_paste_history_flush (seed);
    }

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load_async (history, name);
    g_paste_history_add (history, g_paste_text_item_new ("twin"));
    g_assert_true (pump_until_loaded (history));
    g_paste_history_flush (history);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) stored = read_history_ok (backend, name);

    g_assert_cmpuint (g_list_length (stored), ==, 2);
    g_assert_cmpstr (g_paste_item_get_value (stored->data), ==, "twin");
    g_assert_cmpstr (g_paste_item_get_value (stored->next->data), ==, "older");
}
#endif

/* An add made while a load is out, equal to the head it reads, keeps the uuid
 * its caller was answered with, and takes the pin that head had. */
static void
test_load_keeps_add_uuid (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = loading_history (&settings, "twin", TRUE);
    GPasteItem *added = g_paste_text_item_new ("twin");
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (added));

    g_paste_history_add (history, added);
    g_assert_true (pump_until_loaded (history));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_nonnull (g_paste_history_get_by_uuid (history, uuid));
    g_assert_true (g_paste_item_is_favourite (g_paste_history_get (history, 0)));
}

static void
count_replace_all (GPasteHistory     *history  G_GNUC_UNUSED,
                   GPasteUpdateAction action,
                   GPasteUpdateTarget target,
                   const gchar       *uuid     G_GNUC_UNUSED,
                   guint64            position G_GNUC_UNUSED,
                   gpointer           user_data)
{
    if (action == G_PASTE_UPDATE_ACTION_REPLACE && target == G_PASTE_UPDATE_TARGET_ALL)
        ++*(guint *) user_data;
}

/* A reload empties the history until its load fills it again, and says so then
 * and there, as a switch does: its name staying the same, nothing else would
 * tell a view still showing what it put away. */
static void
test_reload_backend_announced (void)
{
    guint updates = 0;
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);

    g_paste_history_add (history, g_paste_text_item_new ("reloaded"));
    pump_for_ms (50);
    g_paste_history_flush (history);
    g_signal_connect (history, "update", G_CALLBACK (count_replace_all), &updates);
    g_paste_history_reload_backend (history);

    g_assert_cmpuint (updates, ==, 1);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_assert_true (pump_until_length (history, 1, 5000));
    g_assert_cmpuint (updates, ==, 2);
}

/* What is copied while the store is handed over -- a passphrase change's
 * prompts up -- is in the model alone: the reload ending the handover keeps
 * it, recorded on top of what it reads back. A password among it has not
 * left the history either. */
static void
test_reload_keeps_stopped_adds (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = make_history (&settings, 10);
    g_autoptr (GPtrArray) dropped = g_ptr_array_new_with_free_func (g_free);

    g_signal_connect (history, "password-dropped", G_CALLBACK (record_dropped), dropped);
    g_paste_history_add (history, g_paste_text_item_new ("stored"));
    pump_for_ms (50);
    g_paste_history_flush (history);
    g_paste_history_add (history, g_paste_password_item_new (NULL, "secret copied while stopped", 0));
    g_paste_history_add (history, g_paste_text_item_new ("copied while stopped"));
    g_paste_history_reload_backend (history);

    g_assert_true (pump_until_length (history, 3, 5000));
    g_assert_cmpstr (value_at (history, 0), ==, "copied while stopped");
    g_assert_cmpstr (g_paste_item_get_real_value (g_paste_history_get (history, 1)), ==, "secret copied while stopped");
    g_assert_cmpstr (value_at (history, 2), ==, "stored");
    g_assert_cmpuint (dropped->len, ==, 0);

    g_paste_history_flush (history);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) stored = read_history_ok (backend, g_paste_history_get_current (history));

    g_assert_cmpuint (g_list_length (stored), ==, 2);
    g_assert_cmpstr (g_paste_item_get_value (stored->data), ==, "copied while stopped");
    g_signal_handlers_disconnect_by_data (history, dropped);
}

/* A load landing inside a handover adds what was copied while it was out on
 * top of what it read, and the reload ending the handover carries that over
 * once: a delete of it then holds, the reload's own load leaving it out. */
static void
test_reload_carries_load_adds_once (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = loading_history (&settings, "stored", FALSE);

    g_paste_history_flush (history);
    g_paste_history_add (history, g_paste_text_item_new ("copied"));
    g_assert_true (pump_until_loaded (history));
    g_paste_history_reload_backend (history);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_assert_true (g_paste_history_remove_by_uuid (history, uuid));
    g_assert_true (pump_until_loaded (history));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_cmpstr (value_at (history, 0), ==, "stored");
}

/* A password copied during a handover whose countdown runs out has the entry
 * before it put back on top (the clipboards manager's retire_password ()), and
 * the reload ending the handover keeps that order. */
static void
test_reload_keeps_retired_order (void)
{
    g_autoptr (GPasteHistory) history = make_history (NULL, 10);

    g_paste_history_add (history, g_paste_text_item_new ("stored"));
    pump_for_ms (50);
    g_paste_history_flush (history);
    g_paste_history_add (history, g_paste_password_item_new (NULL, "expired secret", 0));
    g_paste_history_add (history, g_object_ref (g_paste_history_get (history, 1)));
    g_paste_history_reload_backend (history);

    g_assert_true (pump_until_loaded (history));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (value_at (history, 0), ==, "stored");
    g_assert_cmpstr (g_paste_item_get_real_value (g_paste_history_get (history, 1)), ==, "expired secret");
}

/* What a cap evicts during a handover is freed then, not kept for the reload. */
static void
test_stopped_add_evicted_freed (void)
{
    g_autoptr (GPasteHistory) history = make_history (NULL, 5);
    GPasteItem *evicted = g_paste_text_item_new ("evicted while stopped");

    g_object_add_weak_pointer (G_OBJECT (evicted), (gpointer *) &evicted);
    g_paste_history_flush (history);
    g_paste_history_add (history, evicted);
    for (guint i = 0; i < 5; ++i)
    {
        g_autofree gchar *text = g_strdup_printf ("copy %u", i);

        g_paste_history_add (history, g_paste_text_item_new (text));
    }
    g_assert_null (evicted);
}

/* A reload landing before the load it replaces carries what was added while
 * that load was out: nothing has recorded it yet. */
static void
test_reload_during_load_keeps_adds (void)
{
    g_autoptr (GPasteSettings) settings = NULL;
    g_autoptr (GPasteHistory) history = loading_history (&settings, "stored", FALSE);

    g_paste_history_add (history, g_paste_text_item_new ("copied during the load"));
    g_paste_history_flush (history);
    g_paste_history_reload_backend (history);
    g_assert_true (pump_until_loaded (history));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (value_at (history, 0), ==, "copied during the load");
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* The encrypted file backend must round-trip a history (keeping password
 * entries and their real value), and the on-disk ".xmls" file must actually be
 * ciphertext rather than the plaintext XML. */
static void
test_encrypted_roundtrip (void)
{
    const gchar *name = "encrypted";
    const gchar *secret = "s3cr3t-passw0rd";
    const gchar *pw_name = "my login";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "the master passphrase");

    g_autoptr (GBytes) png = test_png_bytes_colored (7, 8, 9);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    GList *items = NULL;
    items = g_list_append (items, g_paste_text_item_new ("plain text entry"));
    items = g_list_append (items, g_paste_password_item_new (pw_name, secret, PASSWORD_TIMEOUT));
    items = g_list_append (items, g_paste_image_item_new_from_bytes (png, date, NULL));

    g_paste_settings_set_images_support (settings, TRUE);
    g_paste_storage_backend_write_history (backend, name, items);

    /* On disk: ".xmls", starting with our magic, with no plaintext leaking. */
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xmls");
    g_autofree gchar *raw = NULL;
    gsize raw_len = 0;
    g_assert_true (g_file_get_contents (path, &raw, &raw_len, NULL));
    g_assert_cmpuint (raw_len, >=, 8);
    g_assert_cmpint (memcmp (raw, "GPSTENC1", 8), ==, 0);
    /* Binary-safe scan: the ciphertext is full of NUL bytes, so g_strstr_len
     * would stop at the first one and only check a tiny prefix. */
    g_assert_false (file_contains (path, secret));
    g_assert_false (file_contains (path, "<?xml"));

    /* The image was materialized as an encrypted ".pngs" side file: the
     * referenced plaintext path stays absent and no PNG marker is in clear. */
    g_autofree gchar *cache_path = g_paste_file_backend_image_path (name,
                                                                    g_paste_image_item_get_checksum (g_list_nth_data (items, 2)));
    g_autofree gchar *encrypted_image_path = g_paste_file_backend_encrypted_path (cache_path);

    g_assert_false (g_file_test (cache_path, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (encrypted_image_path, G_FILE_TEST_EXISTS));
    g_assert_false (file_contains (encrypted_image_path, "IHDR"));

    /* Read back through the same backend. */
    g_autolist (GPasteItem) loaded = read_history (backend, name);
    g_assert_cmpuint (g_list_length (loaded), ==, 3);

    gboolean found_text = FALSE;
    gboolean found_password = FALSE;
    gboolean found_image = FALSE;
    for (const GList *l = loaded; l; l = l->next)
    {
        GPasteItem *item = l->data;
        GPasteItemKind kind = g_paste_item_get_kind (item);

        if (kind == G_PASTE_ITEM_KIND_PASSWORD)
        {
            found_password = TRUE;
            g_assert_cmpstr (g_paste_item_get_real_value (item), ==, secret);
            g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (item)), ==, pw_name);
            g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (item)), ==, PASSWORD_TIMEOUT);
        }
        else if (kind == G_PASTE_ITEM_KIND_IMAGE)
        {
            found_image = TRUE;

            /* Decrypted from the side file, byte-identical. */
            GBytes *read_png = g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (item));

            g_assert_nonnull (read_png);
            g_assert_true (g_bytes_equal (read_png, png));
        }
        else
        {
            found_text = TRUE;
            g_assert_cmpstr (g_paste_item_get_value (item), ==, "plain text entry");
        }
    }
    g_assert_true (found_text);
    g_assert_true (found_password);
    g_assert_true (found_image);

    g_list_free_full (items, g_object_unref);
}

/* Reading a history that does not exist yet writes an empty placeholder file so
 * the history shows up in listings. Reading that placeholder back must say
 * "empty", not "unreadable": an unreadable history is one the daemon refuses to
 * persist over, so a brand new encrypted history would have stopped being stored
 * the first time the daemon restarted after it was created. */
static void
test_encrypted_empty_placeholder (void)
{
    const gchar *name = "encrypted-placeholder";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "the master passphrase");

    /* The first read creates the placeholder. */
    g_autolist (GPasteItem) first = read_history_ok (backend, name);

    g_assert_null (first);

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xmls");

    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    /* The second read finds the file already there, holding no encrypted
     * stream. */
    g_autolist (GPasteItem) second = read_history_ok (backend, name);

    g_assert_null (second);
}

static void
count_selected (GPasteHistory *history G_GNUC_UNUSED,
                GPasteItem    *item G_GNUC_UNUSED,
                gpointer       user_data)
{
    ++*(guint *) user_data;
}

/* Loading a history selects its head, which puts it on the selections -- but not
 * a password: nobody asked to publish it, and under a password-timeout of 0 it
 * would then stay there for the session.
 *
 * Encryption is not what the rule is about -- it belongs to the load path, and
 * the removal path pins the same one without any store at all (the
 * /clipboard/automatic_head cases). It is what the *case* takes: only the
 * encrypted flavours persist a password entry at all
 * (g_paste_storage_backend_stores_item ()), the readable ones dropping it on
 * write by design, so no other backend can hand back a history with a
 * password at its head. */
static void
test_load_skips_password_head (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "the master passphrase");
    const gchar *names[] = { "load-password-head", "load-text-head" };

    for (guint i = 0; i < G_N_ELEMENTS (names); ++i)
    {
        g_autolist (GPasteItem) items = NULL;

        items = g_list_append (items, (i) ? g_paste_text_item_new ("head") : g_paste_password_item_new (NULL, "head", 0));
        items = g_list_append (items, g_paste_text_item_new ("older"));
        g_paste_storage_backend_write_history (backend, names[i], items);
    }

    g_paste_storage_backend_set_passphrase ("the master passphrase");
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_FILE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    guint selected = 0;

    g_signal_connect (history, "selected", G_CALLBACK (count_selected), &selected);

    g_paste_history_load (history, names[0]);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpuint (selected, ==, 0);

    g_paste_history_load (history, names[1]);
    g_assert_cmpuint (selected, ==, 1);

    g_signal_handlers_disconnect_by_data (history, &selected);
    g_paste_storage_backend_set_passphrase (NULL);
}

/* g_paste_storage_backend_new_with_passphrase() must key the backend with
 * exactly the passphrase it is given, never with the process-wide one: a
 * migration between two encrypted flavors holds the source and the destination
 * open at the same time under two different keys, and reading the source with
 * the destination's key would yield an empty history that passes for an emptied
 * one. */
static void
test_encrypted_explicit_passphrase (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "encrypted-explicit";
        const gchar *source_passphrase = "the source passphrase";
        const gchar *value = "only the source key reads this";

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        /* The process-wide passphrase is deliberately a different one throughout,
         * standing in for the destination's key during a migration. */
        g_paste_storage_backend_set_passphrase ("the destination passphrase");

        {
            g_autoptr (GPasteStorageBackend) backend =
                g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, source_passphrase);
            GList *items = g_list_append (NULL, g_paste_text_item_new (value));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);
        }

        /* The explicit key reads it back... */
        {
            g_autoptr (GPasteStorageBackend) backend =
                g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, source_passphrase);
            g_autolist (GPasteItem) loaded = read_history_ok (backend, name);
            g_assert_cmpuint (g_list_length (loaded), ==, 1);
            g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, value);
        }

        /* ...while the process-wide one does not, and is refused rather than
         * silently yielding an empty history. */
        {
            g_autoptr (GPasteStorageBackend) backend =
                g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings,
                                                             g_paste_storage_backend_get_passphrase ());
            assert_history_refused (backend, name);
        }

        /* No passphrase at all degrades to "no storage" instead of falling back
         * to the process-wide one (the destination's key here). */
        {
            g_autoptr (GPasteStorageBackend) backend =
                g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, NULL);
            g_autolist (GPasteItem) loaded = read_history (backend, name);
            g_assert_null (loaded);
        }

        /* The plain constructor, by contrast, does fall back to the process-wide
         * passphrase — which is the destination's, hence unable to read this. */
        {
            g_autoptr (GPasteStorageBackend) backend =
                g_paste_storage_backend_new (G_PASTE_STORAGE_ENCRYPTED_FILE, settings);
            assert_history_refused (backend, name);
        }

        g_paste_storage_backend_set_passphrase (NULL);

        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*No passphrase for the encrypted storage backend*");
}

/* Changing the passphrase must re-encrypt everything the history holds, in
 * place: afterwards the new passphrase reads it all back — the image included,
 * which is what proves the ".pngs" side files were rewritten rather than left
 * behind under the old key — and the old one no longer opens it. */
static void
test_encrypted_rekey (void)
{
    if (g_test_subprocess ())
    {
        /* Reading with the old passphrase afterwards warns; g_test_expect_message
         * cannot swallow that under structured logging, so drop warning fatality
         * here and let the parent match it on stderr. */
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "encrypted-rekey";
        const gchar *other_name = "encrypted-rekey-backup";
        const gchar *other_value = "the second history";
        const gchar *old_passphrase = "the old passphrase";
        const gchar *new_passphrase = "the new passphrase";
        const gchar *secret = "s3cr3t-passw0rd";
        const gchar *pw_name = "my login";

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        g_autoptr (GBytes) png = test_png_bytes_colored (3, 5, 7);
        g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);

        g_paste_settings_set_images_support (settings, TRUE);

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, old_passphrase);
            GList *items = NULL;

            items = g_list_append (items, g_paste_text_item_new ("plain text entry"));
            items = g_list_append (items, g_paste_password_item_new (pw_name, secret, PASSWORD_TIMEOUT));
            items = g_list_append (items, g_paste_image_item_new_from_bytes (png, date, NULL));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);

            /* A second history, re-keyed through the same backend as the daemon
             * does it: re-keying one must not cost this instance the ability to
             * open the others, which are still on the old passphrase. */
            GList *other_items = g_list_append (NULL, g_paste_text_item_new (other_value));

            g_paste_storage_backend_write_history (backend, other_name, other_items);
            g_list_free_full (other_items, g_object_unref);

            g_assert_true (g_paste_storage_backend_rekey (backend, name, new_passphrase, NULL));
            g_assert_true (g_paste_storage_backend_rekey (backend, other_name, new_passphrase, NULL));
        }

        /* Both histories moved, not just the first. */
        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, new_passphrase);
            g_autolist (GPasteItem) loaded = read_history_ok (backend, other_name);
            g_assert_cmpuint (g_list_length (loaded), ==, 1);
            g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, other_value);
        }

        /* The new passphrase reads back everything, contents included. */
        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, new_passphrase);
            g_autolist (GPasteItem) loaded = read_history_ok (backend, name);
            g_assert_cmpuint (g_list_length (loaded), ==, 3);

            gboolean found_password = FALSE;
            gboolean found_image = FALSE;

            for (const GList *l = loaded; l; l = l->next)
            {
                GPasteItem *item = l->data;
                GPasteItemKind kind = g_paste_item_get_kind (item);

                if (kind == G_PASTE_ITEM_KIND_PASSWORD)
                {
                    found_password = TRUE;
                    g_assert_cmpstr (g_paste_item_get_real_value (item), ==, secret);
                    g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (item)), ==, pw_name);
                    g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (item)), ==, PASSWORD_TIMEOUT);
                }
                else if (kind == G_PASTE_ITEM_KIND_IMAGE)
                {
                    found_image = TRUE;

                    /* Decrypted from the side file with the *new* key. */
                    GBytes *read_png = g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (item));

                    g_assert_nonnull (read_png);
                    g_assert_true (g_bytes_equal (read_png, png));
                }
            }

            g_assert_true (found_password);
            g_assert_true (found_image);
        }

        /* ...and the old one is refused rather than read as empty. */
        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, old_passphrase);
            assert_history_refused (backend, name);
        }

        /* A plain history has no passphrase to change, and says so — through the
         * backend's own guard, since the file class implements rekey either way. */
        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

            g_assert_false (g_paste_storage_backend_rekey (backend, name, new_passphrase, NULL));
        }

        /* A key change must change nothing but the key: a history holding more
         * than the current size limit keeps every item, where reading it into
         * items and writing them back would have dropped the overflow. */
        {
            const gchar *big_name = "encrypted-rekey-big";

            g_paste_settings_set_max_history_size (settings, 10);

            {
                g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, old_passphrase);
                GList *items = NULL;

                for (guint i = 0; i < 10; ++i)
                {
                    g_autofree gchar *value = g_strdup_printf ("item %u", i);

                    items = g_list_append (items, g_paste_text_item_new (value));
                }

                g_paste_storage_backend_write_history (backend, big_name, items);
                g_list_free_full (items, g_object_unref);
            }

            /* The limit drops well below what is stored, as it would after the
             * user lowered it in the preferences: to the schema's minimum,
             * which a lower value would be refused for, leaving the limit
             * where it was. */
            g_paste_settings_set_max_history_size (settings, 5);
            g_assert_cmpuint (g_paste_settings_get_max_history_size (settings), ==, 5);

            {
                g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, old_passphrase);

                g_assert_true (g_paste_storage_backend_rekey (backend, big_name, new_passphrase, NULL));
            }

            g_paste_settings_set_max_history_size (settings, 10);

            {
                g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, new_passphrase);
                g_autolist (GPasteItem) loaded = read_history_ok (backend, big_name);
                g_assert_cmpuint (g_list_length (loaded), ==, 10);
            }
        }

        /* A backend that does not implement it at all is refused by the wrapper
         * rather than quietly reporting a success that never happened. */
        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_NOOP, settings);

            g_assert_false (g_paste_storage_backend_rekey (backend, name, new_passphrase, NULL));
        }

        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*not encrypted*");
}
#endif

#ifdef G_PASTE_ENABLE_SQLITE
/* Read @name through a fresh backend of @kind until its stored values exactly
 * match @values, front to back (the saver persists in the background, so
 * intermediate states — e.g. a matching length — are not enough to know it
 * caught up). */
static gboolean
sqlite_wait_for_values (GPasteStorage        kind,
                        GPasteSettings      *settings,
                        const gchar         *name,
                        const gchar * const *values,
                        guint                max_ms)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (kind, settings);
    guint expected = g_strv_length ((GStrv) values);

    for (guint i = 0; i < max_ms; ++i)
    {
        pump_once ();

        g_autolist (GPasteItem) loaded = read_history (backend, name);

        gboolean match = (g_list_length (loaded) == expected);
        const GList *l = loaded;

        for (guint v = 0; match && v < expected; ++v, l = l->next)
            match = g_paste_str_equal (g_paste_item_get_value (l->data), values[v]);

        if (match)
            return TRUE;

        g_usleep (1000);
    }

    return FALSE;
}

/* The SQLite backend must round-trip every item kind with full data parity
 * with the plain XML backend: kind, uuid, value, image date+checksum, special
 * values (mime + bytes, in order) — and skip password items just like it. */
static void
test_sqlite_roundtrip (void)
{
    const gchar *name = "sqlite-roundtrip";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

    g_assert_true (g_paste_storage_backend_is_incremental (backend));

    GPasteItem *text = g_paste_text_item_new ("plain text entry");
    g_paste_item_add_special_value (text, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>hi</b>", 9)));
    g_paste_item_add_special_value (text, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_GNOME_COPIED_FILES,
                                                                   g_bytes_new_static ("copy\nfile:///tmp/x", 18)));

    /* An image item loaded by path reads its file at construction, so write a
     * real PNG into our throwaway data dir. */
    g_autoptr (GBytes) orig_png = test_png_bytes ();
    gsize png_len = 0;
    gconstpointer png = g_bytes_get_data (orig_png, &png_len);
    g_autofree gchar *png_path = g_build_filename (g_get_user_data_dir (), "gpaste-test-image.png", NULL);
    g_assert_true (g_file_set_contents (png_path, (const gchar *) png, png_len, NULL));

    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);

    GList *items = NULL;
    items = g_list_append (items, text);
    items = g_list_append (items, g_paste_uris_item_new_from_str ("file:///tmp/a\nfile:///tmp/b"));
    items = g_list_append (items, g_paste_color_item_new_from_str ("rgb(255,0,0)"));
    items = g_list_append (items, g_paste_image_item_new_from_file (png_path, date,
                                                                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    items = g_list_append (items, g_paste_password_item_new ("my login", "s3cr3t", 0));

    g_paste_storage_backend_write_history (backend, name, items);

    /* The database exists and the history is listed. */
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, NULL);
    g_assert_true (g_strv_contains ((const gchar * const *) names, name));

    g_autolist (GPasteItem) loaded = read_history (backend, name);

    /* The password entry is not persisted; everything else is, in order. */
    g_assert_cmpuint (g_list_length (loaded), ==, 4);

    const GList *l = loaded;
    const GList *o = items;

    for (guint i = 0; i < 4; ++i, l = l->next, o = o->next)
    {
        GPasteItem *read = l->data;
        GPasteItem *orig = o->data;

        g_assert_cmpint (g_paste_item_get_kind (read), ==, g_paste_item_get_kind (orig));
        g_assert_cmpstr (g_paste_item_get_uuid (read), ==, g_paste_item_get_uuid (orig));
        /* An image comes back from its stored blob, re-anchored at its
         * canonical cache path — its bytes are compared below instead. */
        if (g_paste_item_get_kind (read) != G_PASTE_ITEM_KIND_IMAGE)
            g_assert_cmpstr (g_paste_item_get_real_value (read), ==, g_paste_item_get_real_value (orig));
    }

    /* Special values survive with their mimes, bytes and order. */
    const GSList *read_svs = g_paste_item_get_special_values (loaded->data);
    const GSList *orig_svs = g_paste_item_get_special_values (items->data);

    g_assert_cmpuint (g_slist_length ((GSList *) read_svs), ==, 2);

    for (; read_svs && orig_svs; read_svs = read_svs->next, orig_svs = orig_svs->next)
    {
        GPasteBinaryData *read_sv = read_svs->data;
        GPasteBinaryData *orig_sv = orig_svs->data;

        g_assert_cmpint (g_paste_binary_data_get_mime (read_sv), ==, g_paste_binary_data_get_mime (orig_sv));
        g_assert_true (g_bytes_equal (g_paste_binary_data_get_bytes (read_sv), g_paste_binary_data_get_bytes (orig_sv)));
    }

    /* Image metadata survives, and the image itself comes back as the blob the
     * backend stored — byte-identical to the source PNG, so the item no longer
     * depends on any file on disk. */
    GPasteImageItem *image = G_PASTE_IMAGE_ITEM (g_list_nth_data (loaded, 3));

    g_assert_cmpint (g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (image)), ==, 1234567890);
    g_assert_cmpstr (g_paste_image_item_get_checksum (image), ==,
                     "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

    GBytes *read_png = g_paste_image_item_get_png_bytes (image);

    g_assert_nonnull (read_png);
    g_assert_true (g_bytes_equal (read_png, orig_png));

    g_list_free_full (items, g_object_unref);

    /* Deleting the history removes the database. */
    g_paste_storage_backend_delete_history (backend, name, NULL);
    g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
}

/* Drive a GPasteHistory backed by SQLite through the incremental save path:
 * adds, dedup, select, remove, eviction past max size, and empty must all end
 * up reflected in the database. */
static void
test_sqlite_incremental (void)
{
    const gchar *name = "sqlite-incr";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_history_size (settings, 5);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, name);

    g_paste_history_add (history, g_paste_text_item_new ("a"));
    g_autofree gchar *uuid_a = g_strdup (g_paste_item_get_uuid (g_paste_history_get (history, 0)));
    g_paste_history_add (history, g_paste_text_item_new ("b"));
    g_paste_history_add (history, g_paste_text_item_new ("c"));
    /* Dedup: no new row, "b" moves to the front. */
    g_paste_history_add (history, g_paste_text_item_new ("b"));
    /* Select: "a" moves to the front. */
    g_assert_true (g_paste_history_select (history, uuid_a));
    /* Remove "c" (now at index 2: [a, b, c]). */
    g_paste_history_remove (history, 2);

    const gchar * const after_ops[] = { "a", "b", NULL };
    g_assert_true (sqlite_wait_for_values (G_PASTE_STORAGE_SQLITE, settings, name, after_ops, 5000));

    /* Overflow past max-history-size: evicted tail rows must be reconciled
     * away even though they get no remove operation of their own. */
    for (guint i = 0; i < 6; ++i)
    {
        g_autofree gchar *text = g_strdup_printf ("item-%u", i);
        g_paste_history_add (history, g_paste_text_item_new (text));
    }

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 5);

    const gchar * const after_overflow[] = { "item-5", "item-4", "item-3", "item-2", "item-1", NULL };
    g_assert_true (sqlite_wait_for_values (G_PASTE_STORAGE_SQLITE, settings, name, after_overflow, 5000));

    /* Emptying clears the database but keeps the history listed. Flush so the
     * clear is persisted, then check the listing *before* reading the history
     * back: list_histories only enumerates the .db file on disk, whereas a read
     * would recreate it (SQLITE_OPEN_CREATE) and mask an erroneous file deletion,
     * making this assertion pass no matter what empty() did. */
    g_assert_true (g_paste_history_empty (history, NULL));
    g_paste_history_flush (history);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, NULL);
    g_assert_true (g_strv_contains ((const gchar * const *) names, name));

    /* And the database really is empty (this read may recreate the file, so it
     * has to come after the listing check above). */
    const gchar * const after_empty[] = { NULL };
    g_assert_true (sqlite_wait_for_values (G_PASTE_STORAGE_SQLITE, settings, name, after_empty, 5000));
}

/* replace_item must swap the row's content in place: the new item inherits the
 * old one's position, its special values replace the old ones, replacing a
 * never-persisted uuid is a no-op, and an item turning into a password must
 * vanish from storage like the plain XML backend drops passwords on rewrite. */
static void
test_sqlite_replace (void)
{
    const gchar *name = "sqlite-replace";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

    GPasteItem *middle = g_paste_text_item_new ("middle");
    g_paste_item_add_special_value (middle, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                     g_bytes_new_static ("<i>old</i>", 10)));

    GList *items = NULL;
    items = g_list_append (items, g_paste_text_item_new ("front"));
    items = g_list_append (items, middle);
    items = g_list_append (items, g_paste_text_item_new ("back"));

    g_paste_storage_backend_write_history (backend, name, items);

    /* Replace the middle item: new uuid, new value, different special values. */
    g_autoptr (GPasteItem) replacement = g_paste_text_item_new ("replaced");
    g_autoptr (GBytes) replacement_sv = g_bytes_new_static ("copy\nfile:///tmp/y", 18);

    g_paste_item_add_special_value (replacement, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_GNOME_COPIED_FILES,
                                                                          g_bytes_ref (replacement_sv)));

    /* The backend is incremental, so the fallback snapshot is never used. */
    g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (middle), replacement, NULL);

    /* Replacing a uuid that was never persisted must change nothing. */
    g_autoptr (GPasteItem) usurper = g_paste_text_item_new ("usurper");

    g_paste_storage_backend_replace_item (backend, name, "e8a95b3d-33ee-4b1e-8ee3-9c22c9635b8d", usurper, NULL);

    g_autolist (GPasteItem) loaded = read_history (backend, name);

    g_assert_cmpuint (g_list_length (loaded), ==, 3);
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (loaded, 0)), ==, "front");
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (loaded, 1)), ==, "replaced");
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (loaded, 2)), ==, "back");
    g_assert_cmpstr (g_paste_item_get_uuid (g_list_nth_data (loaded, 1)), ==, g_paste_item_get_uuid (replacement));

    /* The old special value is gone, the replacement's is on file. */
    const GSList *svs = g_paste_item_get_special_values (g_list_nth_data (loaded, 1));

    g_assert_cmpuint (g_slist_length ((GSList *) svs), ==, 1);
    g_assert_cmpint (g_paste_binary_data_get_mime (svs->data), ==, G_PASTE_SPECIAL_MIME_GNOME_COPIED_FILES);
    g_assert_true (g_bytes_equal (g_paste_binary_data_get_bytes (svs->data), replacement_sv));

    /* make_password turns an item into a password: it must leave storage, not
     * linger as a stale plaintext row. */
    g_autoptr (GPasteItem) password = g_paste_password_item_new ("login", "s3cr3t", 0);

    g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (replacement), password, NULL);

    g_autolist (GPasteItem) after_password = read_history (backend, name);

    g_assert_cmpuint (g_list_length (after_password), ==, 2);
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (after_password, 0)), ==, "front");
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (after_password, 1)), ==, "back");

    g_list_free_full (items, g_object_unref);
}

/* Open a second, read-only connection on @path and run a single-integer query;
 * WAL lets it observe the backend's committed writes. */
static gint64
sqlite_raw_count (const gchar *path,
                  const gchar *sql)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;

    g_assert_cmpint (sqlite3_open_v2 (path, &db, SQLITE_OPEN_READONLY, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_step (stmt), ==, SQLITE_ROW);

    gint64 count = sqlite3_column_int64 (stmt, 0);

    sqlite3_finalize (stmt);
    sqlite3_close (db);

    return count;
}

/* An incremental backend only ever learns what an update tells it, so the strip
 * has to raise one: the model-level test above flushes, and a flush rewrites the
 * whole history whatever happened to it, which would hide a strip that notified
 * nobody. Here the rows have to go on their own. */
static void
test_sqlite_strip_rich_text (void)
{
    const gchar *name = "sqlite-strip-rich-text";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_history_size (settings, 5);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, name);

    GPasteItem *rich = g_paste_text_item_new ("rich");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>rich</b>", 11)));
    g_paste_history_add (history, rich);

    const gchar * const one[] = { "rich", NULL };
    g_assert_true (sqlite_wait_for_values (G_PASTE_STORAGE_SQLITE, settings, name, one, 5000));

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

    {
        g_autolist (GPasteItem) before = read_history (backend, name);

        g_assert_cmpuint (g_slist_length ((GSList *) g_paste_item_get_special_values (before->data)), ==, 1);
    }

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_assert_true (g_paste_history_strip_rich_text (history, uuid, NULL));

    /* No flush: the write has to come from the update the strip raised. */
    gboolean gone = FALSE;

    for (guint i = 0; !gone && i < 5000; ++i)
    {
        pump_once ();

        g_autolist (GPasteItem) loaded = read_history (backend, name);

        gone = (g_list_length (loaded) == 1 && !g_paste_item_get_special_values (loaded->data));

        if (!gone)
            g_usleep (1000);
    }

    g_assert_true (gone);
}

/* Version 2 added the favourite column and version 3 the password timeout, and
 * migrate_schema is what carries a database written by an older GPaste across.
 * Nothing else exercises that path: every database the other tests touch is
 * created at the current version, so build a v1 one by hand and prove both
 * halves — both columns appear, one step per version and each falling through to
 * the next, and the rows that were already there survive the lot. */
static void
test_sqlite_schema_migration (void)
{
    const gchar *name = "sqlite-schema-migration";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");

    g_assert_true (g_paste_util_ensure_history_dir_exists ());

    /* Version 1, verbatim: no favourite column, and user_version says so. */
    {
        sqlite3 *db = NULL;

        g_assert_cmpint (sqlite3_open_v2 (path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db,
                                       "CREATE TABLE items ("
                                       "    id       INTEGER PRIMARY KEY,"
                                       "    uuid     TEXT    NOT NULL UNIQUE,"
                                       "    kind     TEXT    NOT NULL,"
                                       "    value    TEXT    NOT NULL,"
                                       "    rank     INTEGER NOT NULL,"
                                       "    date     INTEGER,"
                                       "    checksum TEXT,"
                                       "    name     TEXT,"
                                       "    image    BLOB"
                                       ");"
                                       "CREATE UNIQUE INDEX items_rank ON items (rank DESC);"
                                       "CREATE TABLE special_values ("
                                       "    item_id  INTEGER NOT NULL REFERENCES items (id) ON DELETE CASCADE,"
                                       "    position INTEGER NOT NULL,"
                                       "    mime     TEXT    NOT NULL,"
                                       "    data     BLOB    NOT NULL,"
                                       "    PRIMARY KEY (item_id, position)"
                                       ");"
                                       "INSERT INTO items (uuid, kind, value, rank) VALUES"
                                       " ('11111111-1111-4111-8111-111111111111', 'Text', 'older', 1);"
                                       "PRAGMA user_version = 1;",
                                       NULL, NULL, NULL), ==, SQLITE_OK);
        sqlite3_close (db);
    }

    /* Opening it is what migrates it. */
    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
        GList *history = NULL;
        gsize size = 0;

        g_assert_true (g_paste_storage_backend_read_history (backend, name, NULL /* cancellable */, &history, &size));
        g_assert_cmpuint (g_list_length (history), ==, 1);

        GPasteItem *item = history->data;

        g_assert_cmpstr (g_paste_item_get_value (item), ==, "older");
        /* Everything an older GPaste stored comes back unpinned, which is what
         * it was: that is what the column default is for. */
        g_assert_false (g_paste_item_is_favourite (item));
        g_assert_cmpstr (g_paste_item_get_uuid (item), ==, "11111111-1111-4111-8111-111111111111");

        g_list_free_full (history, g_object_unref);
    }

    g_assert_cmpint (sqlite_raw_count (path, "PRAGMA user_version;"), ==, 3);
    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM pragma_table_info ('items') WHERE name = 'favourite';"), ==, 1);
    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM pragma_table_info ('items') WHERE name = 'timeout';"), ==, 1);
    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM pragma_table_info ('items') WHERE name = 'date' AND \"notnull\" = 0;"), ==, 1);
    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM items WHERE date IS NULL;"), ==, 1);
}

/* remove_item relies on the FK cascade to clean an item's special values, and
 * foreign_keys is a per-connection pragma that fails silent: prove it is
 * actually ON for the backend's connection instead of leaking orphaned blobs. */
static void
test_sqlite_cascade (void)
{
    const gchar *name = "sqlite-cascade";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

    GPasteItem *rich = g_paste_text_item_new ("rich");
    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>rich</b>", 11)));
    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_GNOME_COPIED_FILES,
                                                                   g_bytes_new_static ("copy\nfile:///tmp/z", 18)));

    GPasteItem *survivor = g_paste_text_item_new ("survivor");
    g_paste_item_add_special_value (survivor, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                       g_bytes_new_static ("<u>keep</u>", 11)));

    GList *items = NULL;
    items = g_list_append (items, rich);
    items = g_list_append (items, survivor);

    g_paste_storage_backend_write_history (backend, name, items);

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");

    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM special_values;"), ==, 3);

    /* The backend is incremental, so the fallback snapshot is never used. */
    g_paste_storage_backend_remove_item (backend, name, g_paste_item_get_uuid (rich), NULL);

    /* Only the survivor and its own special value remain. */
    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM items;"), ==, 1);
    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM special_values;"), ==, 1);

    g_list_free_full (items, g_object_unref);
}

/* A database keeps its images in the store, so deleting the last file flavour
 * of a name takes the images directory with it, a database of the same name
 * notwithstanding: a migration from the plain flavour leaves no screenshot in
 * clear. */
static void
test_sqlite_migration_takes_source_images (void)
{
    const gchar *name = "sqlite-migrate-source-images";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autofree gchar *png = write_texts_and_image (settings, name, 0, 41);
    g_autoptr (GPasteStorageBackend) source = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autoptr (GPasteStorageBackend) destination = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) items = read_history_ok (source, name);

    g_autofree gchar *images = g_paste_file_backend_images_dir (name);

    g_paste_storage_backend_write_history (destination, name, items);
    g_paste_storage_backend_delete_history (source, name, NULL);
    g_assert_false (g_file_test (png, G_FILE_TEST_EXISTS));
    g_assert_false (g_file_test (images, G_FILE_TEST_EXISTS));

    g_autolist (GPasteItem) imported = read_history_ok (destination, name);

    g_assert_cmpuint (g_list_length (imported), ==, 1);
}

/* What the plain database flavour deletes is in no file afterwards: a text made a
 * password, an item deleted, a text edited, rich text stripped, a history
 * emptied. Neither the database, whose freed cells SQLite keeps unless told
 * otherwise, nor the write-ahead log, which holds the pages they were on for as
 * long as the connection lives. */
static void
test_sqlite_deleted_text_gone (void)
{
    const gchar *name = "sqlite-deleted-text-gone";
    const gchar *texts[] = { "made-a-password-52", "deleted-52", "edited-52", "stripped-52", "emptied-52" };
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autofree gchar *db = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autofree gchar *wal = g_strconcat (db, "-wal", NULL);
    g_autolist (GPasteItem) items = NULL;

    for (guint i = 0; i < G_N_ELEMENTS (texts); ++i)
    {
        /* The one to strip carries the text as its rich alternative alone. */
        GPasteItem *item = g_paste_text_item_new ((i == 3) ? "stripped" : texts[i]);

        if (i == 3)
            g_paste_item_add_special_value (item, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML, g_bytes_new_static (texts[i], strlen (texts[i]))));
        items = g_list_append (items, item);
    }
    g_paste_storage_backend_write_history (backend, name, items);
    for (guint i = 0; i < G_N_ELEMENTS (texts); ++i)
        g_assert_true (file_contains (db, texts[i]) || file_contains (wal, texts[i]));

    g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "password-52", 0);
    g_autoptr (GPasteItem) edited = g_paste_text_item_new ("edited into another");
    g_autoptr (GPasteItem) stripped = g_paste_text_item_new ("stripped");

    g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (g_list_nth_data (items, 0)), password, NULL);
    g_paste_storage_backend_remove_item (backend, name, g_paste_item_get_uuid (g_list_nth_data (items, 1)), NULL);
    g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (g_list_nth_data (items, 2)), edited, NULL);
    g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (g_list_nth_data (items, 3)), stripped, NULL);
    for (guint i = 0; i < 4; ++i)
    {
        g_assert_false (file_contains (db, texts[i]));
        g_assert_false (file_contains (wal, texts[i]));
    }

    g_paste_storage_backend_clear_history (backend, name, NULL);
    g_assert_false (file_contains (db, texts[4]));
    g_assert_false (file_contains (wal, texts[4]));
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* The encrypted database flavour stores an image's checksum in clear, which a
 * delete leaves in no file either. */
static void
test_sqlite_encrypted_deleted_checksum_gone (void)
{
    const gchar *name = "sqlite-deleted-checksum-gone";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, "passphrase");
    g_autofree gchar *db = g_paste_storage_backend_get_history_file_path (backend, name);
    g_autofree gchar *wal = g_strconcat (db, "-wal", NULL);
    g_autoptr (GBytes) png = test_png_bytes_colored (52, 53, 54);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));
    g_autofree gchar *checksum = g_strdup (g_paste_image_item_get_checksum (items->data));

    items = g_list_append (items, g_paste_text_item_new ("kept"));
    g_paste_storage_backend_write_history (backend, name, items);
    g_assert_true (file_contains (db, checksum) || file_contains (wal, checksum));

    g_paste_storage_backend_remove_item (backend, name, g_paste_item_get_uuid (items->data), NULL);
    g_assert_false (file_contains (db, checksum));
    g_assert_false (file_contains (wal, checksum));

    /* Gone, rather than left to the cases after this one under a passphrase
     * of its own: the data directory is the whole binary's. */
    g_paste_storage_backend_delete_history (backend, name, NULL);
}
#endif

/* The images/<name>/ directory belongs to the history *name*, shared across
 * backend flavors: a migration imports into the destination and then deletes
 * the source under the same name, which must not sweep the images the
 * destination just materialized. Only the last flavor to store the name may
 * take the directory with it. */
static void
test_sqlite_migration_keeps_destination_images (void)
{
    const gchar *name = "sqlite-migrate-images";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes_colored (22, 23, 24);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    GList *items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));

    g_assert_nonnull (items->data);

    g_autoptr (GPasteStorageBackend) source = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autoptr (GPasteStorageBackend) destination = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

    /* What a sqlite -> file migration does: import, then clean the source up. */
    g_paste_storage_backend_write_history (source, name, items);
    g_paste_storage_backend_write_history (destination, name, items);

    g_autofree gchar *image_path = g_paste_file_backend_image_path (name,
                                                                    g_paste_image_item_get_checksum (items->data));

    g_assert_true (g_file_test (image_path, G_FILE_TEST_EXISTS));

    g_paste_storage_backend_delete_history (source, name, NULL);

    /* The destination still stores the name: its images must survive... */
    g_assert_true (g_file_test (image_path, G_FILE_TEST_EXISTS));

    g_autolist (GPasteItem) loaded = read_history (destination, name);
    g_assert_cmpuint (g_list_length (loaded), ==, 1);

    /* ...and go away with the last flavor storing it. */
    g_paste_storage_backend_delete_history (destination, name, NULL);
    g_assert_false (g_file_test (image_path, G_FILE_TEST_EXISTS));

    g_list_free_full (items, g_object_unref);
}

/* An image row whose date GDateTime cannot hold is dropped like one with no
 * date, the rest of the history read with it, rather than reaching the image
 * constructor's own refusal and taking the whole history down. */
static void
test_sqlite_image_date_out_of_range (void)
{
    const gchar *name = "sqlite-image-date";
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes_colored (54, 55, 56);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));

    items = g_list_append (items, g_paste_text_item_new ("kept"));

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

        g_paste_storage_backend_write_history (backend, name, items);
    }

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");
    sqlite3 *db = NULL;

    /* Past year 9999. */
    g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
    g_assert_cmpint (sqlite3_exec (db, "UPDATE items SET date = 100000000000000 WHERE kind = 'Image';", NULL, NULL, NULL), ==, SQLITE_OK);
    sqlite3_close (db);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) loaded = read_history_ok (backend, name);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, "kept");
}

/* The stored blob is the source of truth for images: an item must survive the
 * complete absence of its on-disk cache file — including rebuilding its
 * texture — while a plain database visibly contains the PNG. */
static void
test_sqlite_image_blob (void)
{
    const gchar *name = "sqlite-image-blob";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes_colored (10, 11, 12);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    GList *items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));

    g_assert_nonnull (items->data);

    /* The bytes-built item never touches the disk: it names no file at all, and
     * the one the file backend would have written for it does not exist. */
    g_assert_null (g_paste_image_item_get_cache_path (items->data));

    g_autofree gchar *cache_path = g_paste_file_backend_image_path (name,
                                                                    g_paste_image_item_get_checksum (items->data));

    g_assert_false (g_file_test (cache_path, G_FILE_TEST_EXISTS));

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);

        g_paste_storage_backend_write_history (backend, name, items);
        /* Dropping the backend checkpoints the WAL for the scan below. */
    }

    /* A plain database carries the PNG in clear (its IHDR chunk shows). */
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");

    g_assert_true (file_contains (path, "IHDR"));

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autolist (GPasteItem) loaded = read_history (backend, name);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);

    GPasteItem *read = loaded->data;
    GPasteImageItem *image = G_PASTE_IMAGE_ITEM (read);
    GBytes *read_png = g_paste_image_item_get_png_bytes (image);

    g_assert_cmpint (g_paste_item_get_kind (read), ==, G_PASTE_ITEM_KIND_IMAGE);
    g_assert_nonnull (read_png);
    g_assert_true (g_bytes_equal (read_png, png));
    g_assert_cmpstr (g_paste_image_item_get_checksum (image),
                     ==, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (items->data)));

    /* The texture rebuilds from the bytes, with no file anywhere. */
    g_assert_null (g_paste_image_item_get_cache_path (image));
    g_assert_false (g_file_test (cache_path, G_FILE_TEST_EXISTS));
    g_paste_item_set_state (read, G_PASTE_ITEM_STATE_ACTIVE);
    g_assert_nonnull (g_paste_image_item_get_image (image));
    g_paste_item_set_state (read, G_PASTE_ITEM_STATE_IDLE);

    g_list_free_full (items, g_object_unref);
}

/* Switching histories loads with save_after=TRUE so a snapshot-rewriting
 * backend persists its read-time normalization — for an incremental backend
 * that full rewrite is skipped: the database must keep rows beyond
 * max-history-size (only memory is capped) instead of being truncated by a
 * write-back of the capped read. */
static void
test_sqlite_no_rewrite_on_switch (void)
{
    const gchar *big_name = "sqlite-switch-big";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);
    g_paste_settings_set_max_history_size (settings, 100);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

    /* Seed a history holding more items than the soon-to-be max size. */
    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
        GList *items = NULL;

        for (guint i = 0; i < 7; ++i)
        {
            g_autofree gchar *text = g_strdup_printf ("big-%u", i);
            items = g_list_append (items, g_paste_text_item_new (text));
        }

        g_paste_storage_backend_write_history (backend, big_name, items);
        g_list_free_full (items, g_object_unref);
    }

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "sqlite-switch-start");
    g_paste_settings_set_max_history_size (settings, 5);

    /* Switch through the settings key: this is the path that loads with
     * save_after=TRUE (g_paste_history_load itself never writes back). */
    g_paste_settings_set_history_name (settings, big_name);
    g_assert_true (pump_until_length (history, 5, 5000));

    /* Flush the saver: a (wrongly) recorded post-load rewrite would be applied
     * right here, so the assertion below catches it deterministically. */
    g_paste_history_flush (history);

    /* Memory is capped at 5, but all 7 rows must still be in the database. */
    g_autofree gchar *path = g_paste_util_get_history_file_path (big_name, "db");

    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM items;"), ==, 7);
}

/* Evictions a replacement brings along go as a rewrite of the whole history: a
 * replace operation alone would leave their rows in an incremental store, to
 * come back on the next load. */
static void
test_sqlite_replace_evictions (void)
{
    const gchar *name = "sqlite-replace-evictions";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_memory_usage (settings, 5);
    g_paste_settings_set_max_history_size (settings, 100);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autofree gchar *medium = g_strnfill (2 * 1024 * 1024, 'M');
    g_autofree gchar *large = g_strnfill (4 * 1024 * 1024, 'L');

    g_paste_history_load (history, name);
    g_paste_history_add (history, g_paste_text_item_new ("old"));

    g_autofree gchar *uuid = dup_uuid_at (history, 0);

    g_paste_history_add (history, g_paste_text_item_new (medium));
    g_paste_history_add (history, g_paste_text_item_new ("head"));

    g_autofree gchar *replacement = g_paste_history_replace (history, uuid, large);

    g_assert_nonnull (replacement);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_paste_history_flush (history);

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");

    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM items;"), ==, 2);
    g_paste_settings_reset (settings, G_PASTE_MAX_MEMORY_USAGE_SETTING);
}

/* The read-write connections opened on @counted_path, by any thread. */
static const gchar *counted_path = NULL;
static gint counted_opens = 0;

int
sqlite3_open_v2 (const char *filename,
                 sqlite3   **db,
                 int         flags,
                 const char *vfs)
{
    typedef int (*Open) (const char *, sqlite3 **, int, const char *);
    Open original = (Open) dlsym (RTLD_NEXT, "sqlite3_open_v2");

    g_assert_nonnull (original);
    if ((flags & SQLITE_OPEN_READWRITE) && g_paste_str_equal (filename, counted_path))
        g_atomic_int_inc (&counted_opens);
    return original (filename, db, flags, vfs);
}

/* Saving the history under another name -- a backup of the current one --
 * leaves the connection its later writes go through alone: opened again, on an
 * encrypted store, it would take an Argon2id pass. */
static void
test_sqlite_save_elsewhere_keeps_connection (void)
{
    const gchar *name = "sqlite-kept-connection";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);
    g_paste_settings_set_max_history_size (settings, 100);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");

    g_paste_history_load (history, name);
    g_paste_history_add (history, g_paste_text_item_new ("before"));
    g_paste_history_flush (history);
    g_paste_history_resume (history);

    counted_path = path;
    g_atomic_int_set (&counted_opens, 0);
    g_paste_history_save (history, "sqlite-kept-connection-copy");
    g_paste_history_add (history, g_paste_text_item_new ("after"));
    g_paste_history_flush (history);
    counted_path = NULL;

    g_assert_cmpint (g_atomic_int_get (&counted_opens), ==, 0);
    g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM items;"), ==, 2);
}

/* A database created by a newer GPaste (higher user_version) must be left
 * alone: reads come back empty and writes are dropped instead of clobbering. */
static void
test_sqlite_version_guard (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "sqlite-newer";

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
            GList *items = g_list_append (NULL, g_paste_text_item_new ("precious"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);
        }

        g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");
        sqlite3 *db = NULL;
        sqlite3_stmt *stmt = NULL;
        gint64 current_version = 0;

        /* Remember the real schema version to restore it after tampering. */
        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_prepare_v2 (db, "PRAGMA user_version;", -1, &stmt, NULL), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_step (stmt), ==, SQLITE_ROW);
        current_version = sqlite3_column_int64 (stmt, 0);
        sqlite3_finalize (stmt);
        g_assert_cmpint (sqlite3_exec (db, "PRAGMA user_version = 99;", NULL, NULL, NULL), ==, SQLITE_OK);
        sqlite3_close (db);

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
            g_autolist (GPasteItem) loaded = read_history (backend, name);
            g_assert_null (loaded);

            /* A write against the newer database must not touch it. */
            GList *items = g_list_append (NULL, g_paste_text_item_new ("usurper"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);
        }

        /* Once the version is back within range, the original data is intact. */
        g_autofree gchar *restore = g_strdup_printf ("PRAGMA user_version = %" G_GINT64_FORMAT ";", current_version);

        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, restore, NULL, NULL, NULL), ==, SQLITE_OK);
        sqlite3_close (db);

        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
        g_autolist (GPasteItem) loaded = read_history (backend, name);
        g_assert_cmpuint (g_list_length (loaded), ==, 1);
        g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, "precious");

        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*newer GPaste*");
}

/* Emptying a history whose database will not open -- a newer GPaste's -- writes
 * over it as the file flavour does, rather than leave every item in it while
 * answering that it was emptied: an empty database in its place, which takes
 * what is copied next. */
static void
test_sqlite_empty_unopenable (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "sqlite-empty-newer";
        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");

        g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_SQLITE);

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
            g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("precious"));

            g_assert_true (g_paste_storage_backend_write_history (backend, name, items));
        }

        sqlite3 *db = NULL;

        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, "PRAGMA user_version = 99;", NULL, NULL, NULL), ==, SQLITE_OK);
        sqlite3_close (db);

        g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

        g_paste_history_load (history, name);
        g_assert_true (g_paste_history_is_unreadable (history));
        g_assert_true (g_paste_history_empty (history, NULL));
        g_paste_history_add (history, g_paste_text_item_new ("next"));
        g_paste_history_flush (history);

        g_assert_cmpint (sqlite_raw_count (path, "PRAGMA user_version;"), <, 99);
        g_assert_cmpint (sqlite_raw_count (path, "SELECT COUNT (*) FROM items;"), ==, 1);

        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*newer GPaste*");
}
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* Two histories of the same flavor, keyed differently -- the state a rekey that
 * fails partway through leaves behind, and which the migration warns about by
 * name. A passphrase that opens one of them but not the other must be refused:
 * accepting it would load the one it cannot open as empty, and that history's
 * next save would then destroy the real content -- which is what answering on
 * the first history that opens would do. */
static void
test_encrypted_split_keys_refuse_passphrase (void)
{
    const struct {
        GPasteStorage kind;
        const gchar  *first;
        const gchar  *second;
    } flavors[] = {
        { G_PASTE_STORAGE_ENCRYPTED_FILE, "split-file-a", "split-file-b" },
#ifdef G_PASTE_ENABLE_SQLITE
        { G_PASTE_STORAGE_ENCRYPTED_SQLITE, "split-db-a", "split-db-b" },
#endif
    };

    for (gsize i = 0; i < G_N_ELEMENTS (flavors); ++i)
    {
        const gchar *one = "passphrase one";
        const gchar *two = "passphrase two";

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        /* Both hold data, so neither can be silently re-keyed on open. */
        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (flavors[i].kind, settings, one);
            GList *items = g_list_append (NULL, g_paste_text_item_new ("first"));

            g_paste_storage_backend_write_history (backend, flavors[i].first, items);
            g_list_free_full (items, g_object_unref);
        }

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (flavors[i].kind, settings, two);
            GList *items = g_list_append (NULL, g_paste_text_item_new ("second"));

            g_paste_storage_backend_write_history (backend, flavors[i].second, items);
            g_list_free_full (items, g_object_unref);
        }

        /* Each opens its own and not the other, whichever is enumerated first. */
        g_assert_false (g_paste_storage_passphrase_can_decrypt (flavors[i].kind, settings, one));
        g_assert_false (g_paste_storage_passphrase_can_decrypt (flavors[i].kind, settings, two));

        /* Both must go: every test in this binary shares one XDG_DATA_HOME, and
         * a pair no single passphrase opens would make every later can_decrypt
         * refuse -- which is precisely the behaviour under test.
         *
         * Not asserted for the same reason: that a passphrase opening
         * everything is accepted. The scan sees whatever the other tests have
         * left lying around under their own passphrases. */
        g_autoptr (GPasteStorageBackend) cleanup_one = g_paste_storage_backend_new_with_passphrase (flavors[i].kind, settings, one);
        g_autoptr (GPasteStorageBackend) cleanup_two = g_paste_storage_backend_new_with_passphrase (flavors[i].kind, settings, two);

        g_paste_storage_backend_delete_history (cleanup_one, flavors[i].first, NULL);
        g_paste_storage_backend_delete_history (cleanup_two, flavors[i].second, NULL);
    }
}
#endif

#if defined(G_PASTE_ENABLE_SQLITE) && defined(G_PASTE_ENABLE_ENCRYPTION)
/* The Argon2 parameters live in the store's own `meta` table, so anything that
 * can write to $XDG_DATA_HOME dictates them. An absurd opslimit would otherwise
 * have crypto_pwhash run effectively forever -- a denial of service just from
 * opening the history -- so deriving a key with them must be refused outright
 * rather than attempted. The real data must survive the refusal. */
static void
test_encrypted_sqlite_absurd_kdf_params (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "sqlite-absurd-kdf";
        const gchar *passphrase = "correct horse battery staple";

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, passphrase);
            GList *items = g_list_append (NULL, g_paste_text_item_new ("precious"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);
        }

        g_autofree gchar *path = g_paste_util_get_history_file_path (name, "dbs");
        sqlite3 *db = NULL;

        /* Way past crypto_pwhash_OPSLIMIT_SENSITIVE. Deriving with this is what
         * we must never get as far as doing. */
        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, "UPDATE meta SET value = 4000000000 WHERE key = 'opslimit';", NULL, NULL, NULL), ==, SQLITE_OK);
        sqlite3_close (db);

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, passphrase);
            g_autolist (GPasteItem) loaded = read_history (backend, name);
            g_assert_null (loaded);

            /* And the refusal must not let a write clobber what is still there. */
            GList *items = g_list_append (NULL, g_paste_text_item_new ("usurper"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);
        }

        /* Note what is deliberately *not* asserted here: that the passphrase is
         * rejected. A store we cannot open is corrupt, not proof of a wrong
         * passphrase, and saying otherwise would tell the user their correct one
         * is wrong. Refusing the read and the write is what keeps the data safe,
         * and that is what the restore below proves. */

        /* Put the real parameters back: the original data is untouched. */
        g_autofree gchar *restore = g_strdup_printf ("UPDATE meta SET value = %u WHERE key = 'opslimit';",
                                                     (guint) crypto_pwhash_OPSLIMIT_MODERATE);

        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, restore, NULL, NULL, NULL), ==, SQLITE_OK);
        sqlite3_close (db);

        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, passphrase);
        g_autolist (GPasteItem) loaded = read_history (backend, name);
        g_assert_cmpuint (g_list_length (loaded), ==, 1);
        g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, "precious");

        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*Unreasonable key-derivation parameters*");
}

/* The same parameters in a history holding nothing are no store to lose: it is
 * keyed afresh for the passphrase, as one with parameters missing is, rather
 * than refused for good -- no write would ever land in it again. */
static void
test_encrypted_sqlite_absurd_kdf_params_empty (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "sqlite-absurd-kdf-empty";
        const gchar *passphrase = "correct horse battery staple";
        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, passphrase);

            g_paste_storage_backend_write_history (backend, name, NULL);
        }

        g_autofree gchar *path = g_paste_util_get_history_file_path (name, "dbs");
        sqlite3 *db = NULL;

        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, "UPDATE meta SET value = 4000000000 WHERE key = 'opslimit';", NULL, NULL, NULL), ==, SQLITE_OK);
        sqlite3_close (db);

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, passphrase);
            GList *items = g_list_append (NULL, g_paste_text_item_new ("kept"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);
        }

        g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, passphrase);
        g_autolist (GPasteItem) loaded = read_history_ok (backend, name);

        g_assert_cmpuint (g_list_length (loaded), ==, 1);
        g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, "kept");
        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*Unreasonable key-derivation parameters*");
}

/* The encrypted SQLite flavor must round-trip everything the plain one does,
 * plus password items (real value and name), with no plaintext content ever
 * reaching the ".dbs" file. */
static void
test_encrypted_sqlite_roundtrip (void)
{
    const gchar *name = "sqlite-encrypted";
    const gchar *master = "the master passphrase";
    const gchar *secret = "s3cr3t-passw0rd";
    const gchar *pw_name = "my login";
    const gchar *text_value = "plain text entry";
    const gchar *html_value = "<b>hidden markup</b>";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes ();
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    GList *items = NULL;
    GPasteItem *text = g_paste_text_item_new (text_value);

    g_paste_item_add_special_value (text, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new_static (html_value, strlen (html_value))));
    items = g_list_append (items, text);
    items = g_list_append (items, g_paste_password_item_new (pw_name, secret, PASSWORD_TIMEOUT));
    items = g_list_append (items, g_paste_color_item_new_from_str ("rgb(0,255,0)"));
    items = g_list_append (items, g_paste_image_item_new_from_bytes (png, date, NULL));

    {
        g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, master);

        g_assert_true (g_paste_storage_backend_is_incremental (backend));
        g_paste_storage_backend_write_history (backend, name, items);

        g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, NULL);
        g_assert_true (g_strv_contains ((const gchar * const *) names, name));

        /* Dropping the backend closes the connection, checkpointing the WAL so
         * everything is in the main database file for the scan below. */
    }

    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "dbs");

    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));
    g_assert_false (file_contains (path, text_value));
    g_assert_false (file_contains (path, html_value));
    g_assert_false (file_contains (path, secret));
    g_assert_false (file_contains (path, pw_name));
    /* The image blob is encrypted too: no PNG chunk marker in clear. */
    g_assert_false (file_contains (path, "IHDR"));

    /* The passphrase verification is quiet and flavor-aware. */
    g_assert_true (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, master));
    g_assert_false (g_paste_storage_passphrase_can_decrypt (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, "not the passphrase"));

    g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, master);
    g_autolist (GPasteItem) loaded = read_history (backend, name);

    g_assert_cmpuint (g_list_length (loaded), ==, 4);

    GPasteItem *read_text = loaded->data;
    GPasteItem *read_password = loaded->next->data;
    GPasteItem *read_color = loaded->next->next->data;
    GPasteItem *read_image = loaded->next->next->next->data;

    g_assert_cmpstr (g_paste_item_get_value (read_text), ==, text_value);

    const GSList *svs = g_paste_item_get_special_values (read_text);
    g_assert_cmpuint (g_slist_length ((GSList *) svs), ==, 1);
    g_assert_cmpint (g_paste_binary_data_get_mime (svs->data), ==, G_PASTE_SPECIAL_MIME_TEXT_HTML);
    g_assert_cmpmem (g_bytes_get_data (g_paste_binary_data_get_bytes (svs->data), NULL), strlen (html_value),
                     html_value, strlen (html_value));

    g_assert_cmpint (g_paste_item_get_kind (read_password), ==, G_PASTE_ITEM_KIND_PASSWORD);
    g_assert_cmpstr (g_paste_item_get_real_value (read_password), ==, secret);
    g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM ((gpointer) read_password)), ==, pw_name);
    g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM ((gpointer) read_password)), ==, PASSWORD_TIMEOUT);

    g_assert_cmpstr (g_paste_item_get_value (read_color), ==, g_paste_item_get_value (g_list_nth_data (items, 2)));

    GBytes *read_png = g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (read_image));

    g_assert_nonnull (read_png);
    g_assert_true (g_bytes_equal (read_png, png));

    g_list_free_full (items, g_object_unref);
}

/* A wrong passphrase must be refused outright — reads come back empty, writes
 * are dropped, the real data survives — while an *empty* encrypted history is
 * silently re-keyed for the new passphrase instead of locking the user out. */
static void
test_encrypted_sqlite_wrong_passphrase (void)
{
    if (g_test_subprocess ())
    {
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "sqlite-encrypted-wrong";

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "right passphrase");
            GList *items = g_list_append (NULL, g_paste_text_item_new ("precious"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);
        }

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "wrong passphrase");
            g_autolist (GPasteItem) loaded = read_history (backend, name);
            g_assert_null (loaded);

            GList *items = g_list_append (NULL, g_paste_text_item_new ("usurper"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);
        }

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "right passphrase");
            g_autolist (GPasteItem) loaded = read_history (backend, name);
            g_assert_cmpuint (g_list_length (loaded), ==, 1);
            g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, "precious");
        }

        /* An empty encrypted history holds nothing to lose: a different
         * passphrase re-keys it instead of refusing it. */
        const gchar *empty_name = "sqlite-encrypted-empty";

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "original passphrase");
            g_autolist (GPasteItem) loaded = read_history (backend, empty_name);
            g_assert_null (loaded);
        }

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "replacement passphrase");
            g_autolist (GPasteItem) loaded = read_history (backend, empty_name);
            g_assert_null (loaded);

            GList *items = g_list_append (NULL, g_paste_text_item_new ("fresh start"));

            g_paste_storage_backend_write_history (backend, empty_name, items);
            g_list_free_full (items, g_object_unref);

            g_autolist (GPasteItem) refilled = read_history (backend, empty_name);
            g_assert_cmpuint (g_list_length (refilled), ==, 1);
        }

        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*does not unlock*");
}

/* The database flavor re-keys every content column it holds — the value, the
 * password name, the image blob and the special values — in one go, so the new
 * passphrase reads all of it back and the old one no longer opens the file. */
static void
test_encrypted_sqlite_rekey (void)
{
    if (g_test_subprocess ())
    {
        /* The refused read with the old passphrase warns; see the file flavor's
         * re-key test for why fatality is dropped here. */
        G_PASTE_TEST_IN_SUBPROCESS;

        const gchar *name = "sqlite-rekey";
        const gchar *other_name = "sqlite-rekey-backup";
        const gchar *other_value = "the second history";
        const gchar *old_passphrase = "the old passphrase";
        const gchar *new_passphrase = "the new passphrase";
        const gchar *secret = "s3cr3t-passw0rd";
        const gchar *pw_name = "my login";
        const gchar *html_value = "<b>hidden markup</b>";

        g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
        g_autoptr (GBytes) png = test_png_bytes_colored (11, 13, 17);
        g_autoptr (GBytes) html = g_bytes_new_static (html_value, strlen (html_value));
        g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);

        g_paste_settings_set_images_support (settings, TRUE);

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, old_passphrase);
            GPasteItem *text = g_paste_text_item_new ("plain text entry");
            GList *items = NULL;

            /* A special value exercises the special_values.data column. */
            g_paste_item_add_special_value (text, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                           g_bytes_ref (html)));

            items = g_list_append (items, text);
            items = g_list_append (items, g_paste_password_item_new (pw_name, secret, PASSWORD_TIMEOUT));
            items = g_list_append (items, g_paste_image_item_new_from_bytes (png, date, NULL));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);

            /* A second history (a backup, say): the daemon re-keys every one of
             * them through this same backend, so re-keying must leave it able to
             * open the ones it has not reached yet — still with the old
             * passphrase. */
            GList *other_items = g_list_append (NULL, g_paste_text_item_new (other_value));

            g_paste_storage_backend_write_history (backend, other_name, other_items);
            g_list_free_full (other_items, g_object_unref);

            g_assert_true (g_paste_storage_backend_rekey (backend, name, new_passphrase, NULL));
            g_assert_true (g_paste_storage_backend_rekey (backend, other_name, new_passphrase, NULL));
        }

        /* Both histories moved, not just the first. */
        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, new_passphrase);
            g_autolist (GPasteItem) loaded = read_history_ok (backend, other_name);
            g_assert_cmpuint (g_list_length (loaded), ==, 1);
            g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, other_value);
        }

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, new_passphrase);
            g_autolist (GPasteItem) loaded = read_history_ok (backend, name);
            g_assert_cmpuint (g_list_length (loaded), ==, 3);

            gboolean found_text = FALSE;
            gboolean found_password = FALSE;
            gboolean found_image = FALSE;

            for (const GList *l = loaded; l; l = l->next)
            {
                GPasteItem *item = l->data;
                GPasteItemKind kind = g_paste_item_get_kind (item);

                if (kind == G_PASTE_ITEM_KIND_PASSWORD)
                {
                    found_password = TRUE;
                    g_assert_cmpstr (g_paste_item_get_real_value (item), ==, secret);
                    g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (item)), ==, pw_name);
                    g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (item)), ==, PASSWORD_TIMEOUT);
                }
                else if (kind == G_PASTE_ITEM_KIND_IMAGE)
                {
                    found_image = TRUE;

                    GBytes *read_png = g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (item));

                    g_assert_nonnull (read_png);
                    g_assert_true (g_bytes_equal (read_png, png));
                }
                else
                {
                    const GSList *svs = g_paste_item_get_special_values (item);

                    found_text = TRUE;

                    g_assert_cmpuint (g_slist_length ((GSList *) svs), ==, 1);
                    g_assert_cmpint (g_paste_binary_data_get_mime (svs->data), ==, G_PASTE_SPECIAL_MIME_TEXT_HTML);
                    g_assert_true (g_bytes_equal (g_paste_binary_data_get_bytes (svs->data), html));
                }
            }

            g_assert_true (found_text);
            g_assert_true (found_password);
            g_assert_true (found_image);
        }

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, old_passphrase);
            assert_history_refused (backend, name);
        }

        return;
    }

    G_PASTE_TEST_TRAP_SUBPROCESS ("*does not unlock*");
}

/* The incremental save path works identically through the encrypted flavor,
 * driven by the factory + process-wide passphrase like the daemon does it. */
static void
test_encrypted_sqlite_incremental (void)
{
    const gchar *name = "sqlite-encrypted-incr";

    g_paste_storage_backend_set_passphrase ("incremental passphrase");

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_history_size (settings, 5);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_ENCRYPTED_SQLITE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, name);

    g_paste_history_add (history, g_paste_text_item_new ("a"));
    g_autofree gchar *uuid_a = g_strdup (g_paste_item_get_uuid (g_paste_history_get (history, 0)));
    g_paste_history_add (history, g_paste_text_item_new ("b"));
    g_paste_history_add (history, g_paste_text_item_new ("c"));
    g_assert_true (g_paste_history_select (history, uuid_a));
    g_paste_history_remove (history, 1); /* [a, c, b] -> drop c */

    const gchar * const after_ops[] = { "a", "b", NULL };
    g_assert_true (sqlite_wait_for_values (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, name, after_ops, 5000));

    g_assert_true (g_paste_history_empty (history, NULL));

    const gchar * const after_empty[] = { NULL };
    g_assert_true (sqlite_wait_for_values (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, name, after_empty, 5000));

    g_paste_storage_backend_set_passphrase (NULL);
}
#endif

/* A history name goes straight into the file the history is stored in and into
 * the images directory it owns, so one carrying a path component names
 * something outside the history directory: the file is written wherever the
 * traversal lands, and deleting that history sweeps whatever is there. Names
 * arrive from the bus, so this has to be a refusal. */
static void
test_history_name_refuses_a_path (void)
{
    g_assert_true (g_paste_util_history_name_is_valid ("history"));
    g_assert_true (g_paste_util_history_name_is_valid ("a name with spaces"));
    g_assert_true (g_paste_util_history_name_is_valid ("..dots-but-not-a-traversal"));

    g_assert_false (g_paste_util_history_name_is_valid ("../escape"));
    g_assert_false (g_paste_util_history_name_is_valid ("sub/history"));
    g_assert_false (g_paste_util_history_name_is_valid ("/absolute"));
    g_assert_false (g_paste_util_history_name_is_valid ("trailing/"));
    g_assert_false (g_paste_util_history_name_is_valid ("."));
    g_assert_false (g_paste_util_history_name_is_valid (".."));
    g_assert_false (g_paste_util_history_name_is_valid (""));
    g_assert_false (g_paste_util_history_name_is_valid (NULL));

    /* As long as a history can be saved under it, "<name>.xml.tmp" being
     * written: one a save landed for is never turned away. */
    g_autofree gchar *longest = g_strnfill (247, 'n');
    g_autofree gchar *too_long = g_strnfill (248, 'n');

    g_assert_true (g_paste_util_history_name_is_valid (longest));
    g_assert_false (g_paste_util_history_name_is_valid (too_long));

    /* Refused off the bus for what it is: no path, but too long a name. */
    {
        const GPasteDaemonMethods methods = { 0 };
        g_autoptr (GError) error = NULL;

        g_paste_daemon_methods_switch_history (&methods, too_long, &error);
        g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_INVALID_ARGUMENT);
        g_assert_nonnull (g_strstr_len (error->message, -1, "too long"));
    }

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    g_autolist (GPasteItem) items = g_list_append (NULL, g_paste_text_item_new ("saved under the longest name"));

    g_assert_true (g_paste_storage_name_fits (G_PASTE_STORAGE_FILE, longest));
    g_assert_true (g_paste_storage_name_fits (G_PASTE_STORAGE_NOOP, longest));
    g_assert_true (g_paste_storage_backend_write_history (backend, longest, items));

    g_autolist (GPasteItem) loaded = read_history_ok (backend, longest);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);

    /* The other flavours create a history only under a name they can keep their
     * longest file under: the "<name>.xmls.rekey-old" an encrypted file
     * history's re-key keeps the original aside as, the "<name>.db-journal" or
     * "<name>.dbs-journal" SQLite opens beside a new database. Asked of the
     * flavour, whether or not this build can open it -- a plain SQLite one it
     * cannot being kept as a plain file. */
    static const struct
    {
        GPasteStorage storage;
        gsize         longest;
    } bounds[] = {
        { G_PASTE_STORAGE_ENCRYPTED_FILE,   240 },
#ifdef G_PASTE_ENABLE_SQLITE
        { G_PASTE_STORAGE_SQLITE,           244 },
#else
        { G_PASTE_STORAGE_SQLITE,           247 },
#endif
        { G_PASTE_STORAGE_ENCRYPTED_SQLITE, 243 },
    };

    for (gsize i = 0; i < G_N_ELEMENTS (bounds); ++i)
    {
        g_autofree gchar *fits = g_strnfill (bounds[i].longest, 'n');
        g_autofree gchar *past = g_strnfill (bounds[i].longest + 1, 'n');

        g_assert_true (g_paste_storage_name_fits (bounds[i].storage, fits));
        /* One past the longest name a history may have is no name at all. */
        if (bounds[i].longest < strlen (longest))
            g_assert_false (g_paste_storage_name_fits (bounds[i].storage, past));
    }

#ifdef G_PASTE_ENABLE_SQLITE
    /* The longest name that fits is one a new database does open under. */
    g_autoptr (GPasteStorageBackend) sqlite = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autofree gchar *longest_database = g_strnfill (244, 's');

    g_assert_true (g_paste_storage_backend_write_history (sqlite, longest_database, items));

    g_autolist (GPasteItem) read_back = read_history_ok (sqlite, longest_database);

    g_assert_cmpuint (g_list_length (read_back), ==, 1);
#endif
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* A password name is whatever MakePassword was handed, and it is written as an
 * XML attribute value. A quote in one closed the attribute early: the entries
 * that followed were forged out of the rest of the name, or the document simply
 * stopped parsing and the whole history read back as unreadable. Only the
 * encrypted flavour persists passwords, so that is where this is pinned. */
static void
test_encrypted_password_name_is_escaped (void)
{
    const gchar *name = "encrypted-password-name";
    const gchar *secret = "s3cr3t-passw0rd";
    /* Every character that means something in an attribute, the whitespace a
     * read normalizes in one, and the CDATA terminator the value escaping is
     * there for. */
    const gchar *pw_name = "a \" quote & <tag> ' and ]]> too,\ta\nb\rc";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteStorageBackend) backend = g_paste_file_backend_new_encrypted (settings, "the master passphrase");

    GList *items = NULL;

    items = g_list_append (items, g_paste_password_item_new (pw_name, secret, PASSWORD_TIMEOUT));
    items = g_list_append (items, g_paste_text_item_new ("the entry after it"));

    g_paste_storage_backend_write_history (backend, name, items);
    g_list_free_full (items, g_object_unref);

    /* Both entries come back -- the document parsed -- and the name is the one
     * that went in, not a truncated or re-escaped version of it. */
    g_autolist (GPasteItem) loaded = read_history_ok (backend, name);

    g_assert_cmpuint (g_list_length (loaded), ==, 2);

    GPasteItem *password = loaded->data;

    g_assert_cmpint (g_paste_item_get_kind (password), ==, G_PASTE_ITEM_KIND_PASSWORD);
    g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (password)), ==, pw_name);
    g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (password)), ==, PASSWORD_TIMEOUT);
    g_assert_cmpstr (g_paste_item_get_real_value (password), ==, secret);
    g_assert_cmpstr (g_paste_item_get_real_value (loaded->next->data), ==, "the entry after it");
}
#endif

/* The history directory holds the clipboard -- passwords under the encrypted
 * flavour, and the screenshots the plain one writes unencrypted -- so it is the
 * owner's alone, and so is the images directory under it. */
static void
test_history_dir_is_private (void)
{
    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    GStatBuf st;

    /* As an installation predating the private mode left it: found, not
     * created, it has to be restricted all the same. */
    g_assert_cmpint (g_mkdir_with_parents (dir, 0755), ==, 0);
    g_assert_cmpint (g_chmod (dir, 0755), ==, 0);
    g_assert_true (g_paste_util_ensure_history_dir_exists ());
    g_assert_cmpint (g_stat (dir, &st), ==, 0);
    g_assert_cmpuint (st.st_mode & 0777, ==, 0700);

    /* The images directory is created on demand, by writing a history that has
     * an image in it. */
    const gchar *name = "private-dir";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GBytes) png = test_png_bytes_colored (1, 2, 3);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    GList *items = g_list_append (NULL, g_paste_image_item_new_from_bytes (png, date, NULL));

    g_paste_storage_backend_write_history (backend, name, items);
    g_list_free_full (items, g_object_unref);

    g_autofree gchar *images_dir = g_paste_file_backend_images_dir (name);

    g_assert_cmpint (g_stat (images_dir, &st), ==, 0);
    g_assert_cmpuint (st.st_mode & 0777, ==, 0700);
}

/* Every case starts from the schema's defaults: the memory backend is shared by
 * the whole binary, so a key one case sets (the storage backend, most of all)
 * would otherwise reach every case after it, and a case run alone with -p would
 * see different settings. */
static void
run_from_defaults (gconstpointer test)
{
    g_autoptr (GSettings) raw = g_settings_new (G_PASTE_SETTINGS_NAME);
    g_autoptr (GSettingsSchema) schema = NULL;

    g_object_get (raw, "settings-schema", &schema, NULL);

    g_auto (GStrv) keys = g_settings_schema_list_keys (schema);

    for (GStrv key = keys; *key; ++key)
        g_settings_reset (raw, *key);

    ((GTestFunc) test) ();
}

static void
add_test (const gchar *path,
          GTestFunc    test)
{
    g_test_add_data_func (path, (gconstpointer) test, run_from_defaults);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    add_test ("/history/image_capture_does_not_write", test_image_capture_does_not_write);
    add_test ("/history/file_image_materialization", test_file_image_materialization);
    add_test ("/history/history_image_names_no_file", test_history_image_names_no_file);
    add_test ("/history/file_image_per_history", test_file_image_per_history);
    add_test ("/history/file_delete_takes_temporary", test_file_delete_takes_temporary);
    add_test ("/history/empty_other_keeps_stored_images", test_empty_other_keeps_stored_images);
    add_test ("/history/noop_refuses_unmade", test_noop_refuses_unmade);
    add_test ("/history/file_backup_owns_images", test_file_backup_owns_images);
    add_test ("/history/file_eviction_deletes_image", test_file_eviction_deletes_image);
    add_test ("/history/file_flushed_eviction_keeps_image", test_file_flushed_eviction_keeps_image);
    add_test ("/history/file_image_moved_home", test_file_image_moved_home);
    add_test ("/history/file_unreadable_keeps_image", test_file_unreadable_keeps_image);
    add_test ("/history/file_skipped_image_kept_by_reads", test_file_skipped_image_kept_by_reads);
    add_test ("/history/file_skipped_image_swept_by_save", test_file_skipped_image_swept_by_save);
    add_test ("/history/file_sweep_bounds", test_file_sweep_bounds);
    add_test ("/history/file_legacy_images_sweep", test_file_legacy_images_sweep);
    add_test ("/history/file_parser_malformed", test_file_parser_malformed);
    add_test ("/history/file_count_matches_read", test_file_count_matches_read);
    add_test ("/history/switch_refused", test_switch_refused);
    add_test ("/history/switch_unstored", test_switch_unstored);
    add_test ("/history/delete_current_drops_passwords", test_delete_current_drops_passwords);
    add_test ("/history/delete_current_failed_stays", test_delete_current_failed_stays);
    add_test ("/history/backup_missing_source", test_backup_missing_source);
    add_test ("/history/delete_missing_history", test_delete_missing_history);
#ifdef G_PASTE_ENABLE_SQLITE
    add_test ("/history/migration_detects_backend_in_use", test_migration_detects_backend_in_use);
#endif
    add_test ("/history/migration_none_is_current", test_migration_none_is_current);
    add_test ("/history/migration_invalid_history_name", test_migration_invalid_history_name);
    add_test ("/history/file_value_round_trip", test_file_value_round_trip);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    add_test ("/history/file_encrypting_migration_images", test_file_encrypting_migration_images);
    add_test ("/history/file_encrypted_fallback_verified", test_file_encrypted_fallback_verified);
    add_test ("/history/file_encrypted_side_file_verified", test_file_encrypted_side_file_verified);
    add_test ("/history/file_encrypted_image_round_trip", test_file_encrypted_image_round_trip);
    add_test ("/history/file_encrypting_migration_lost_side_file", test_file_encrypting_migration_lost_side_file);
#endif
    add_test ("/history/content_kind_transitions", test_content_kind_transitions);
    add_test ("/history/same_display_string_keeps_size", test_same_display_string_keeps_size);
    add_test ("/history/add_get_length", test_add_get_length);
    add_test ("/history/dedup_moves_to_front", test_dedup_moves_to_front);
    add_test ("/history/add_equal_first_is_noop", test_add_equal_first_is_noop);
    add_test ("/history/size_enforcement", test_size_enforcement);
    add_test ("/history/remove", test_remove);
    add_test ("/history/remove_by_uuid", test_remove_by_uuid);
    add_test ("/history/get_by_uuid", test_get_by_uuid);
    add_test ("/history/uuid_lookup_survives_reshuffling", test_uuid_lookup_survives_reshuffling);
    add_test ("/history/memory_eviction_evicts_repeatedly", test_memory_eviction_evicts_repeatedly);
    add_test ("/history/select_counts_image_once", test_select_counts_image_once);
    add_test ("/history/search_image_by_what_it_shows", test_search_image_by_what_it_shows);
    add_test ("/history/search_queries", test_search_queries);
    add_test ("/history/merge_refuses_image_and_password", test_merge_refuses_image_and_password);
    add_test ("/history/memory_cap_after_head_removal", test_memory_cap_after_head_removal);
    add_test ("/history/memory_cap_after_replacement", test_memory_cap_after_replacement);
    add_test ("/history/replace_refused_over_cap", test_replace_refused_over_cap);
    add_test ("/history/memory_cap_after_replacement_spares_once", test_memory_cap_after_replacement_spares_once);
    add_test ("/history/select_favourite_over_cap", test_select_favourite_over_cap);
    add_test ("/history/select_image_over_cap", test_select_image_over_cap);
    add_test ("/history/favourite_survives_size_cap", test_favourite_survives_size_cap);
    add_test ("/history/unfavourite_makes_evictable", test_unfavourite_makes_evictable);
    add_test ("/history/all_favourites_grow_past_cap", test_all_favourites_grow_past_cap);
    add_test ("/history/favourite_survives_memory_cap", test_favourite_survives_memory_cap);
    add_test ("/history/favourite_roundtrip", test_favourite_roundtrip);
    add_test ("/history/favourite_survives_replace", test_favourite_survives_replace);
    add_test ("/history/add_password_name_conflict", test_add_password_name_conflict);
    add_test ("/history/password_names_wait_for_load", test_password_names_wait_for_load);
    add_test ("/history/add_file_refusals", test_add_file_refusals);
    add_test ("/history/add_file_tiff", test_add_file_tiff);
    add_test ("/history/set_active_refused", test_set_active_refused);
    add_test ("/history/set_active_unstored", test_set_active_unstored);
    add_test ("/history/migration_refused_backend", test_migration_refused_backend);
    add_test ("/history/migration_unstored_backend", test_migration_unstored_backend);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    add_test ("/history/migration_refused_keeps_passphrase", test_migration_refused_keeps_passphrase);
#endif
#ifdef G_PASTE_ENABLE_SQLITE
    add_test ("/history/migration_locked_backend_is_current", test_migration_locked_backend_is_current);
    add_test ("/history/migration_unstored_backend_is_current", test_migration_unstored_backend_is_current);
#endif
    add_test ("/history/make_password_keeps_uuid", test_make_password_keeps_uuid);
    add_test ("/history/make_password_heavy_name", test_make_password_heavy_name);
    add_test ("/history/nameless_passwords_may_coexist", test_nameless_passwords_may_coexist);
    add_test ("/history/nameless_passwords_with_same_value_dedup", test_nameless_passwords_with_same_value_dedup);
    add_test ("/history/nameless_password_dedup_past_the_head", test_nameless_password_dedup_past_the_head);
    add_test ("/history/select_moves_a_password_the_head_matches", test_select_moves_a_password_the_head_matches);
    add_test ("/history/named_passwords_never_match", test_named_passwords_never_match);
    add_test ("/history/rename_keeps_history_size", test_rename_keeps_history_size);
    add_test ("/history/strip_rich_text", test_strip_rich_text);
    add_test ("/history/strip_rich_text_persists", test_strip_rich_text_persists);
    add_test ("/history/edit_and_make_password_drop_rich_text", test_edit_and_make_password_drop_rich_text);
    add_test ("/history/favourite_survives_growing_line", test_favourite_survives_growing_line);
    add_test ("/history/favourite_survives_dedup", test_favourite_survives_dedup);
    add_test ("/history/uris_item_answers_its_uris", test_uris_item_answers_its_uris);
    add_test ("/history/load_applies_the_caps", test_load_applies_the_caps);
    add_test ("/history/load_leaves_a_flushed_history_alone", test_load_leaves_a_flushed_history_alone);
    add_test ("/history/select_moves_to_front", test_select_moves_to_front);
    add_test ("/history/empty", test_empty);
    add_test ("/history/save_load_roundtrip", test_save_load_roundtrip);
    add_test ("/history/unnamed_history_persists_nothing", test_unnamed_history_persists_nothing);
    add_test ("/history/flush_persists_synchronously", test_flush_persists_synchronously);
    add_test ("/history/flush_stops_recording", test_flush_stops_recording);
    add_test ("/history/delete_refused_after_flush", test_delete_refused_after_flush);
    add_test ("/history/file_v1_refused_and_preserved", test_file_v1_refused_and_preserved);
    add_test ("/history/file_version_guard", test_file_version_guard);
    add_test ("/history/password_dropped", test_password_dropped);
    add_test ("/history/reload_backend_announced", test_reload_backend_announced);
    add_test ("/history/reload_then_empty_drops_password", test_reload_then_empty_drops_password);
    g_test_add_data_func ("/history/reload_then_delete_drops_password", GINT_TO_POINTER (FALSE), test_reload_then_delete_drops_password);
    g_test_add_data_func ("/history/reload_twice_drops_password", GINT_TO_POINTER (TRUE), test_reload_then_delete_drops_password);
    add_test ("/history/load_selects_last_add", test_load_selects_last_add);
    add_test ("/history/load_keeps_add_uuid", test_load_keeps_add_uuid);
    add_test ("/history/load_keeps_password_selected", test_load_keeps_password_selected);
    add_test ("/history/switch_to_current_keeps_model", test_switch_to_current_keeps_model);
#ifdef G_PASTE_ENABLE_SQLITE
    add_test ("/history/load_add_replaces_stored_head", test_load_add_replaces_stored_head);
#endif
    add_test ("/history/reload_keeps_stopped_adds", test_reload_keeps_stopped_adds);
    add_test ("/history/reload_carries_load_adds_once", test_reload_carries_load_adds_once);
    add_test ("/history/reload_keeps_retired_order", test_reload_keeps_retired_order);
    add_test ("/history/stopped_add_evicted_freed", test_stopped_add_evicted_freed);
    add_test ("/history/reload_during_load_keeps_adds", test_reload_during_load_keeps_adds);
    add_test ("/history/name_refuses_a_path", test_history_name_refuses_a_path);
    add_test ("/history/dir_is_private", test_history_dir_is_private);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    add_test ("/history/encrypted_roundtrip", test_encrypted_roundtrip);
    add_test ("/history/encrypted_empty_placeholder", test_encrypted_empty_placeholder);
    add_test ("/history/load_skips_password_head", test_load_skips_password_head);
    add_test ("/history/encrypted_explicit_passphrase", test_encrypted_explicit_passphrase);
    add_test ("/history/encrypted_rekey", test_encrypted_rekey);
    add_test ("/history/encrypted_split_keys_refuse_passphrase", test_encrypted_split_keys_refuse_passphrase);
    add_test ("/history/encrypted_password_name_is_escaped", test_encrypted_password_name_is_escaped);
#endif
#ifdef G_PASTE_ENABLE_SQLITE
    add_test ("/history/sqlite_roundtrip", test_sqlite_roundtrip);
    add_test ("/history/sqlite_incremental", test_sqlite_incremental);
    add_test ("/history/sqlite_replace", test_sqlite_replace);
    add_test ("/history/sqlite_strip_rich_text", test_sqlite_strip_rich_text);
    add_test ("/history/sqlite_cascade", test_sqlite_cascade);
    add_test ("/history/sqlite_migration_keeps_destination_images", test_sqlite_migration_keeps_destination_images);
    add_test ("/history/sqlite_migration_takes_source_images", test_sqlite_migration_takes_source_images);
    add_test ("/history/sqlite_deleted_text_gone", test_sqlite_deleted_text_gone);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    add_test ("/history/sqlite_encrypted_deleted_checksum_gone", test_sqlite_encrypted_deleted_checksum_gone);
#endif
    add_test ("/history/sqlite_image_blob", test_sqlite_image_blob);
    add_test ("/history/sqlite_image_date_out_of_range", test_sqlite_image_date_out_of_range);
    add_test ("/history/sqlite_eviction_deletes_no_image", test_sqlite_eviction_deletes_no_image);
    add_test ("/history/sqlite_no_rewrite_on_switch", test_sqlite_no_rewrite_on_switch);
    add_test ("/history/sqlite_save_elsewhere_keeps_connection", test_sqlite_save_elsewhere_keeps_connection);
    add_test ("/history/sqlite_replace_evictions", test_sqlite_replace_evictions);
    add_test ("/history/sqlite_version_guard", test_sqlite_version_guard);
    add_test ("/history/sqlite_empty_unopenable", test_sqlite_empty_unopenable);
    add_test ("/history/sqlite_schema_migration", test_sqlite_schema_migration);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    add_test ("/history/encrypted_sqlite_absurd_kdf_params", test_encrypted_sqlite_absurd_kdf_params);
    add_test ("/history/encrypted_sqlite_absurd_kdf_params_empty", test_encrypted_sqlite_absurd_kdf_params_empty);
    add_test ("/history/encrypted_sqlite_roundtrip", test_encrypted_sqlite_roundtrip);
    add_test ("/history/encrypted_sqlite_wrong_passphrase", test_encrypted_sqlite_wrong_passphrase);
    add_test ("/history/encrypted_sqlite_rekey", test_encrypted_sqlite_rekey);
    add_test ("/history/encrypted_sqlite_incremental", test_encrypted_sqlite_incremental);
#endif
#endif

    return g_paste_test_env_run ();
}
