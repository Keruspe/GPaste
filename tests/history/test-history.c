// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <string.h>

#ifdef G_PASTE_ENABLE_ENCRYPTION
#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <string.h>
#endif

#ifdef G_PASTE_ENABLE_SQLITE
#include <gpaste/gpaste-util.h>

#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-sqlite-backend.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <sqlite3.h>
#include <string.h>
#endif

/* Build a fresh, empty history backed by an in-memory GSettings.
 * Growing-lines merging is disabled so distinct strings stay distinct. */
static GPasteHistory *
make_history (GPasteSettings **out_settings,
              guint64          max_history_size)
{
    /* A distinct history name per call keeps each test's on-disk file (under our
     * throwaway XDG_DATA_HOME) separate, so the model's async persistence can't
     * leak state from one test into the next. */
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

/* Like make_history but without forcing a name into GSettings (which would emit
 * a deferred "changed" and reload), so the round-trip test can drive the name
 * itself through load()/load_async(). */
static GPasteHistory *
make_plain_history (void)
{
    GPasteSettings *settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_max_history_size (settings, 100);
    g_paste_settings_set_max_memory_usage (settings, 1024 /* MiB */);

    GPasteHistory *history = g_paste_history_new (settings);

    g_object_unref (settings);

    return history;
}

static const gchar *
value_at (GPasteHistory *history,
          guint64        index)
{
    const GPasteItem *item = g_paste_history_get (history, index);
    return item ? g_paste_item_get_value (item) : NULL;
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
        while (g_main_context_iteration (NULL, FALSE))
            ;
        g_usleep (1000);
    }
    return g_paste_history_get_length (history) == expected_len;
}

/* A real (1x1 transparent) PNG for the image item tests. */
static GBytes *
test_png_bytes (void)
{
    gsize length = 0;
    guchar *data = g_base64_decode ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==",
                                    &length);

    return g_bytes_new_take (data, length);
}

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

/* Capturing an image no longer touches the disk: the item carries its encoded
 * PNG and *materializing* it is the storage backend's job. */
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
 * bytes when writing its XML, and reads it back by path. */
static void
test_file_image_materialization (void)
{
    const gchar *name = "file-image";

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_images_support (settings, TRUE);

    g_autoptr (GBytes) png = test_png_bytes_colored (4, 5, 6);
    g_autoptr (GDateTime) date = g_date_time_new_from_unix_local (1234567890);
    GList *items = g_list_append (NULL, g_paste_image_item_new_from_bytes (name, png, date, NULL));

    g_assert_nonnull (items->data);

    const gchar *cache_path = g_paste_item_get_value (items->data);

    g_assert_false (g_file_test (cache_path, G_FILE_TEST_EXISTS));

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

    g_paste_storage_backend_write_history (backend, name, items);

    /* The write materialized the referenced file, byte-identical to the item. */
    g_autofree gchar *materialized = NULL;
    gsize materialized_len = 0;

    g_assert_true (g_file_get_contents (cache_path, &materialized, &materialized_len, NULL));
    g_assert_cmpmem (materialized, materialized_len,
                     g_bytes_get_data (png, NULL), g_bytes_get_size (png));

    GList *loaded = NULL;
    gsize size = 0;

    g_paste_storage_backend_read_history (backend, name, &loaded, &size);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpstr (g_paste_item_get_kind (loaded->data), ==, "Image");
    g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, cache_path);

    g_list_free_full (loaded, g_object_unref);
    g_list_free_full (items, g_object_unref);
}

/* The history re-anchors a captured image under its own images directory
 * before it is persisted. */
