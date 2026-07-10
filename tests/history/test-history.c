// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-history.h>
#include <gpaste-daemon/gpaste-text-item.h>

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
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <sqlite3.h>
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

    /* Write through the async saver: add two items, then drain the loop so the
     * background write reaches disk. */
    {
        g_autoptr (GPasteHistory) writer = make_plain_history ();
        g_paste_history_load (writer, name);

        g_paste_history_add (writer, g_paste_text_item_new ("alpha"));
        g_paste_history_add (writer, g_paste_text_item_new ("beta"));

        for (guint i = 0; i < 300; ++i)
        {
            while (g_main_context_iteration (NULL, FALSE))
                ;
            g_usleep (1000);
        }
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

    GList *items = NULL;
    items = g_list_append (items, g_paste_text_item_new ("plain text entry"));
    items = g_list_append (items, g_paste_password_item_new (pw_name, secret));

    g_paste_storage_backend_write_history (backend, name, items);

    /* On disk: ".xmls", starting with our magic, with no plaintext leaking. */
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xmls");
    g_autofree gchar *raw = NULL;
    gsize raw_len = 0;
    g_assert_true (g_file_get_contents (path, &raw, &raw_len, NULL));
    g_assert_cmpuint (raw_len, >=, 8);
    g_assert_cmpint (memcmp (raw, "GPSTENC1", 8), ==, 0);
    g_assert_null (g_strstr_len (raw, raw_len, secret));
    g_assert_null (g_strstr_len (raw, raw_len, "<?xml"));

    /* Read back through the same backend. */
    GList *loaded = NULL;
    gsize size = 0;
    g_paste_storage_backend_read_history (backend, name, &loaded, &size);
    g_assert_cmpuint (g_list_length (loaded), ==, 2);

    gboolean found_text = FALSE;
    gboolean found_password = FALSE;
    for (const GList *l = loaded; l; l = l->next)
    {
        GPasteItem *item = l->data;

        if (g_strcmp0 (g_paste_item_get_kind (item), "Password") == 0)
        {
            found_password = TRUE;
            g_assert_cmpstr (g_paste_item_get_real_value (item), ==, secret);
            g_assert_cmpstr (g_paste_password_item_get_name (G_PASTE_PASSWORD_ITEM (item)), ==, pw_name);
        }
        else
        {
            found_text = TRUE;
            g_assert_cmpstr (g_paste_item_get_value (item), ==, "plain text entry");
        }
    }
    g_assert_true (found_text);
    g_assert_true (found_password);

    g_list_free_full (loaded, g_object_unref);
    g_list_free_full (items, g_object_unref);
}
#endif

#ifdef G_PASTE_ENABLE_SQLITE
/* Read @name through a fresh backend until its stored values exactly match
 * @values, front to back (the saver persists in the background, so intermediate
 * states — e.g. a matching length — are not enough to know it caught up). */
static gboolean
sqlite_wait_for_values (GPasteSettings      *settings,
                        const gchar         *name,
                        const gchar * const *values,
                        guint                max_ms)
{
    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
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

    /* An image item loads its texture from disk at construction, so write a
     * real (1x1 transparent) PNG into our throwaway data dir. */
    gsize png_len = 0;
    g_autofree guchar *png = g_base64_decode ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==",
                                              &png_len);
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

    /* Image metadata survives. */
    const GPasteImageItem *image = _G_PASTE_IMAGE_ITEM (g_list_nth_data (loaded, 3));

    g_assert_cmpint (g_date_time_to_unix ((GDateTime *) g_paste_image_item_get_date (image)), ==, 1234567890);
    g_assert_cmpstr (g_paste_image_item_get_checksum (image), ==,
                     "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

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
    g_assert_true (sqlite_wait_for_values (settings, name, after_ops, 5000));

    /* Overflow past max-history-size: evicted tail rows must be reconciled
     * away even though they get no remove operation of their own. */
    for (guint i = 0; i < 6; ++i)
    {
        g_autofree gchar *text = g_strdup_printf ("item-%u", i);
        g_paste_history_add (history, g_paste_text_item_new (text));
    }

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 5);

    const gchar * const after_overflow[] = { "item-5", "item-4", "item-3", "item-2", "item-1", NULL };
    g_assert_true (sqlite_wait_for_values (settings, name, after_overflow, 5000));

    /* Emptying clears the database but keeps the history listed. */
    g_paste_history_empty (history);

    const gchar * const after_empty[] = { NULL };
    g_assert_true (sqlite_wait_for_values (settings, name, after_empty, 5000));

    g_autoptr (GPasteStorageBackend) backend = g_paste_storage_backend_new (G_PASTE_STORAGE_SQLITE, settings);
    g_auto (GStrv) names = g_paste_storage_backend_list_histories (backend, NULL);
    g_assert_true (g_strv_contains ((const gchar * const *) names, name));
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

        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
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
        g_assert_cmpint (sqlite3_open (path, &db), ==, SQLITE_OK);
        g_assert_cmpint (sqlite3_exec (db, "PRAGMA user_version = 1;", NULL, NULL, NULL), ==, SQLITE_OK);
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
#ifdef G_PASTE_ENABLE_ENCRYPTION
    g_test_add_func ("/history/encrypted_roundtrip", test_encrypted_roundtrip);
#endif
#ifdef G_PASTE_ENABLE_SQLITE
    g_test_add_func ("/history/sqlite_roundtrip", test_sqlite_roundtrip);
    g_test_add_func ("/history/sqlite_incremental", test_sqlite_incremental);
    g_test_add_func ("/history/sqlite_replace", test_sqlite_replace);
    g_test_add_func ("/history/sqlite_cascade", test_sqlite_cascade);
    g_test_add_func ("/history/sqlite_version_guard", test_sqlite_version_guard);
#endif

    return g_test_run ();
}
