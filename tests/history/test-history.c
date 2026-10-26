// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The history and its storage, the daemon's own sources compiled in, against
 * memory GSettings and directories of the test's own. */

#include <gpaste/gpaste-util.h>
#include <gpaste-daemon.h>
#include <gpaste-history.h>
#include <gpaste-image-item.h>
#include <gpaste-password-item.h>
#include <gpaste-text-item.h>

#include <glib/gstdio.h>

#include <string.h>
#include <sys/stat.h>

/* Iterate the main context until @path exists: what an image item writes at
 * capture, on a worker thread. */
static void
wait_for_file (const gchar *path)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

    while (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
        g_usleep (G_USEC_PER_SEC / 100);
    }
}

/* Iterate the main context until the writes of the history called @name have
 * all landed: each runs on a thread of its own and the next is chained from the
 * main context, so nothing left to dispatch and no temporary file for a while
 * is none in flight. A test ending with one out would have its directories
 * removed under it. */
static void
settle_writes (const gchar *name)
{
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xml");
    g_autofree gchar *tmp = g_strconcat (path, ".tmp", NULL);
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

    for (guint quiet = 0; quiet < 10;)
    {
        gboolean dispatched = FALSE;

        while (g_main_context_iteration (NULL, FALSE))
            dispatched = TRUE;
        quiet = (dispatched || g_file_test (tmp, G_FILE_TEST_EXISTS)) ? 0 : quiet + 1;
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_usleep (G_USEC_PER_SEC / 50);
    }
}

/* Iterate the main context until the history file @name holds @needle: what a
 * write on the history's own thread lands with, the history writing every
 * change of its own. */
static void
wait_for_saved (const gchar *name,
                const gchar *needle)
{
    g_autofree gchar *path = g_paste_util_get_history_file_path (name, "xml");
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

    for (;;)
    {
        g_autofree gchar *contents = NULL;

        if (g_file_get_contents (path, &contents, NULL, NULL) && strstr (contents, needle))
            break;
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_main_context_iteration (NULL, FALSE);
        g_usleep (G_USEC_PER_SEC / 100);
    }

    settle_writes (name);
}

/* Remove @path and all it holds, whatever its name is in. */
static void
remove_tree (const gchar *path)
{
    g_autoptr (GDir) dir = g_dir_open (path, 0, NULL);

    for (const gchar *entry; dir && (entry = g_dir_read_name (dir));)
    {
        g_autofree gchar *child = g_build_filename (path, entry, NULL);

        if (g_file_test (child, G_FILE_TEST_IS_DIR) && !g_file_test (child, G_FILE_TEST_IS_SYMLINK))
            remove_tree (child);
        else
            g_assert_cmpint (g_remove (child), ==, 0);
    }

    g_assert_cmpint (g_rmdir (path), ==, 0);
}

static gchar *
dup_uuid_at (GPasteHistory *history,
             guint64        index)
{
    return g_strdup (g_paste_item_get_uuid (g_paste_history_get (history, index)));
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

/* Selecting an idle image decodes it again, and the history's total has to
 * count that texture once: counted twice, the memory cap evicts entries to pay
 * for memory nothing uses -- here every one but the head, the image weighing
 * just over half the cap. */
static void
test_select_counts_image_once (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

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

    g_autofree gchar *png = g_strconcat (g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (image)), ".png", NULL);
    g_autofree gchar *path = g_build_filename (g_get_user_data_dir (), "gpaste", "images", png, NULL);

    g_paste_history_add (history, image);
    /* Read back from there once the image is idle. */
    wait_for_file (path);

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
    settle_writes ("select-counts-image-once");
}

/* A value of whitespace alone -- blank lines copied -- is written as it is, and
 * read back: dropped, it would be gone for good at the next save. */
static void
test_blank_value_kept (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    {
        g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

        g_paste_history_load (history, "blank-value");
        g_paste_history_add (history, g_paste_text_item_new ("  \n\t "));
        g_paste_history_add (history, g_paste_text_item_new ("visible"));
        wait_for_saved ("blank-value", "visible");
    }

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "blank-value");
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 1)), ==, "  \n\t ");
    settle_writes ("blank-value");
}