static void
test_history_anchors_image (void)
{
    g_autoptr (GPasteHistory) history = make_plain_history ();

    g_paste_history_load (history, "anchor-test");

    g_autoptr (GBytes) png = test_png_bytes_colored (13, 14, 15);
    g_autoptr (GError) error = NULL;
    g_autoptr (GdkTexture) texture = gdk_texture_new_from_bytes (png, &error);

    g_assert_nonnull (texture);

    g_paste_history_add (history, g_paste_image_item_new (texture));

    const GPasteItem *item = g_paste_history_get (history, 0);
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *expected_dir = g_build_filename (history_dir, "images", "anchor-test", NULL);
    g_autofree gchar *dir = g_path_get_dirname (g_paste_item_get_value (item));

    g_assert_cmpstr (dir, ==, expected_dir);
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
    GList *items_a = g_list_append (NULL, g_paste_image_item_new_from_bytes ("per-history-a", png, date, NULL));
    GList *items_b = g_list_append (NULL, g_paste_image_item_new_from_bytes ("per-history-b", png, date, NULL));

    g_assert_nonnull (items_a->data);
    g_assert_nonnull (items_b->data);

    const gchar *path_a = g_paste_item_get_value (items_a->data);
    const gchar *path_b = g_paste_item_get_value (items_b->data);

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

    GList *loaded = NULL;
    gsize size = 0;

    g_paste_storage_backend_read_history (backend, "per-history-b", &loaded, &size);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpstr (g_paste_item_get_kind (loaded->data), ==, "Image");

    g_list_free_full (loaded, g_object_unref);
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
    GList *items = g_list_append (NULL, g_paste_image_item_new_from_bytes ("backup-src", png, date, NULL));

    g_assert_nonnull (items->data);

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

    g_paste_storage_backend_write_history (backend, "backup-src", items);
    /* What g_paste_history_save does for BackupHistory: same items, new name. */
    g_paste_storage_backend_write_history (backend, "backup-dst", items);

    /* The backup got its own copy; the item still belongs to the source. */
    const gchar *src_path = g_paste_item_get_value (items->data);
    g_autofree gchar *dst_path = g_paste_image_item_get_path_for_history (_G_PASTE_IMAGE_ITEM (items->data), "backup-dst");

    g_assert_true (g_strstr_len (src_path, -1, "backup-src") != NULL);
    g_assert_true (g_file_test (src_path, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (dst_path, G_FILE_TEST_EXISTS));

    /* Deleting the source history leaves the backup fully readable. */
    g_paste_storage_backend_delete_history (backend, "backup-src", NULL);

    g_assert_false (g_file_test (src_path, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (dst_path, G_FILE_TEST_EXISTS));

    GList *loaded = NULL;
    gsize size = 0;

    g_paste_storage_backend_read_history (backend, "backup-dst", &loaded, &size);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_assert_cmpstr (g_paste_item_get_kind (loaded->data), ==, "Image");
    g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, dst_path);

    g_list_free_full (loaded, g_object_unref);
    g_list_free_full (items, g_object_unref);
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

    const GPasteItem *item = g_paste_history_get_by_uuid (history, uuid);
    g_assert_nonnull (item);
    g_assert_cmpstr (g_paste_item_get_value (item), ==, "hello");

    g_assert_null (g_paste_history_get_by_uuid (history, "nope"));
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

    g_paste_history_empty (history);
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

        for (guint i = 0; i < 100; ++i)
        {
            while (g_main_context_iteration (NULL, FALSE))
                ;
            g_usleep (1000);
        }
    }

    {
        g_autoptr (GPasteHistory) reader = make_plain_history ();
        g_paste_history_load_async (reader, name);

        g_assert_true (pump_until_length (reader, 1, 5000));
        g_assert_cmpstr (value_at (reader, 0), ==, "kept");
    }
}

/* A delete must not race whoever owns the store now: once the history has been
 * flushed for a handover (to a successor daemon, or to the gpaste-storage helper
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

/* A history written by a newer GPaste (an unknown <history version=>) is a read
 * *failure*, not an empty one: every item is skipped, so passing it off as empty
 * would have the very next clipboard change overwrite it. Once loaded it must
 * therefore stop persistence — for itself alone, and only until it is gone. */
