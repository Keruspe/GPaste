// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-text-item.h>

#define TEST_TYPE_CLIPBOARD (test_clipboard_get_type ())
G_DECLARE_FINAL_TYPE (TestClipboard, test_clipboard, TEST, CLIPBOARD, GObject)

struct _TestClipboard
{
    GObject parent_instance;

    GPasteClipboardContent content;
    gboolean               is_clipboard;
    GPasteItem            *published;
    guint                  publications;
    GPasteClipboardUpdate *pending;
    gboolean               defer_reads;
};

static void test_clipboard_iface_init (GPasteClipboardProviderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestClipboard, test_clipboard, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (G_PASTE_TYPE_CLIPBOARD_PROVIDER, test_clipboard_iface_init))

static gboolean
is_clipboard (GPasteClipboardProvider *provider)
{
    return TEST_CLIPBOARD (provider)->is_clipboard;
}

static gboolean
is_reading (GPasteClipboardProvider *provider)
{
    return TEST_CLIPBOARD (provider)->pending != NULL;
}

static const gchar *
get_text (GPasteClipboardProvider *provider)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    return (self->pending) ? NULL : g_paste_clipboard_content_get_text (&self->content);
}

static const gchar *
get_image_checksum (GPasteClipboardProvider *provider)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    return (self->pending) ? NULL : g_paste_clipboard_content_get_image_checksum (&self->content);
}

static gboolean
is_empty (GPasteClipboardProvider *provider)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    return !self->pending && g_paste_clipboard_content_is_empty (&self->content);
}

static void
update (GPasteClipboardProvider              *provider,
        GPasteClipboardProviderUpdateCallback callback,
        gpointer                              user_data)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    if (self->defer_reads)
    {
        GPasteClipboardUpdate *pending = g_paste_clipboard_update_new (provider, CLIPBOARD_CONTENT_TEXT,
                                                                       &self->pending, &self->content, callback, user_data);

        /* The text and HTML replies are released independently by the test. */
        g_paste_clipboard_update_add_read (pending);
    }
    else if (callback)
        callback (provider, NULL, FALSE, user_data);
}

static gboolean
select_item (GPasteClipboardProvider *provider,
             GPasteItem              *item)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    g_paste_clipboard_update_supersede (&self->pending);
    g_paste_clipboard_content_set_text (&self->content, g_paste_item_get_real_value (item));
    g_set_object (&self->published, item);
    ++self->publications;
    return TRUE;
}

static void
test_clipboard_iface_init (GPasteClipboardProviderInterface *iface)
{
    iface->is_clipboard = is_clipboard;
    iface->get_text = get_text;
    iface->get_image_checksum = get_image_checksum;
    iface->is_reading = is_reading;
    iface->is_empty = is_empty;
    iface->update = update;
    iface->select_item = select_item;
}

static void
test_clipboard_dispose (GObject *object)
{
    TestClipboard *self = TEST_CLIPBOARD (object);

    g_clear_object (&self->published);
    g_paste_clipboard_content_clear (&self->content);
    G_OBJECT_CLASS (test_clipboard_parent_class)->dispose (object);
}

static void
test_clipboard_class_init (TestClipboardClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = test_clipboard_dispose;
}

static void
test_clipboard_init (TestClipboard *self G_GNUC_UNUSED)
{
}

static GPasteHistory *
make_history (GPasteSettings *settings)
{
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_NOOP);
    g_paste_settings_set_growing_lines (settings, FALSE);
    g_paste_settings_set_track_changes (settings, TRUE);

    GPasteHistory *history = g_paste_history_new (settings);

    g_paste_history_load (history, "clipboard-tests");
    return history;
}

static void
test_strip_refreshes_matching_selections (gconstpointer user_data)
{
    gboolean at_head = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    GPasteItem *rich = g_paste_text_item_new ("rich text");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                  g_bytes_new_static ("<b>rich text</b>", 16)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));

    g_paste_history_add (history, rich);
    if (!at_head)
        g_paste_history_add (history, g_paste_text_item_new ("later history item"));

    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
    g_autoptr (TestClipboard) primary = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&clipboard->content, "new untracked copy");
    g_paste_clipboard_content_set_text (&primary->content, "rich text");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_settings_set_track_changes (settings, FALSE);

    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .clipboards_manager = manager };
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);

    g_assert_no_error (error);
    g_assert_cmpstr (clipboard->content.str, ==, "new untracked copy");
    g_assert_cmpuint (clipboard->publications, ==, 0);
    g_assert_cmpuint (primary->publications, ==, 1);
    g_assert_cmpstr (g_paste_item_get_uuid (primary->published), ==, uuid);
    g_assert_null (g_paste_item_get_special_values (primary->published));
    g_assert_cmpuint (g_paste_history_get_length (history), ==, at_head ? 1 : 2);
}