/* An attribute that does not belong to an item's kind -- a checksum on a
 * text -- is skipped, the item keeping the attributes after it, its uuid
 * among them. */
static void
test_stray_checksum_skipped (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *path = g_paste_util_get_history_file_path ("stray", "xml");
    g_autofree gchar *dir = g_path_get_dirname (path);
    const gchar *document = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                            "<history version=\"2.0\">\n"
                            "  <item kind=\"Text\" checksum=\"0000000000000000000000000000000000000000000000000000000000000000\" "
                            "uuid=\"00000000-0000-4000-8000-000000000001\">\n"
                            "    <value><![CDATA[kept]]></value>\n  </item>\n"
                            "</history>\n";

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, document, -1, NULL));

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "stray");
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "kept");
    g_assert_cmpstr (g_paste_item_get_uuid (g_paste_history_get (history, 0)), ==, "00000000-0000-4000-8000-000000000001");
    settle_writes ("stray");
}

/* An image's checksum is a SHA-256 in lowercase hex, and the writer puts it in
 * an attribute as it is: one read off a file that is anything else -- a quote
 * in it -- is dropped, the image computing its own again, rather than written
 * back unescaped, which would make the whole file unreadable at the next
 * start. */
static void
test_invalid_checksum_dropped (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*invalid image checksum*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *images = g_build_filename (dir, "images", NULL);
    g_autofree gchar *png = g_build_filename (images, "quoted.png", NULL);
    g_autofree gchar *path = g_paste_util_get_history_file_path ("quoted-checksum", "xml");
    g_autoptr (GPasteItem) source = uniform_image (4);

    g_paste_settings_set_images_support (settings, TRUE);
    g_assert_cmpint (g_mkdir_with_parents (images, 0700), ==, 0);
    g_assert_true (gdk_texture_save_to_png (g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (source)), png));

    g_autofree gchar *document = g_strdup_printf ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                                  "<history version=\"2.0\">\n"
                                                  "  <item kind=\"Text\" uuid=\"%s\">\n    <value><![CDATA[text]]></value>\n  </item>\n"
                                                  "  <item kind=\"Image\" uuid=\"%s\" date=\"1234567890\" checksum=\"ab&quot;cd\">\n"
                                                  "    <value><![CDATA[%s]]></value>\n  </item>\n"
                                                  "</history>\n",
                                                  "00000000-0000-4000-8000-000000000001", "00000000-0000-4000-8000-000000000002", png);

    g_assert_true (g_file_set_contents (path, document, -1, NULL));

    {
        g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

        g_paste_history_load (history, "quoted-checksum");
        g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
        /* Any change writes the file again. */
        g_paste_history_add (history, g_paste_text_item_new ("written back"));
        wait_for_saved ("quoted-checksum", "written back");
    }

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "quoted-checksum");
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);
    settle_writes ("quoted-checksum");
}

/* An image's path is under the history directory, and one under a home
 * directory whose name is not UTF-8 has no place in the document, which only
 * holds UTF-8: the image is left out of the file, the rest of the history saved
 * around it, rather than the save crashing on it. The child's directories are
 * made under a TMPDIR that is not UTF-8 (G_TEST_OPTION_ISOLATE_DIRS). */