static void
test_file_version_guard (void)
{
    if (g_test_subprocess ())
    {
        /* The refusals below warn; see test_sqlite_version_guard. */
        g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

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
            GList *loaded = NULL;
            gsize size = 0;

            g_assert_false (g_paste_storage_backend_read_history (backend, name, &loaded, &size));
            g_assert_null (loaded);
        }

        g_autoptr (GPasteHistory) history = make_plain_history ();

        g_paste_history_load (history, name);
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);

        /* Recording is off for it: pump the loop so an (erroneous) async write
         * would run, then check the file is byte-for-byte what we wrote. */
        g_paste_history_add (history, g_paste_text_item_new ("usurper"));

        for (guint i = 0; i < 100; ++i)
        {
            while (g_main_context_iteration (NULL, FALSE))
                ;
            g_usleep (1000);
        }

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

    g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed ();
    g_test_trap_assert_stderr ("*Unknown history version: 9.0*");
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
    items = g_list_append (items, g_paste_password_item_new (pw_name, secret));
    items = g_list_append (items, g_paste_image_item_new_from_bytes (name, png, date, NULL));

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
    const gchar *cache_path = g_paste_item_get_value (g_list_nth_data (items, 2));
    g_autofree gchar *encrypted_image_path = g_strconcat (cache_path, "s", NULL);

    g_assert_false (g_file_test (cache_path, G_FILE_TEST_EXISTS));
    g_assert_true (g_file_test (encrypted_image_path, G_FILE_TEST_EXISTS));
    g_assert_false (file_contains (encrypted_image_path, "IHDR"));

    /* Read back through the same backend. */
    GList *loaded = NULL;
    gsize size = 0;
    g_paste_storage_backend_read_history (backend, name, &loaded, &size);
    g_assert_cmpuint (g_list_length (loaded), ==, 3);

    gboolean found_text = FALSE;
    gboolean found_password = FALSE;
    gboolean found_image = FALSE;
    for (const GList *l = loaded; l; l = l->next)
    {
        GPasteItem *item = l->data;
        const gchar *kind = g_paste_item_get_kind (item);

        if (g_strcmp0 (kind, "Password") == 0)
        {
            found_password = TRUE;
            g_assert_cmpstr (g_paste_item_get_real_value (item), ==, secret);
            g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (item)), ==, pw_name);
        }
        else if (g_strcmp0 (kind, "Image") == 0)
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

    g_list_free_full (loaded, g_object_unref);
    g_list_free_full (items, g_object_unref);
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
        /* The refusals below warn; g_test_expect_message cannot swallow them
         * under structured logging, so drop warning fatality in this subprocess
         * and let the parent match them on stderr. */
        g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

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
            GList *loaded = NULL;
            gsize size = 0;

            g_assert_true (g_paste_storage_backend_read_history (backend, name, &loaded, &size));
            g_assert_cmpuint (g_list_length (loaded), ==, 1);
            g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, value);

            g_list_free_full (loaded, g_object_unref);
        }

        /* ...while the process-wide one does not, and is refused rather than
         * silently yielding an empty history. */
        {
            g_autoptr (GPasteStorageBackend) backend =
                g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings,
                                                             g_paste_storage_backend_get_passphrase ());
            GList *loaded = NULL;
            gsize size = 0;

            g_assert_false (g_paste_storage_backend_read_history (backend, name, &loaded, &size));
            g_assert_null (loaded);
        }

        /* No passphrase at all degrades to "no storage" instead of falling back
         * to the process-wide one (the destination's key here). */
        {
            g_autoptr (GPasteStorageBackend) backend =
                g_paste_storage_backend_new_with_passphrase (G_PASTE_STORAGE_ENCRYPTED_FILE, settings, NULL);
            GList *loaded = NULL;
            gsize size = 0;

            g_paste_storage_backend_read_history (backend, name, &loaded, &size);
            g_assert_null (loaded);
        }

        /* The plain constructor, by contrast, does fall back to the process-wide
         * passphrase — which is the destination's, hence unable to read this. */
        {
            g_autoptr (GPasteStorageBackend) backend =
                g_paste_storage_backend_new (G_PASTE_STORAGE_ENCRYPTED_FILE, settings);
            GList *loaded = NULL;
            gsize size = 0;

            g_assert_false (g_paste_storage_backend_read_history (backend, name, &loaded, &size));
            g_assert_null (loaded);
        }

        g_paste_storage_backend_set_passphrase (NULL);

        return;
    }

    g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed ();
    g_test_trap_assert_stderr ("*No passphrase for the encrypted storage backend*");
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
        while (g_main_context_iteration (NULL, FALSE))
            ;

        GList *loaded = NULL;
        gsize size = 0;

        g_paste_storage_backend_read_history (backend, name, &loaded, &size);

        gboolean match = (g_list_length (loaded) == expected);
        const GList *l = loaded;

        for (guint v = 0; match && v < expected; ++v, l = l->next)
            match = g_paste_str_equal (g_paste_item_get_value (l->data), values[v]);

        g_list_free_full (loaded, g_object_unref);

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
    g_paste_item_add_special_value (text, g_paste_binary_data_new (G_PASTE_SPECIAL_ATOM_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>hi</b>", 9)));
    g_paste_item_add_special_value (text, g_paste_binary_data_new (G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES,
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
    items = g_list_append (items, g_paste_password_item_new ("my login", "s3cr3t"));

    g_paste_storage_backend_write_history (backend, name, items);

    /* The database exists and the history is listed. */
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "db");
    g_assert_true (g_file_test (path, G_FILE_TEST_EXISTS));

    g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, NULL);
    g_assert_true (g_strv_contains ((const gchar * const *) names, name));

    GList *loaded = NULL;
    gsize size = 0;

    g_paste_storage_backend_read_history (backend, name, &loaded, &size);

    /* The password entry is not persisted; everything else is, in order. */
    g_assert_cmpuint (g_list_length (loaded), ==, 4);

    const GList *l = loaded;
    const GList *o = items;

    for (guint i = 0; i < 4; ++i, l = l->next, o = o->next)
    {
        const GPasteItem *read = l->data;
        const GPasteItem *orig = o->data;

        g_assert_cmpstr (g_paste_item_get_kind (read), ==, g_paste_item_get_kind (orig));
        g_assert_cmpstr (g_paste_item_get_uuid (read), ==, g_paste_item_get_uuid (orig));
        /* An image comes back from its stored blob, re-anchored at its
         * canonical cache path — its bytes are compared below instead. */
        if (!g_paste_str_equal (g_paste_item_get_kind (read), "Image"))
            g_assert_cmpstr (g_paste_item_get_real_value (read), ==, g_paste_item_get_real_value (orig));
    }

    /* Special values survive with their mimes, bytes and order. */
    const GSList *read_svs = g_paste_item_get_special_values (loaded->data);
    const GSList *orig_svs = g_paste_item_get_special_values (items->data);

    g_assert_cmpuint (g_slist_length ((GSList *) read_svs), ==, 2);

    for (; read_svs && orig_svs; read_svs = read_svs->next, orig_svs = orig_svs->next)
    {
        const GPasteBinaryData *read_sv = read_svs->data;
        const GPasteBinaryData *orig_sv = orig_svs->data;

        g_assert_cmpint (g_paste_binary_data_get_mime (read_sv), ==, g_paste_binary_data_get_mime (orig_sv));
        g_assert_true (g_bytes_equal (g_paste_binary_data_get_bytes (read_sv), g_paste_binary_data_get_bytes (orig_sv)));
    }

    /* Image metadata survives, and the image itself comes back as the blob the
     * backend stored — byte-identical to the source PNG, so the item no longer
     * depends on any file on disk. */
    const GPasteImageItem *image = _G_PASTE_IMAGE_ITEM (g_list_nth_data (loaded, 3));

    g_assert_cmpint (g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (image)), ==, 1234567890);
    g_assert_cmpstr (g_paste_image_item_get_checksum (image), ==,
                     "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

    GBytes *read_png = g_paste_image_item_get_png_bytes (image);

    g_assert_nonnull (read_png);
    g_assert_true (g_bytes_equal (read_png, orig_png));

    g_list_free_full (loaded, g_object_unref);
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
    g_paste_history_empty (history);
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
    g_paste_item_add_special_value (middle, g_paste_binary_data_new (G_PASTE_SPECIAL_ATOM_TEXT_HTML,
                                                                     g_bytes_new_static ("<i>old</i>", 10)));

    GList *items = NULL;
    items = g_list_append (items, g_paste_text_item_new ("front"));
    items = g_list_append (items, middle);
    items = g_list_append (items, g_paste_text_item_new ("back"));

    g_paste_storage_backend_write_history (backend, name, items);

    /* Replace the middle item: new uuid, new value, different special values. */
    g_autoptr (GPasteItem) replacement = g_paste_text_item_new ("replaced");
    g_autoptr (GBytes) replacement_sv = g_bytes_new_static ("copy\nfile:///tmp/y", 18);

    g_paste_item_add_special_value (replacement, g_paste_binary_data_new (G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES,
                                                                          g_bytes_ref (replacement_sv)));

    /* The backend is incremental, so the fallback snapshot is never used. */
    g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (middle), replacement, NULL);

    /* Replacing a uuid that was never persisted must change nothing. */
    g_autoptr (GPasteItem) usurper = g_paste_text_item_new ("usurper");

    g_paste_storage_backend_replace_item (backend, name, "e8a95b3d-33ee-4b1e-8ee3-9c22c9635b8d", usurper, NULL);

    GList *loaded = NULL;
    gsize size = 0;

    g_paste_storage_backend_read_history (backend, name, &loaded, &size);

    g_assert_cmpuint (g_list_length (loaded), ==, 3);
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (loaded, 0)), ==, "front");
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (loaded, 1)), ==, "replaced");
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (loaded, 2)), ==, "back");
    g_assert_cmpstr (g_paste_item_get_uuid (g_list_nth_data (loaded, 1)), ==, g_paste_item_get_uuid (replacement));

    /* The old special value is gone, the replacement's is on file. */
    const GSList *svs = g_paste_item_get_special_values (g_list_nth_data (loaded, 1));

    g_assert_cmpuint (g_slist_length ((GSList *) svs), ==, 1);
    g_assert_cmpint (g_paste_binary_data_get_mime (svs->data), ==, G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES);
    g_assert_true (g_bytes_equal (g_paste_binary_data_get_bytes (svs->data), replacement_sv));

    g_list_free_full (loaded, g_object_unref);

    /* set_password turns an item into a password: it must leave storage, not
     * linger as a stale plaintext row. */
    g_autoptr (GPasteItem) password = g_paste_password_item_new ("login", "s3cr3t");

    g_paste_storage_backend_replace_item (backend, name, g_paste_item_get_uuid (replacement), password, NULL);

    loaded = NULL;
    g_paste_storage_backend_read_history (backend, name, &loaded, &size);

    g_assert_cmpuint (g_list_length (loaded), ==, 2);
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (loaded, 0)), ==, "front");
    g_assert_cmpstr (g_paste_item_get_value (g_list_nth_data (loaded, 1)), ==, "back");

    g_list_free_full (loaded, g_object_unref);
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
    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_ATOM_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>rich</b>", 11)));
    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_ATOM_GNOME_COPIED_FILES,
                                                                   g_bytes_new_static ("copy\nfile:///tmp/z", 18)));

    GPasteItem *survivor = g_paste_text_item_new ("survivor");
    g_paste_item_add_special_value (survivor, g_paste_binary_data_new (G_PASTE_SPECIAL_ATOM_TEXT_HTML,
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
    GList *items = g_list_append (NULL, g_paste_image_item_new_from_bytes (name, png, date, NULL));

    g_assert_nonnull (items->data);

    g_autoptr (GPasteStorageBackend) source = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_autoptr (GPasteStorageBackend) destination = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);

    /* What a sqlite -> file migration does: import, then clean the source up. */
    g_paste_storage_backend_write_history (source, name, items);
    g_paste_storage_backend_write_history (destination, name, items);

    const gchar *image_path = g_paste_item_get_value (items->data);

    g_assert_true (g_file_test (image_path, G_FILE_TEST_EXISTS));

    g_paste_storage_backend_delete_history (source, name, NULL);

    /* The destination still stores the name: its images must survive... */
    g_assert_true (g_file_test (image_path, G_FILE_TEST_EXISTS));

    GList *loaded = NULL;
    gsize size = 0;

    g_paste_storage_backend_read_history (destination, name, &loaded, &size);
    g_assert_cmpuint (g_list_length (loaded), ==, 1);
    g_list_free_full (loaded, g_object_unref);

    /* ...and go away with the last flavor storing it. */
    g_paste_storage_backend_delete_history (destination, name, NULL);
    g_assert_false (g_file_test (image_path, G_FILE_TEST_EXISTS));

    g_list_free_full (items, g_object_unref);
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
    GList *items = g_list_append (NULL, g_paste_image_item_new_from_bytes (name, png, date, NULL));

    g_assert_nonnull (items->data);

    /* The bytes-built item never touches the disk: its canonical cache file
     * does not exist. */
    const gchar *cache_path = g_paste_item_get_value (items->data);

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
    GList *loaded = NULL;
    gsize size = 0;

    g_paste_storage_backend_read_history (backend, name, &loaded, &size);

    g_assert_cmpuint (g_list_length (loaded), ==, 1);

    GPasteItem *read = loaded->data;
    const GPasteImageItem *image = _G_PASTE_IMAGE_ITEM (read);
    GBytes *read_png = g_paste_image_item_get_png_bytes (image);

    g_assert_cmpstr (g_paste_item_get_kind (read), ==, "Image");
    g_assert_nonnull (read_png);
    g_assert_true (g_bytes_equal (read_png, png));
    g_assert_cmpstr (g_paste_image_item_get_checksum (image),
                     ==, g_paste_image_item_get_checksum (_G_PASTE_IMAGE_ITEM (items->data)));

    /* The texture rebuilds from the bytes, with no file anywhere. */
    g_assert_false (g_file_test (g_paste_item_get_value (read), G_FILE_TEST_EXISTS));
    g_paste_item_set_state (read, G_PASTE_ITEM_STATE_ACTIVE);
    g_assert_nonnull (g_paste_image_item_get_image (image));
    g_paste_item_set_state (read, G_PASTE_ITEM_STATE_IDLE);

    g_list_free_full (loaded, g_object_unref);
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

/* A database created by a newer GPaste (higher user_version) must be left
 * alone: reads come back empty and writes are dropped instead of clobbering. */
static void
test_sqlite_version_guard (void)
{
    if (g_test_subprocess ())
    {
        /* The refusals below warn. With structured logging (G_LOG_USE_STRUCTURED)
         * g_test_expect_message cannot swallow them, so drop warning fatality in
         * this subprocess and let the parent match them on stderr instead. */
        g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

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
            GList *loaded = NULL;
            gsize size = 0;

            g_paste_storage_backend_read_history (backend, name, &loaded, &size);
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
        GList *loaded = NULL;
        gsize size = 0;

        g_paste_storage_backend_read_history (backend, name, &loaded, &size);
        g_assert_cmpuint (g_list_length (loaded), ==, 1);
        g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, "precious");
        g_list_free_full (loaded, g_object_unref);

        return;
    }

    g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed ();
    g_test_trap_assert_stderr ("*newer GPaste*");
}
#endif