static void
text_ready (TestClipboard         *self,
            GPasteSettings        *settings,
            GPasteClipboardUpdate *pending,
            const gchar           *text)
{
    if (!g_paste_clipboard_update_is_expired (pending))
    {
        g_autofree gchar *value = NULL;
        GPasteClipboardTextAction action = g_paste_clipboard_content_classify_text (&self->content, settings, self->is_clipboard, text, &value);

        if (action != G_PASTE_CLIPBOARD_TEXT_REJECT)
        {
            pending->produced = TRUE;
            pending->reselect = action == G_PASTE_CLIPBOARD_TEXT_RESELECT;
            g_set_str_take (&pending->text, g_steal_pointer (&value));
        }
    }

    g_paste_clipboard_update_maybe_done (pending);
}

static void
assert_superseded (GPasteClipboardUpdate *pending)
{
    gint64 deadline = g_get_monotonic_time () + 3 * G_USEC_PER_SEC;

    while (!pending->concluded && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    }

    g_assert_true (pending->concluded);
    g_assert_true (g_cancellable_is_cancelled (pending->guard.cancellable));
}

static void
test_overlapping_copies (gconstpointer user_data)
{
    gboolean first_text_ready = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);

    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&clipboard->content, "previous copy");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;

    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *first = clipboard->pending;

    if (first_text_ready)
        text_ready (clipboard, settings, first, "copied text");

    g_assert_cmpstr (clipboard->content.str, ==, "previous copy");
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *second = clipboard->pending;

    assert_superseded (first);
    text_ready (clipboard, settings, second, "copied text");
    g_assert_cmpstr (clipboard->content.str, ==, "previous copy");
    g_paste_clipboard_update_maybe_done (second);

    if (!first_text_ready)
        text_ready (clipboard, settings, first, "older copy");
    g_paste_clipboard_update_maybe_done (first);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "copied text");
    g_assert_cmpstr (get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)), ==, "copied text");

    /* A completed copy, unlike an abandoned one, still suppresses duplicates. */
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *duplicate = clipboard->pending;

    text_ready (clipboard, settings, duplicate, "copied text");
    g_assert_false (duplicate->produced);
    g_paste_clipboard_update_maybe_done (duplicate);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
}

static void
test_superseded_empty_cache (gconstpointer user_data)
{
    gboolean bootstrap = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = make_history (settings);

    g_paste_history_add (history, g_paste_text_item_new ("saved history"));

    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);

    clipboard->is_clipboard = TRUE;
    clipboard->defer_reads = bootstrap;
    if (!bootstrap)
        g_paste_clipboard_content_set_text (&clipboard->content, "previous copy");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;

    if (!bootstrap)
    {
        g_paste_clipboard_content_clear (&clipboard->content);
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    }
    GPasteClipboardUpdate *first = clipboard->pending;

    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *second = clipboard->pending;

    assert_superseded (first);
    g_assert_cmpuint (clipboard->publications, ==, 0);
    g_assert_true (clipboard->pending == second);

    text_ready (clipboard, settings, second, "new copy");
    g_paste_clipboard_update_maybe_done (second);
    text_ready (clipboard, settings, first, "old copy");
    g_paste_clipboard_update_maybe_done (first);

    g_assert_cmpuint (clipboard->publications, ==, 0);
    g_assert_cmpstr (get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)), ==, "new copy");
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
}

static void
test_strip_during_read (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    GPasteItem *rich = g_paste_text_item_new ("rich text");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                  g_bytes_new_static ("<b>rich text</b>", 16)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));
    g_paste_history_add (history, rich);
    if (variant == 2 || variant == 4)
        g_paste_history_add (history, g_paste_text_item_new ("later item"));

    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);

    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&clipboard->content, variant == 2 ? "later item" : "rich text");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *pending = clipboard->pending;

    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .clipboards_manager = manager };
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (clipboard->publications, ==, 0);
    if (variant == 3)
    {
        GPasteClipboardUpdate *superseded = pending;

        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        pending = clipboard->pending;
        text_ready (clipboard, settings, superseded, "old copy");
        g_paste_clipboard_update_maybe_done (superseded);
        g_assert_cmpuint (clipboard->publications, ==, 0);
    }
    if (variant == 4)
        g_assert_true (g_paste_history_remove_by_uuid (history, uuid));

    const gchar *text = variant == 0 ? "new copy" : "rich text";
    gboolean refreshed = variant != 0 && variant != 4;

    text_ready (clipboard, settings, pending, text);
    pending->mimes.special_mime[G_PASTE_SPECIAL_MIME_TEXT_HTML] = g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML, g_bytes_new_static ("<b>rich text</b>", 16));
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpstr (get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)), ==, text);
    g_assert_cmpuint (clipboard->publications, ==, refreshed ? 1 : 0);
    if (refreshed)
    {
        g_assert_nonnull (clipboard->published);
        g_assert_null (g_paste_item_get_special_values (clipboard->published));
        g_assert_cmpstr (g_paste_item_get_uuid (clipboard->published), ==, uuid);
        g_assert_null (g_paste_item_get_special_values (g_paste_history_get_by_uuid (history, uuid)));
    }
}