static void
test_unnamable_image_left_out (void)
{
    if (!g_test_subprocess ())
    {
        g_autofree gchar *template = g_build_filename (g_get_tmp_dir (), "gpaste-caf\xe9-XXXXXX", NULL);
        g_autofree gchar *tmp = g_mkdtemp (g_steal_pointer (&template));
        g_auto (GStrv) envp = g_environ_setenv (g_environ_setenv (g_get_environ (), "TMPDIR", tmp, TRUE), "G_TEST_TMPDIR", tmp, TRUE);

        g_assert_nonnull (tmp);
        g_test_trap_subprocess_with_envp (NULL, (const gchar * const *) envp, 0, G_TEST_SUBPROCESS_DEFAULT);
        remove_tree (tmp);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*not UTF-8*");
        /* Said at the first save leaving it out, not at every one. */
        g_test_trap_assert_stderr_unmatched ("*not UTF-8*not UTF-8*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);
    g_assert_false (g_utf8_validate (g_get_user_data_dir (), -1, NULL));

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    GPasteItem *image = uniform_image (4);
    g_autofree gchar *png = g_strconcat (g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (image)), ".png", NULL);
    g_autofree gchar *dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *path = g_build_filename (dir, "images", png, NULL);

    g_paste_history_load (history, "unnamable-image");
    g_paste_history_add (history, image);
    wait_for_file (path);
    g_paste_history_add (history, g_paste_text_item_new ("saved around it"));
    wait_for_saved ("unnamable-image", "saved around it");

    g_autoptr (GPasteHistory) read = g_paste_history_new (settings);

    g_paste_history_load (read, "unnamable-image");
    g_assert_cmpuint (g_paste_history_get_length (read), ==, 1);
    settle_writes ("unnamable-image");
}

/* A line growing over the head replaces it, and an older copy of the line it
 * grew into goes as any duplicate does: the history then lists it once. */
static void
test_growing_line_drops_older_copy (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_set_growing_lines (settings, TRUE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "growing-line");
    g_paste_history_add (history, g_paste_text_item_new ("abcdef"));
    g_paste_history_add (history, g_paste_text_item_new ("x"));
    g_paste_history_add (history, g_paste_text_item_new ("abc"));
    g_paste_history_add (history, g_paste_text_item_new ("abcdef"));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "abcdef");
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 1)), ==, "x");

    /* A line the grown one merely ends with is another item, kept: the exact
     * copy deeper is the one that goes. */
    g_paste_history_add (history, g_paste_text_item_new ("def"));
    g_paste_history_add (history, g_paste_text_item_new ("abcde"));
    g_paste_history_add (history, g_paste_text_item_new ("abcdef"));

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 3);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "abcdef");
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 1)), ==, "def");
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 2)), ==, "x");

    settle_writes ("growing-line");

    /* Nor when the head stays: "x" grows into nothing, and the "c" a new "abc"
     * ends with is kept, its exact copy deeper the one that goes. */
    g_autoptr (GPasteHistory) kept = g_paste_history_new (settings);

    g_paste_history_load (kept, "growing-line-kept");
    g_paste_history_add (kept, g_paste_text_item_new ("abc"));
    g_paste_history_add (kept, g_paste_text_item_new ("c"));
    g_paste_history_add (kept, g_paste_text_item_new ("x"));
    g_paste_history_add (kept, g_paste_text_item_new ("abc"));

    g_assert_cmpuint (g_paste_history_get_length (kept), ==, 3);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (kept, 0)), ==, "abc");
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (kept, 1)), ==, "x");
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (kept, 2)), ==, "c");

    /* Failing an exact copy, a line deeper that the new one grows over is
     * replaced, as the head would be: "c" by "cde", below "abc". */
    g_paste_history_add (kept, g_paste_text_item_new ("cde"));

    g_assert_cmpuint (g_paste_history_get_length (kept), ==, 3);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (kept, 0)), ==, "cde");
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (kept, 1)), ==, "abc");
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (kept, 2)), ==, "x");
    settle_writes ("growing-line-kept");
}

/* What a damaged history file holds stays out of the journal, which outlives
 * it and has readers of its own: a value not in UTF-8 and text out of place
 * are said to be there, not quoted. */