#if defined(G_PASTE_ENABLE_SQLITE) && defined(G_PASTE_ENABLE_ENCRYPTION)
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

    g_paste_item_add_special_value (text, g_paste_binary_data_new (G_PASTE_SPECIAL_ATOM_TEXT_HTML,
                                                                   g_bytes_new_static (html_value, strlen (html_value))));
    items = g_list_append (items, text);
    items = g_list_append (items, g_paste_password_item_new (pw_name, secret));
    items = g_list_append (items, g_paste_color_item_new_from_str ("rgb(0,255,0)"));
    items = g_list_append (items, g_paste_image_item_new_from_bytes (name, png, date, NULL));

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
    GList *loaded = NULL;
    gsize size = 0;

    g_paste_storage_backend_read_history (backend, name, &loaded, &size);

    g_assert_cmpuint (g_list_length (loaded), ==, 4);

    const GPasteItem *read_text = loaded->data;
    const GPasteItem *read_password = loaded->next->data;
    const GPasteItem *read_color = loaded->next->next->data;
    const GPasteItem *read_image = loaded->next->next->next->data;

    g_assert_cmpstr (g_paste_item_get_value (read_text), ==, text_value);

    const GSList *svs = g_paste_item_get_special_values (read_text);
    g_assert_cmpuint (g_slist_length ((GSList *) svs), ==, 1);
    g_assert_cmpint (g_paste_binary_data_get_mime (svs->data), ==, G_PASTE_SPECIAL_ATOM_TEXT_HTML);
    g_assert_cmpmem (g_bytes_get_data (g_paste_binary_data_get_bytes (svs->data), NULL), strlen (html_value),
                     html_value, strlen (html_value));

    g_assert_cmpstr (g_paste_item_get_kind (read_password), ==, "Password");
    g_assert_cmpstr (g_paste_item_get_real_value (read_password), ==, secret);
    g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM ((gpointer) read_password)), ==, pw_name);

    g_assert_cmpstr (g_paste_item_get_value (read_color), ==, g_paste_item_get_value (g_list_nth_data (items, 2)));

    GBytes *read_png = g_paste_image_item_get_png_bytes (_G_PASTE_IMAGE_ITEM (read_image));

    g_assert_nonnull (read_png);
    g_assert_true (g_bytes_equal (read_png, png));

    g_list_free_full (loaded, g_object_unref);
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
        /* The refusals below warn; g_test_expect_message cannot swallow them
         * under structured logging, so drop warning fatality in this subprocess
         * and let the parent match them on stderr. */
        g_log_set_always_fatal (G_LOG_LEVEL_ERROR);

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
            GList *loaded = NULL;
            gsize size = 0;

            g_paste_storage_backend_read_history (backend, name, &loaded, &size);
            g_assert_null (loaded);

            GList *items = g_list_append (NULL, g_paste_text_item_new ("usurper"));

            g_paste_storage_backend_write_history (backend, name, items);
            g_list_free_full (items, g_object_unref);
        }

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "right passphrase");
            GList *loaded = NULL;
            gsize size = 0;

            g_paste_storage_backend_read_history (backend, name, &loaded, &size);
            g_assert_cmpuint (g_list_length (loaded), ==, 1);
            g_assert_cmpstr (g_paste_item_get_value (loaded->data), ==, "precious");
            g_list_free_full (loaded, g_object_unref);
        }

        /* An empty encrypted history holds nothing to lose: a different
         * passphrase re-keys it instead of refusing it. */
        const gchar *empty_name = "sqlite-encrypted-empty";

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "original passphrase");
            GList *loaded = NULL;
            gsize size = 0;

            g_paste_storage_backend_read_history (backend, empty_name, &loaded, &size);
            g_assert_null (loaded);
        }

        {
            g_autoptr (GPasteStorageBackend) backend = g_paste_sqlite_backend_new_encrypted (settings, "replacement passphrase");
            GList *loaded = NULL;
            gsize size = 0;

            g_paste_storage_backend_read_history (backend, empty_name, &loaded, &size);
            g_assert_null (loaded);

            GList *items = g_list_append (NULL, g_paste_text_item_new ("fresh start"));

            g_paste_storage_backend_write_history (backend, empty_name, items);
            g_list_free_full (items, g_object_unref);

            g_paste_storage_backend_read_history (backend, empty_name, &loaded, &size);
            g_assert_cmpuint (g_list_length (loaded), ==, 1);
            g_list_free_full (loaded, g_object_unref);
        }

        return;
    }

    g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
    g_test_trap_assert_passed ();
    g_test_trap_assert_stderr ("*does not unlock*");
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

    g_paste_history_empty (history);

    const gchar * const after_empty[] = { NULL };
    g_assert_true (sqlite_wait_for_values (G_PASTE_STORAGE_ENCRYPTED_SQLITE, settings, name, after_empty, 5000));

    g_paste_storage_backend_set_passphrase (NULL);
}
#endif

