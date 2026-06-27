// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-history.h>
#include <gpaste-text-item.h>

#ifdef G_PASTE_ENABLE_ENCRYPTION
#include <gpaste/gpaste-util.h>

#include <gpaste-file-backend.h>
#include <gpaste-password-item.h>
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
    g_paste_settings_set_save_history (settings, TRUE);

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

    return g_test_run ();
}