static void
fill_typed_read (GPasteClipboardUpdate *pending)
{
    pending->produced = TRUE;

    switch (pending->content_kind)
    {
    case CLIPBOARD_CONTENT_IMAGE:
    {
        static const guint8 pixel[] = { 255, 0, 0, 255 };
        g_autoptr (GBytes) bytes = g_bytes_new_static (pixel, sizeof pixel);

        pending->texture = gdk_memory_texture_new (1, 1, GDK_MEMORY_R8G8B8A8, bytes, sizeof pixel);
        break;
    }
    case CLIPBOARD_CONTENT_COLOR:
        pending->rgba = (GdkRGBA) { 1, 0, 0, 1 };
        break;
    case CLIPBOARD_CONTENT_FILE_LIST:
    {
        g_autoptr (GFile) file = g_file_new_for_uri ("file:///tmp/clipboard-test");

        pending->file_list = gdk_file_list_new_from_array (&file, 1);
        break;
    }
    default:
        g_assert_not_reached ();
    }
}

static void
test_typed_cache_commit (gconstpointer user_data)
{
    GPasteClipboardContentKind kind = GPOINTER_TO_INT (user_data);
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
    GPasteClipboardProvider *provider = G_PASTE_CLIPBOARD_PROVIDER (clipboard);

    g_paste_clipboard_content_set_text (&clipboard->content, "previous copy");
    GPasteClipboardUpdate *first = g_paste_clipboard_update_new (provider, kind, &clipboard->pending, &clipboard->content, NULL, NULL);

    fill_typed_read (first);
    g_assert_cmpint (clipboard->content.kind, ==, CLIPBOARD_CONTENT_TEXT);
    GPasteClipboardUpdate *second = g_paste_clipboard_update_new (provider, kind, &clipboard->pending, &clipboard->content, NULL, NULL);

    fill_typed_read (second);
    g_paste_clipboard_update_maybe_done (second);
    g_assert_cmpint (clipboard->content.kind, ==, kind);
    g_paste_clipboard_update_maybe_done (first);
    g_assert_cmpint (clipboard->content.kind, ==, kind);

    switch (kind)
    {
    case CLIPBOARD_CONTENT_IMAGE:
        g_assert_nonnull (get_image_checksum (provider));
        break;
    case CLIPBOARD_CONTENT_COLOR:
    {
        const GdkRGBA red = { 1, 0, 0, 1 };

        g_assert_true (gdk_rgba_equal (&clipboard->content.rgba, &red));
        break;
    }
    case CLIPBOARD_CONTENT_FILE_LIST:
    {
        g_autoptr (GSList) files = gdk_file_list_get_files (clipboard->content.file_list);
        g_autofree gchar *uri = g_file_get_uri (files->data);

        g_assert_cmpstr (uri, ==, "file:///tmp/clipboard-test");
        break;
    }
    default:
        g_assert_not_reached ();
    }
}

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_data_func ("/clipboard/strip_head", GINT_TO_POINTER (TRUE), test_strip_refreshes_matching_selections);
    g_test_add_data_func ("/clipboard/strip_non_head", GINT_TO_POINTER (FALSE), test_strip_refreshes_matching_selections);
    g_test_add_data_func ("/clipboard/overlapping_same_text", GINT_TO_POINTER (TRUE), test_overlapping_copies);
    g_test_add_data_func ("/clipboard/late_text_reply", GINT_TO_POINTER (FALSE), test_overlapping_copies);
    g_test_add_data_func ("/clipboard/superseded_bootstrap", GINT_TO_POINTER (TRUE), test_superseded_empty_cache);
    g_test_add_data_func ("/clipboard/superseded_update", GINT_TO_POINTER (FALSE), test_superseded_empty_cache);
    g_test_add_data_func ("/clipboard/strip_during_read/unrelated", GUINT_TO_POINTER (0), test_strip_during_read);
    g_test_add_data_func ("/clipboard/strip_during_read/matching", GUINT_TO_POINTER (1), test_strip_during_read);
    g_test_add_data_func ("/clipboard/strip_during_read/non_head", GUINT_TO_POINTER (2), test_strip_during_read);
    g_test_add_data_func ("/clipboard/strip_during_read/superseded", GUINT_TO_POINTER (3), test_strip_during_read);
    g_test_add_data_func ("/clipboard/strip_during_read/removed", GUINT_TO_POINTER (4), test_strip_during_read);
    g_test_add_data_func ("/clipboard/image_cache", GINT_TO_POINTER (CLIPBOARD_CONTENT_IMAGE), test_typed_cache_commit);
    g_test_add_data_func ("/clipboard/color_cache", GINT_TO_POINTER (CLIPBOARD_CONTENT_COLOR), test_typed_cache_commit);
    g_test_add_data_func ("/clipboard/file_list_cache", GINT_TO_POINTER (CLIPBOARD_CONTENT_FILE_LIST), test_typed_cache_commit);
    return g_test_run ();
}