static void
test_damage_not_logged (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        /* A word no path holds: the program's own, in every line, may be
         * under meson-private/. */
        g_test_trap_assert_stderr_unmatched ("*xyzzy*");
        g_test_trap_assert_stderr ("*history file*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *path = g_paste_util_get_history_file_path ("damaged", "xml");
    g_autofree gchar *dir = g_path_get_dirname (path);
    const gchar *document = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                            "<history version=\"2.0\">\n"
                            "  <item kind=\"Text\" uuid=\"00000000-0000-4000-8000-000000000001\">xyzzy text out of place\n"
                            "    <value><![CDATA[kept]]></value>\n  </item>\n"
                            "  <item kind=\"Text\" uuid=\"00000000-0000-4000-8000-000000000002\">\n"
                            "    <value><![CDATA[xyzzy \xff value]]></value>\n  </item>\n"
                            "</history>\n";

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_true (g_file_set_contents (path, document, -1, NULL));

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "damaged");
    settle_writes ("damaged");
}

/* A FIFO named like a history holds open (2) until a writer comes, which none
 * ever does: it is no history, left out of the listing and read as none,
 * rather than the daemon frozen on it. The child gets 10 s, so a hang fails
 * the test rather than stalling the suite. */
static void
test_fifo_not_a_history (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 10 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        g_test_trap_assert_stderr ("*not a regular file*");
        return;
    }

    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autofree gchar *fifo = g_paste_util_get_history_file_path ("fifo", "xml");
    g_autofree gchar *dir = g_path_get_dirname (fifo);

    g_assert_cmpint (g_mkdir_with_parents (dir, 0700), ==, 0);
    g_assert_cmpint (mkfifo (fifo, 0600), ==, 0);

    {
        g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

        g_paste_history_load (history, "regular");
        g_paste_history_add (history, g_paste_text_item_new ("stored"));
        wait_for_saved ("regular", "stored");
    }

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) histories = g_paste_history_list (history, &error);

    g_assert_no_error (error);
    g_assert_cmpstrv (histories, ((const gchar * const []) { "regular", NULL }));

    g_paste_history_load (history, "fifo");
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    settle_writes ("fifo");
}

/* AddFile reads only a regular file, and only up to the memory cap: a FIFO
 * would hold the daemon until a writer came, which none ever does, and a
 * device like /dev/zero would never end. The child gets 10 s, so a hang fails
 * the test rather than stalling the suite. */
static void
test_add_file_not_regular (void)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 10 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
        return;
    }

    g_autofree gchar *dir = g_dir_make_tmp ("gpaste-add-file-XXXXXX", NULL);
    g_autofree gchar *fifo = g_build_filename (dir, "fifo", NULL);
    g_autoptr (GError) error = NULL;
    gsize length = 0;

    g_assert_cmpint (mkfifo (fifo, 0600), ==, 0);

    g_autofree gchar *from_fifo = g_paste_daemon_read_file (fifo, 1024, &length, &error);

    g_assert_null (from_fifo);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE);
    g_clear_error (&error);

    g_autofree gchar *from_zero = g_paste_daemon_read_file ("/dev/zero", 1024, &length, &error);

    g_assert_null (from_zero);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE);

    g_unlink (fifo);
    g_rmdir (dir);
}

static void
test_add_file_cap (void)
{
    g_autofree gchar *dir = g_dir_make_tmp ("gpaste-add-file-XXXXXX", NULL);
    g_autofree gchar *path = g_build_filename (dir, "file", NULL);
    g_autoptr (GError) error = NULL;
    gsize length = 0;

    g_assert_true (g_file_set_contents (path, "0123456789abcdef", 16, NULL));

    g_autofree gchar *at_cap = g_paste_daemon_read_file (path, 16, &length, &error);

    g_assert_no_error (error);
    g_assert_cmpstr (at_cap, ==, "0123456789abcdef");
    g_assert_cmpuint (length, ==, 16);

    g_autofree gchar *past_cap = g_paste_daemon_read_file (path, 15, &length, &error);

    g_assert_null (past_cap);
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);

    g_unlink (path);
    g_rmdir (dir);
}

