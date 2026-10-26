// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The history and its storage, the daemon's own sources compiled in, against
 * memory GSettings and directories of the test's own. */

#include <gpaste/gpaste-util.h>
#include <gpaste-history.h>
#include <gpaste-image-item.h>
#include <gpaste-text-item.h>

#include <string.h>

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

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func ("/history/select-counts-image-once", test_select_counts_image_once);

    return g_test_run ();
}
