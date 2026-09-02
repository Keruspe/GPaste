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
};

static void test_clipboard_iface_init (GPasteClipboardProviderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestClipboard, test_clipboard, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (G_PASTE_TYPE_CLIPBOARD_PROVIDER, test_clipboard_iface_init))

static gboolean
is_clipboard (GPasteClipboardProvider *provider)
{
    return TEST_CLIPBOARD (provider)->is_clipboard;
}

static const gchar *
get_text (GPasteClipboardProvider *provider)
{
    return g_paste_clipboard_content_get_text (&TEST_CLIPBOARD (provider)->content);
}

static const gchar *
get_image_checksum (GPasteClipboardProvider *provider)
{
    return g_paste_clipboard_content_get_image_checksum (&TEST_CLIPBOARD (provider)->content);
}

static gboolean
is_empty (GPasteClipboardProvider *provider)
{
    return g_paste_clipboard_content_is_empty (&TEST_CLIPBOARD (provider)->content);
}

static void
update (GPasteClipboardProvider              *provider,
        GPasteClipboardProviderUpdateCallback callback,
        gpointer                              user_data)
{
    if (callback)
        callback (provider, NULL, user_data);
}

static gboolean
select_item (GPasteClipboardProvider *provider,
             GPasteItem              *item)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

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

int
main (int argc, char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_data_func ("/clipboard/strip_head", GINT_TO_POINTER (TRUE), test_strip_refreshes_matching_selections);
    g_test_add_data_func ("/clipboard/strip_non_head", GINT_TO_POINTER (FALSE), test_strip_refreshes_matching_selections);
    return g_test_run ();
}