/* A procfs file reports a size of 0 and is read to its end all the same. */
static void
test_add_file_procfs (void)
{
    if (!g_file_test ("/proc/self/status", G_FILE_TEST_EXISTS))
    {
        g_test_skip ("no procfs");
        return;
    }

    g_autoptr (GError) error = NULL;
    gsize length = 0;
    g_autofree gchar *status = g_paste_daemon_read_file ("/proc/self/status", 1024 * 1024, &length, &error);

    g_assert_no_error (error);
    g_assert_cmpuint (length, >, 0);
    g_assert_cmpuint (strlen (status), ==, length);
    g_assert_true (g_str_has_prefix (status, "Name:"));
}

/* A password is found by its name as a text is by its value, whatever the
 * case, and never by the placeholder its value is. */
static void
test_search_password_name (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);

    g_paste_history_load (history, "passwords");
    g_paste_history_add (history, g_paste_password_item_new ("Bank Account", "secret"));
    g_paste_history_add (history, g_paste_text_item_new ("plain (text)"));

    g_auto (GStrv) by_name = g_paste_history_search (history, "bank", NULL);
    g_autofree gchar *escaped = g_regex_escape_string ("*", -1);
    g_auto (GStrv) by_placeholder = g_paste_history_search (history, escaped, NULL);
    g_autofree gchar *parenthesis = g_regex_escape_string ("(t", -1);
    g_auto (GStrv) by_text = g_paste_history_search (history, parenthesis, NULL);

    g_assert_cmpuint (g_strv_length (by_name), ==, 1);
    g_assert_cmpuint (g_strv_length (by_placeholder), ==, 0);
    g_assert_cmpuint (g_strv_length (by_text), ==, 1);
    settle_writes ("passwords");
}

/* What a search refuses, with an error rather than a word in the journal: a
 * query past the length it takes, and one that is no regular expression. */
static void
test_search_query_valid (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autofree gchar *longest = g_strnfill (G_PASTE_HISTORY_MAX_SEARCH_LENGTH, 'a');
    g_autofree gchar *too_long = g_strnfill (G_PASTE_HISTORY_MAX_SEARCH_LENGTH + 1, 'a');
    g_autofree gchar *escaped = g_regex_escape_string ("(", -1);
    const gchar *valid[] = { "abc", escaped, longest };

    g_paste_history_load (history, "queries");

    for (guint i = 0; i < G_N_ELEMENTS (valid); ++i)
    {
        g_autoptr (GError) error = NULL;
        g_auto (GStrv) results = g_paste_history_search (history, valid[i], &error);

        g_assert_no_error (error);
        g_assert_nonnull (results);
    }

    g_autoptr (GError) invalid = NULL;
    g_auto (GStrv) none = g_paste_history_search (history, "(", &invalid);

    g_assert_null (none);
    g_assert_nonnull (invalid);
    g_assert_cmpuint (invalid->domain, ==, G_REGEX_ERROR);

    g_autoptr (GError) long_error = NULL;
    g_auto (GStrv) none_either = g_paste_history_search (history, too_long, &long_error);

    g_assert_null (none_either);
    g_assert_error (long_error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    settle_writes ("queries");
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func ("/history/select-counts-image-once", test_select_counts_image_once);
    g_test_add_func ("/history/blank-value-kept", test_blank_value_kept);
    g_test_add_func ("/history/stray-checksum-skipped", test_stray_checksum_skipped);
    g_test_add_func ("/history/invalid-checksum-dropped", test_invalid_checksum_dropped);
    g_test_add_func ("/history/unnamable-image-left-out", test_unnamable_image_left_out);
    g_test_add_func ("/history/growing-line-drops-older-copy", test_growing_line_drops_older_copy);
    g_test_add_func ("/history/damage-not-logged", test_damage_not_logged);
    g_test_add_func ("/history/fifo-not-a-history", test_fifo_not_a_history);
    g_test_add_func ("/history/add-file-not-regular", test_add_file_not_regular);
    g_test_add_func ("/history/add-file-cap", test_add_file_cap);
    g_test_add_func ("/history/add-file-procfs", test_add_file_procfs);
    g_test_add_func ("/history/search-password-name", test_search_password_name);
    g_test_add_func ("/history/search-query-valid", test_search_query_valid);

    return g_test_run ();
}