int
main (int argc, char *argv[])
{
    /* Keep any persistence the model schedules out of the real user data dir. */
    g_autofree gchar *tmp = g_dir_make_tmp ("gpaste-test-XXXXXX", NULL);
    if (tmp)
    {
        g_setenv ("XDG_DATA_HOME", tmp, TRUE);
        g_setenv ("XDG_CONFIG_HOME", tmp, TRUE);
        g_setenv ("XDG_CACHE_HOME", tmp, TRUE);
    }

    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/history/image_capture_does_not_write", test_image_capture_does_not_write);
    g_test_add_func ("/history/file_image_materialization", test_file_image_materialization);
    g_test_add_func ("/history/history_anchors_image", test_history_anchors_image);
    g_test_add_func ("/history/file_image_per_history", test_file_image_per_history);
    g_test_add_func ("/history/file_backup_owns_images", test_file_backup_owns_images);
    g_test_add_func ("/history/add_get_length", test_add_get_length);
    g_test_add_func ("/history/dedup_moves_to_front", test_dedup_moves_to_front);
    g_test_add_func ("/history/add_equal_first_is_noop", test_add_equal_first_is_noop);
    g_test_add_func ("/history/size_enforcement", test_size_enforcement);
    g_test_add_func ("/history/remove", test_remove);
    g_test_add_func ("/history/remove_by_uuid", test_remove_by_uuid);
    g_test_add_func ("/history/get_by_uuid", test_get_by_uuid);
    g_test_add_func ("/history/select_moves_to_front", test_select_moves_to_front);
    g_test_add_func ("/history/empty", test_empty);
    g_test_add_func ("/history/save_load_roundtrip", test_save_load_roundtrip);
    g_test_add_func ("/history/flush_persists_synchronously", test_flush_persists_synchronously);
    g_test_add_func ("/history/flush_stops_recording", test_flush_stops_recording);
    g_test_add_func ("/history/delete_refused_after_flush", test_delete_refused_after_flush);
    g_test_add_func ("/history/file_version_guard", test_file_version_guard);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/history/encrypted_roundtrip", test_encrypted_roundtrip);
    g_test_add_func ("/history/encrypted_explicit_passphrase", test_encrypted_explicit_passphrase);
#endif
#ifdef G_PASTE_ENABLE_SQLITE
    g_test_add_func ("/history/sqlite_roundtrip", test_sqlite_roundtrip);
    g_test_add_func ("/history/sqlite_incremental", test_sqlite_incremental);
    g_test_add_func ("/history/sqlite_replace", test_sqlite_replace);
    g_test_add_func ("/history/sqlite_cascade", test_sqlite_cascade);
    g_test_add_func ("/history/sqlite_migration_keeps_destination_images", test_sqlite_migration_keeps_destination_images);
    g_test_add_func ("/history/sqlite_image_blob", test_sqlite_image_blob);
    g_test_add_func ("/history/sqlite_no_rewrite_on_switch", test_sqlite_no_rewrite_on_switch);
    g_test_add_func ("/history/sqlite_version_guard", test_sqlite_version_guard);
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/history/encrypted_sqlite_roundtrip", test_encrypted_sqlite_roundtrip);
    g_test_add_func ("/history/encrypted_sqlite_wrong_passphrase", test_encrypted_sqlite_wrong_passphrase);
    g_test_add_func ("/history/encrypted_sqlite_incremental", test_encrypted_sqlite_incremental);
#endif
#endif

    return g_test_run ();
}
