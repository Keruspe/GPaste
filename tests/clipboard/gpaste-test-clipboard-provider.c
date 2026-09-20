// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-test-clipboard-provider.h"

static void test_clipboard_iface_init (GPasteClipboardProviderInterface *iface);

G_DEFINE_TYPE_WITH_CODE (TestClipboard, test_clipboard, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE (G_PASTE_TYPE_CLIPBOARD_PROVIDER, test_clipboard_iface_init))

static gboolean
is_clipboard (GPasteClipboardProvider *provider G_GNUC_UNUSED)
{
    return TRUE;
}

static gboolean
is_reading (GPasteClipboardProvider *provider)
{
    return TEST_CLIPBOARD (provider)->pending != NULL;
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
        callback (provider, NULL, FALSE, CLIPBOARD_SECRET_NO, user_data);
}

static gboolean
select_item (GPasteClipboardProvider *provider G_GNUC_UNUSED,
             GPasteItem              *item G_GNUC_UNUSED)
{
    g_assert_not_reached ();
}

static void
select_text (GPasteClipboardProvider *provider G_GNUC_UNUSED,
             const gchar             *text G_GNUC_UNUSED)
{
    g_assert_not_reached ();
}

static void
sync_text (GPasteClipboardProvider    *provider G_GNUC_UNUSED,
           GPasteClipboardProvider    *other G_GNUC_UNUSED,
           GPasteClipboardSyncCallback callback G_GNUC_UNUSED,
           gpointer                    user_data G_GNUC_UNUSED,
           GDestroyNotify              destroy G_GNUC_UNUSED)
{
    g_assert_not_reached ();
}

static void
test_clipboard_iface_init (GPasteClipboardProviderInterface *iface)
{
    iface->is_clipboard = is_clipboard;
    iface->get_text = get_text;
    iface->get_image_checksum = get_image_checksum;
    iface->is_empty = is_empty;
    iface->is_reading = is_reading;
    iface->update = update;
    iface->select_item = select_item;
    iface->select_text = select_text;
    iface->sync_text = sync_text;
}

/* The cached value is a plain allocation, and a disposed provider still answers
 * get_text () for it, as the GDK backend does. */
static void
test_clipboard_finalize (GObject *object)
{
    g_paste_clipboard_content_clear (&TEST_CLIPBOARD (object)->content);
    G_OBJECT_CLASS (test_clipboard_parent_class)->finalize (object);
}

static void
test_clipboard_class_init (TestClipboardClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = test_clipboard_finalize;
}

static void
test_clipboard_init (TestClipboard *self G_GNUC_UNUSED)
{
}

TestClipboard *
test_clipboard_new (void)
{
    return g_object_new (TEST_TYPE_CLIPBOARD, NULL);
}

void
test_clipboard_text_ready (GPasteClipboardUpdate *pending,
                           const gchar           *text)
{
    if (!g_paste_clipboard_update_is_expired (pending))
    {
        pending->produced = TRUE;
        g_set_str (&pending->text, text);
    }

    g_paste_clipboard_update_maybe_done (pending);
}
