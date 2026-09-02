// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-password-item.h>

#define TEST_TYPE_CLIPBOARD (test_clipboard_get_type ())
G_DECLARE_FINAL_TYPE (TestClipboard, test_clipboard, TEST, CLIPBOARD, GObject)

struct _TestClipboard
{
    GObject parent_instance;

    GPasteClipboardContent   content;
    /* Borrowed: what the test configures is what a read must be classified
     * against, so the mock hands this very object to every update it builds. */
    GPasteSettings          *settings;
    gboolean                 is_clipboard;
    GPasteItem              *published;
    const gchar             *refuse_value;
    guint                    publications;
    GPasteClipboardUpdate   *pending;
    gboolean                 defer_reads;
    gboolean                 reject_next;
    gboolean                 defer_sync;
    guint                    sync_reads;
    GPasteClipboardSyncData *sync;
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

    return g_paste_clipboard_content_get_text (&self->content);
}

static const gchar *
get_image_checksum (GPasteClipboardProvider *provider)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    return g_paste_clipboard_content_get_image_checksum (&self->content);
}

static gboolean
is_empty (GPasteClipboardProvider *provider)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    return g_paste_clipboard_content_is_empty (&self->content);
}

static void
update (GPasteClipboardProvider              *provider,
        GPasteClipboardProviderUpdateCallback callback,
        gpointer                              user_data)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    if (self->defer_reads)
    {
        GPasteClipboardUpdate *pending = g_paste_clipboard_update_new (provider, self->settings, CLIPBOARD_CONTENT_TEXT,
                                                                       &self->pending, &self->content, callback, user_data);

        /* The text and HTML replies are released independently by the test. */
        g_paste_clipboard_update_add_read (pending);
    }
    else if (callback)
        callback (provider, NULL, FALSE, CLIPBOARD_SECRET_NO, user_data);
}

static gboolean
select_item (GPasteClipboardProvider *provider,
             GPasteItem              *item)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    if (self->reject_next)
    {
        self->reject_next = FALSE;
        return FALSE;
    }

    if (self->refuse_value && g_str_equal (self->refuse_value, g_paste_item_get_real_value (item)))
        return FALSE;

    g_paste_clipboard_update_supersede (&self->pending);
    g_paste_clipboard_content_set_text (&self->content, g_paste_item_get_real_value (item));
    g_set_object (&self->published, item);
    ++self->publications;
    return TRUE;
}

static void
select_text (GPasteClipboardProvider *provider,
             const gchar             *text)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);

    g_paste_clipboard_update_supersede (&self->pending);
    g_paste_clipboard_content_set_text (&self->content, text);
    g_clear_object (&self->published);
    ++self->publications;
}

static void
sync_text (GPasteClipboardProvider    *provider,
           GPasteClipboardProvider    *other,
           GPasteClipboardSyncCallback callback,
           gpointer                    user_data,
           GDestroyNotify              destroy)
{
    TestClipboard *self = TEST_CLIPBOARD (provider);
    GPasteClipboardSyncData *data = g_paste_clipboard_sync_data_new (other, callback, user_data, destroy);

    ++self->sync_reads;
    if (self->defer_sync)
        self->sync = data;
    else
    {
        g_paste_clipboard_sync_data_deliver (data, self->content.str);
        g_paste_clipboard_sync_data_free (data);
    }
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

static void
test_clipboard_dispose (GObject *object)
{
    TestClipboard *self = TEST_CLIPBOARD (object);

    g_clear_object (&self->published);
    /* A sync a test deferred and never delivered is one this frees, so no test
     * has to remember to: the data owns a guard, a cancellable and the refs
     * behind its destroy notify. */
    g_clear_pointer (&self->sync, g_paste_clipboard_sync_data_free);
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

static TestClipboard *
make_clipboard (GPasteSettings *settings)
{
    TestClipboard *self = g_object_new (TEST_TYPE_CLIPBOARD, NULL);

    self->settings = settings;
    return self;
}

/* Every case starts from the schema's defaults. The memory backend is shared by
 * the whole binary, so a key one case sets would otherwise reach every case
 * after it -- and a case run alone with -p would see different settings. */
static GPasteSettings *
make_settings (void)
{
    g_autoptr (GSettings) raw = g_settings_new (G_PASTE_SETTINGS_NAME);
    g_autoptr (GSettingsSchema) schema = NULL;

    g_object_get (raw, "settings-schema", &schema, NULL);

    g_auto (GStrv) keys = g_settings_schema_list_keys (schema);

    for (GStrv key = keys; *key; ++key)
        g_settings_reset (raw, *key);

    return g_paste_settings_new ();
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
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    GPasteItem *rich = g_paste_text_item_new ("rich text");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                  g_bytes_new_static ("<b>rich text</b>", 16)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));

    g_paste_history_add (history, rich);
    if (!at_head)
        g_paste_history_add (history, g_paste_text_item_new ("later history item"));

    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
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
text_ready (TestClipboard         *self G_GNUC_UNUSED,
            GPasteSettings        *settings G_GNUC_UNUSED,
            GPasteClipboardUpdate *pending,
            const gchar           *text)
{
    if (!g_paste_clipboard_update_is_expired (pending))
    {
        pending->produced = TRUE;
        g_set_str (&pending->text, text);
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
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);

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
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)), ==, "copied text");

    /* A completed copy, unlike an abandoned one, still suppresses duplicates. */
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *duplicate = clipboard->pending;

    text_ready (clipboard, settings, duplicate, "copied text");
    g_paste_clipboard_update_maybe_done (duplicate);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
}

static void
test_superseded_empty_cache (gconstpointer user_data)
{
    gboolean bootstrap = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);

    g_paste_history_add (history, g_paste_text_item_new ("saved history"));

    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);

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
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)), ==, "new copy");
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 2);
}

static void
test_strip_during_read (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    GPasteItem *rich = g_paste_text_item_new ("rich text");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                  g_bytes_new_static ("<b>rich text</b>", 16)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));
    g_paste_history_add (history, rich);
    if (variant == 2 || variant == 4)
        g_paste_history_add (history, g_paste_text_item_new ("later item"));

    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);

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
    /* The read the strip was queued during is overtaken by another. It publishes
     * nothing itself -- a superseded read says nothing about the selection -- and
     * the request is owed to the read overtaking it rather than spent by the
     * change: neither the change nor that read could say what the selection
     * holds, and one of the two notifications the GDK backend raises per external
     * copy is exactly this shape (notify ()). So the strip goes out below, as it
     * does for a read nothing overtook. */
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
    gboolean refreshed = variant != 0 && variant != 4 && variant != 6 && variant != 7;

    if (variant == 5)
        g_paste_settings_set_trim_items (settings, TRUE);

    /* The read identifies a password with the stripped text's value. */
    if (variant == 6)
    {
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT),
                                               g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    }

    /* The selection offers the hint and never serves it: what it holds may be a
     * secret, and the plain text would go out with no hint (finish_refresh ()). */
    if (variant == 7)
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);

    text_ready (clipboard, settings, pending, variant == 5 ? " rich text " : text);
    pending->mimes.special_mime[G_PASTE_SPECIAL_MIME_TEXT_HTML] = g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML, g_bytes_new_static ("<b>rich text</b>", 16));
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)), ==, text);
    g_assert_cmpuint (clipboard->publications, ==, variant == 5 ? 2 : (refreshed ? 1 : 0));
    if (refreshed)
    {
        g_assert_nonnull (clipboard->published);
        g_assert_null (g_paste_item_get_special_values (clipboard->published));
        g_assert_cmpstr (g_paste_item_get_uuid (clipboard->published), ==, uuid);
        g_assert_null (g_paste_item_get_special_values (g_paste_history_get_by_uuid (history, uuid)));
    }
    if (variant == 6)
        g_assert_true (G_PASTE_IS_PASSWORD_ITEM (g_paste_history_get (history, 0)));
}

/* A publication the provider refuses puts nothing on the selection, so the strip
 * that selection was owed is still owed: what a write leaves behind goes with
 * the write and not with the asking for one (note_published ()). */
static void
test_strip_survives_refused_publication (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    GPasteItem *rich = g_paste_text_item_new ("rich text");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>rich text</b>", 16)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));
    g_paste_history_add (history, rich);

    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);

    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&clipboard->content, "rich text");
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

    /* A selection the provider turns down: the strip queued above is not spent
     * by a write that never happened. */
    g_autoptr (GPasteItem) refused = g_paste_text_item_new ("refused");

    clipboard->refuse_value = "refused";
    g_assert_false (g_paste_clipboards_manager_select (manager, refused));
    g_assert_cmpuint (clipboard->publications, ==, 0);
    clipboard->refuse_value = NULL;

    text_ready (clipboard, settings, pending, "rich text");
    pending->mimes.special_mime[G_PASTE_SPECIAL_MIME_TEXT_HTML] = g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                                           g_bytes_new_static ("<b>rich text</b>", 16));
    g_paste_clipboard_update_maybe_done (pending);

    g_assert_cmpuint (clipboard->publications, ==, 1);
    g_assert_nonnull (clipboard->published);
    g_assert_cmpstr (g_paste_item_get_uuid (clipboard->published), ==, uuid);
    g_assert_null (g_paste_item_get_special_values (clipboard->published));
}

/* A strip the secrecy check refused is kept, not spent. The history is stripped
 * by the method before the read concludes, so a request dropped there would
 * leave the history plain and the selection serving the rich flavours for good;
 * the next read that does identify the selection publishes it instead. */
static void
test_strip_retried_after_unknown_hint (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    GPasteItem *rich = g_paste_text_item_new ("rich text");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>rich text</b>", 16)));

    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));

    g_paste_history_add (history, rich);

    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboard_content_set_text (&clipboard->content, "rich text");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    GPasteClipboardUpdate *pending = clipboard->pending;
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .clipboards_manager = manager };
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
    g_assert_no_error (error);

    guint publications = clipboard->publications;

    /* The selection offers the hint and never serves it, so nothing may be
     * published over it -- but the history is already stripped. */
    if (variant == 2)
        clipboard->reject_next = TRUE;
    else
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);
    text_ready (clipboard, settings, pending, "rich text");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpuint (clipboard->publications, ==, publications);
    g_assert_null (g_paste_item_get_special_values (g_paste_history_get_by_uuid (history, uuid)));

    if (variant == 3)
    {
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        pending = clipboard->pending;
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);
        g_paste_clipboard_update_maybe_done (pending);
        g_paste_clipboard_update_maybe_done (pending);
    }

    if (variant == 1 || variant == 3)
    {
        gpointer plain = g_paste_history_get_by_uuid (history, uuid);

        g_object_add_weak_pointer (G_OBJECT (plain), &plain);
        g_paste_history_empty (history);
        /* The refused slot holds its snapshot weakly, as the queued ones do: the
         * entry the user just deleted goes with the history, where a slot kept
         * for a read that may never come would hold its cleartext for the
         * session. */
        g_assert_null (plain);
        /* Which is why disposal is asserted on by running at all: the item's
         * lifetime can no longer witness the release, and what is left to get
         * wrong is releasing a slot that names an entry already gone. */
        g_object_run_dispose (G_OBJECT (manager));
        return;
    }

    /* A read that does identify the selection publishes the strip after all. */
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    pending = clipboard->pending;
    text_ready (clipboard, settings, pending, "rich text");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpuint (clipboard->publications, ==, publications + 1);
    g_assert_nonnull (clipboard->published);
    g_assert_cmpstr (g_paste_item_get_uuid (clipboard->published), ==, uuid);
    g_assert_null (g_paste_item_get_special_values (clipboard->published));
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
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    GPasteClipboardProvider *provider = G_PASTE_CLIPBOARD_PROVIDER (clipboard);

    g_paste_clipboard_content_set_text (&clipboard->content, "previous copy");
    GPasteClipboardUpdate *first = g_paste_clipboard_update_new (provider, settings, kind, &clipboard->pending, &clipboard->content, NULL, NULL);

    fill_typed_read (first);
    g_assert_cmpint (clipboard->content.kind, ==, CLIPBOARD_CONTENT_TEXT);
    GPasteClipboardUpdate *second = g_paste_clipboard_update_new (provider, settings, kind, &clipboard->pending, &clipboard->content, NULL, NULL);

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

static void
test_password_expiry_during_read (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "same secret", 37);

    g_paste_history_add (history, g_paste_text_item_new ("replacement"));
    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_track_changes (settings, FALSE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    g_paste_clipboards_manager_select (manager, password);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *pending = clipboard->pending;
    guint publications = clipboard->publications;

    g_paste_clipboards_manager_expire_password (manager);
    g_assert_cmpuint (clipboard->publications, ==, publications);

    if (variant == 2)
    {
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        GPasteClipboardUpdate *superseded = pending;

        pending = clipboard->pending;
        text_ready (clipboard, settings, superseded, "same secret");
        g_paste_clipboard_update_maybe_done (superseded);
        g_assert_cmpuint (clipboard->publications, ==, publications);
    }

    text_ready (clipboard, settings, pending, (variant == 1) ? "new copy" : "same secret");
    g_paste_clipboard_update_maybe_done (pending);

    g_assert_cmpstr (clipboard->content.str, ==, (variant == 1) ? "new copy" : "replacement");
    g_assert_cmpuint (clipboard->publications, ==, publications + (variant != 1));
}

typedef struct
{
    GPasteSettings          *settings;
    GPasteHistory           *history;
    TestClipboard           *clipboard;
    GPasteClipboardsManager *manager;
    GPasteItem              *password;
} PasswordFixture;

static void
password_fixture_free (PasswordFixture *fixture)
{
    g_assert_null (fixture->clipboard->pending);
    g_clear_object (&fixture->manager);
    g_clear_object (&fixture->clipboard);
    g_clear_object (&fixture->password);
    g_clear_object (&fixture->history);
    g_clear_object (&fixture->settings);
    g_free (fixture);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PasswordFixture, password_fixture_free)

static PasswordFixture *
password_fixture_new (guint timeout)
{
    PasswordFixture *fixture = g_new0 (PasswordFixture, 1);

    fixture->settings = make_settings ();
    fixture->history = make_history (fixture->settings);
    fixture->clipboard = make_clipboard (fixture->settings);
    fixture->manager = g_paste_clipboards_manager_new (fixture->history, fixture->settings);
    fixture->password = g_paste_password_item_new (NULL, "same secret", timeout);
    g_paste_history_add (fixture->history, g_paste_text_item_new ("replacement"));
    fixture->clipboard->is_clipboard = TRUE;
    g_paste_settings_set_track_changes (fixture->settings, FALSE);
    g_paste_settings_set_synchronize_clipboards (fixture->settings, FALSE);
    g_paste_clipboards_manager_add_clipboard (fixture->manager, G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    g_paste_clipboards_manager_activate (fixture->manager);
    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
    fixture->clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    return fixture;
}

/* These cases exercise GLib's real timeout dispatch and monotonic deadlines;
 * advancing only a simulated clock would not test their interaction. Keep
 * this drain-after-deadline helper local to that timing contract.
 *
 * Run the main loop for at least @milliseconds, then dispatch whatever is due.
 *
 * The countdowns under test are real GLib timeouts, so what a test can rely on is
 * monotonic time, not how quickly it gets scheduled: the last iteration comes
 * *after* the deadline has passed, which makes every source due by then -- a
 * 1 s countdown armed before a pump_for (1100) -- dispatched however late the
 * process was woken. Asserting that one has fired is therefore not a race.
 * Asserting that one has *not* fired yet is, and goes through still_before (). */
static void
pump_for (guint milliseconds)
{
    gint64 deadline = g_get_monotonic_time () + (gint64) milliseconds * 1000;

    do
    {
        while (g_main_context_iteration (NULL, FALSE));
        g_usleep (1000);
    } while (g_get_monotonic_time () < deadline);

    while (g_main_context_iteration (NULL, FALSE));
}

/* Whether @milliseconds after @start is still ahead. A "has not fired yet"
 * assertion only means something while it is: a test process stalled past the
 * deadline would otherwise read a countdown firing on time as one firing early,
 * so such an assertion is skipped rather than failed once the deadline is
 * gone. */
static gboolean
still_before (gint64 start,
              guint  milliseconds)
{
    return g_get_monotonic_time () < start + (gint64) milliseconds * 1000;
}

static void
password_read_ready (PasswordFixture *fixture,
                     const gchar     *text)
{
    GPasteClipboardUpdate *pending = fixture->clipboard->pending;

    text_ready (fixture->clipboard, fixture->settings, pending, text);
    g_paste_clipboard_update_maybe_done (pending);
}

typedef enum
{
    EDIT_ONLY,
    EDIT_OTHER_CANDIDATE,
    EDIT_REPLACE_READ,
    EDIT_RENAME,
    EDIT_EXTEND_OVERDUE,
    EDIT_LATEST_CANDIDATE,
} TimeoutEditAction;

typedef struct
{
    const gchar      *name;
    guint             initial_timeout;
    guint             timeout;
    guint             wait_before;
    guint             wait_after;
    const gchar      *copied;
    const gchar      *expected;
    TimeoutEditAction action;
} TimeoutEditCase;

static const TimeoutEditCase timeout_edits[] = {
    { "enable",           0, 1,    0, 1100, "same secret", "replacement", EDIT_ONLY },
    { "shorten",         37, 1,    0, 1100, "same secret", "replacement", EDIT_ONLY },
    { "disable",          1, 0,    0, 1100, "same secret", "same secret", EDIT_ONLY },
    { "unrelated",       37, 1,    0, 1100, "new copy",    "new copy",    EDIT_ONLY },
    { "multiple",        37, 1,    0, 1100, "same secret", "replacement", EDIT_OTHER_CANDIDATE },
    { "superseded",      37, 1,    0, 1100, "same secret", "replacement", EDIT_REPLACE_READ },
    { "elapsed",         37, 1, 1100,    0, "same secret", "replacement", EDIT_ONLY },
    { "rename",          37, 1,  600,  500, "same secret", "replacement", EDIT_RENAME },
    { "disable_overdue",  1, 0, 1100,    0, "same secret", "same secret", EDIT_ONLY },
    { "extend_overdue",   1, 2, 1100, 1100, "same secret", "replacement", EDIT_EXTEND_OVERDUE },
    { "latest_edit",      1, 1,    0, 2200, "same secret", "replacement", EDIT_LATEST_CANDIDATE },
};

static void
test_timeout_edit_during_read (gconstpointer user_data)
{
    const TimeoutEditCase *test = user_data;
    g_autoptr (PasswordFixture) fixture = password_fixture_new (test->initial_timeout);
    gint64 edited = g_get_monotonic_time ();

    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), test->timeout);
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);

    if (test->action == EDIT_LATEST_CANDIDATE)
    {
        g_autoptr (GPasteItem) other = g_paste_password_item_new ("other name", "same secret", 0);

        g_paste_clipboards_manager_rearm_password (fixture->manager, other);
        g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), 2);
        g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    }
    if (test->action == EDIT_OTHER_CANDIDATE)
    {
        g_autoptr (GPasteItem) other = g_paste_password_item_new (NULL, "different secret", 37);

        g_paste_clipboards_manager_rearm_password (fixture->manager, other);
    }
    if (test->action == EDIT_REPLACE_READ)
    {
        GPasteClipboardUpdate *superseded = fixture->clipboard->pending;

        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
        text_ready (fixture->clipboard, fixture->settings, superseded, "different secret");
        g_paste_clipboard_update_maybe_done (superseded);
    }
    if (test->wait_before)
        pump_for (test->wait_before);
    if (test->action == EDIT_RENAME)
        g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);

    password_read_ready (fixture, test->copied);
    if (test->action == EDIT_EXTEND_OVERDUE && still_before (edited, 2000))
        g_assert_cmpstr (fixture->clipboard->content.str, ==, "same secret");
    if (test->wait_after)
        pump_for (test->wait_after);

    g_assert_cmpstr (fixture->clipboard->content.str, ==, test->expected);
}

static void
test_reselect_overdue_password (gconstpointer user_data)
{
    gboolean second_selection = GPOINTER_TO_INT (user_data);
    g_autoptr (PasswordFixture) fixture = password_fixture_new (1);
    g_autoptr (TestClipboard) primary = make_clipboard (fixture->settings);

    if (second_selection)
    {
        g_paste_clipboard_content_set_text (&primary->content, "same secret");
        g_paste_clipboards_manager_add_clipboard (fixture->manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    }

    GPasteClipboardUpdate *pending = fixture->clipboard->pending;

    pump_for (1100);
    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
    text_ready (fixture->clipboard, fixture->settings, pending, "same secret");
    g_paste_clipboard_update_maybe_done (pending);

    g_assert_cmpstr (fixture->clipboard->content.str, ==, "same secret");
    if (second_selection)
        g_assert_cmpstr (primary->content.str, ==, "same secret");
    pump_for (1100);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
    if (second_selection)
        g_assert_cmpstr (primary->content.str, ==, "replacement");
}

/* The expiry task carries no source object -- the manager's own list holds it --
 * so the manager to finish against travels here rather than through @source. */
typedef struct
{
    GPasteClipboardsManager *manager;
    guint                    completed;
} ExpiryCounter;

static void
on_passwords_expired (GObject      *source G_GNUC_UNUSED,
                      GAsyncResult *result,
                      gpointer      user_data)
{
    ExpiryCounter *counter = user_data;

    g_autoptr (GError) error = NULL;

    g_assert_true (g_paste_clipboards_manager_expire_password_finish (counter->manager, result, &error));
    g_assert_no_error (error);
    ++counter->completed;
}

static void
test_forced_expiry (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    ExpiryCounter counter = { fixture->manager, 0 };

    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_passwords_expired, &counter);
    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_passwords_expired, &counter);
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 0);

    if (variant == 2 || variant == 3)
    {
        g_object_run_dispose (G_OBJECT (fixture->manager));
        g_object_run_dispose (G_OBJECT (fixture->manager));
    }
    if (variant == 4)
    {
        GPasteClipboardUpdate *pending = fixture->clipboard->pending;

        select_text (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard), "new copy");
        text_ready (fixture->clipboard, fixture->settings, pending, "same secret");
        g_paste_clipboard_update_maybe_done (pending);
    }
    else
        password_read_ready (fixture, (variant == 1 || variant == 3) ? "new copy" : "same secret");

    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 2);
    g_assert_cmpstr (fixture->clipboard->content.str, ==,
                     variant == 2 ? "" : (variant == 0 ? "replacement" : "new copy"));

    gpointer manager = fixture->manager;

    g_object_add_weak_pointer (G_OBJECT (manager), &manager);
    g_clear_pointer (&fixture, password_fixture_free);
    pump_for (10);
    g_assert_null (manager);
}

static void
test_forced_expiry_waits_for_both_selections (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    g_autoptr (TestClipboard) primary = make_clipboard (fixture->settings);
    ExpiryCounter counter = { fixture->manager, 0 };

    password_read_ready (fixture, "same secret");
    g_paste_clipboard_content_set_text (&primary->content, "same secret");
    g_paste_clipboards_manager_add_clipboard (fixture->manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (fixture->manager);
    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
    primary->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_passwords_expired, &counter);

    password_read_ready (fixture, "same secret");
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 0);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");

    GPasteClipboardUpdate *pending = primary->pending;

    text_ready (primary, fixture->settings, pending, "new copy");
    g_paste_clipboard_update_maybe_done (pending);
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 1);
    g_assert_cmpstr (primary->content.str, ==, "new copy");
}

static void
test_publish_pending_deadline (gconstpointer user_data)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);

    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), 1);
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    pump_for (1100);

    GPasteClipboardUpdate *pending = fixture->clipboard->pending;

    g_autoptr (GPasteItem) twin = g_paste_password_item_new (NULL, "same secret", 37);

    g_paste_clipboards_manager_select (fixture->manager, GPOINTER_TO_INT (user_data) ? twin : fixture->password);
    text_ready (fixture->clipboard, fixture->settings, pending, "same secret");
    g_paste_clipboard_update_maybe_done (pending);

    g_assert_cmpstr (fixture->clipboard->content.str, ==, "same secret");
    pump_for (1100);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

static void
test_reenable_pending_timeout (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (1);

    pump_for (1100);
    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), 0);
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);

    gint64 reenabled = g_get_monotonic_time ();

    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), 1);
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    password_read_ready (fixture, "same secret");

    /* Re-enabled with a new 1 s countdown, not the one that ran out before. */
    if (still_before (reenabled, 1000))
        g_assert_cmpstr (fixture->clipboard->content.str, ==, "same secret");
    pump_for (1100);

    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

/* The record converges on the same-value twin a later publication carries, so a
 * rename of the original during a read changes no duration and keeps the
 * countdown running rather than restarting it. */
static void
test_rename_twin_during_read (void)
{
    gint64 armed = g_get_monotonic_time ();
    g_autoptr (PasswordFixture) fixture = password_fixture_new (1);
    g_autoptr (GPasteItem) twin = g_paste_password_item_new (NULL, "same secret", 1);

    password_read_ready (fixture, "same secret");
    g_paste_clipboards_manager_select (fixture->manager, twin);
    pump_for (600);

    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    g_paste_password_item_set_name (G_PASTE_PASSWORD_ITEM (fixture->password), "renamed");
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    password_read_ready (fixture, "same secret");

    pump_for ((guint) (MAX (armed + 1100 * 1000 - g_get_monotonic_time (), 0) / 1000));
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

/* A forced expiry brings a running countdown forward. What resolves while it
 * waits on a read can leave none: a timeout-0 password selected over it, or an
 * edit disabling the countdown. That password stays, and the wait still ends. */
static void
test_forced_expiry_without_countdown (gconstpointer user_data)
{
    gboolean edit = GPOINTER_TO_INT (user_data);
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    ExpiryCounter counter = { fixture->manager, 0 };
    g_autoptr (GPasteItem) kept = g_paste_password_item_new (NULL, "kept secret", 0);

    if (edit)
    {
        g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), 0);
        g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    }
    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_passwords_expired, &counter);
    g_assert_cmpuint (counter.completed, ==, 0);

    if (edit)
        password_read_ready (fixture, "same secret");
    else
    {
        GPasteClipboardUpdate *pending = fixture->clipboard->pending;

        g_paste_clipboards_manager_select (fixture->manager, kept);
        /* Superseded reads still own their replies and must count them out. */
        text_ready (fixture->clipboard, fixture->settings, pending, "same secret");
        g_paste_clipboard_update_maybe_done (pending);
    }
    pump_for (10);

    g_assert_cmpuint (counter.completed, ==, 1);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, (edit) ? "same secret" : "kept secret");
}

static void
test_trimmed_duplicate (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    GPasteClipboardContent content = { 0 };
    g_autofree gchar *value = NULL;

    g_paste_settings_set_trim_items (settings, TRUE);
    g_paste_clipboard_content_set_text (&content, " text ");
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, TRUE, " text ", FALSE, &value), ==, G_PASTE_CLIPBOARD_TEXT_RESELECT);
    g_assert_cmpstr (value, ==, "text");
    g_clear_pointer (&value, g_free);

    g_paste_clipboard_content_set_text (&content, "text");
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, TRUE, " text ", FALSE, &value), ==, G_PASTE_CLIPBOARD_TEXT_RESELECT);
    g_assert_cmpstr (value, ==, "text");
    g_clear_pointer (&value, g_free);

    /* The primary selection is never re-owned trimmed, so its padded text read
     * again is still the duplicate of the trimmed value the cache holds. */
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, " text ", FALSE, &value), ==, G_PASTE_CLIPBOARD_TEXT_UNCHANGED);
    g_assert_null (value);

    g_paste_clipboard_content_set_text (&content, " text ");
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, " text ", FALSE, &value), ==, G_PASTE_CLIPBOARD_TEXT_UNCHANGED);
    g_assert_null (value);

    /* New text comes back as a value only when trimming changed it: the caller
     * already owns the text, which can be megabytes, and keeps it otherwise. */
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, "new text", FALSE, &value), ==, G_PASTE_CLIPBOARD_TEXT_SET);
    g_assert_null (value);
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, " new text ", FALSE, &value), ==, G_PASTE_CLIPBOARD_TEXT_SET);
    g_assert_cmpstr (value, ==, "new text");
    g_paste_clipboard_content_clear (&content);
}

/* A text its owner marked as a secret is held to neither size setting: they are
 * the user's filter for noise and blobs, and a secret turned down there would
 * leave the cleartext on the selection with no item to record it and no
 * countdown to take it off -- however large it is, which is accepted knowingly
 * (see g_paste_clipboard_content_classify_text ()). An empty one is refused. */
static void
test_sensitive_size_policy (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    GPasteClipboardContent content = { 0 };
    g_autofree gchar *value = NULL;

    g_paste_settings_set_min_text_item_size (settings, 4);
    g_paste_settings_set_max_text_item_size (settings, 20);

    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, "pw", FALSE, &value), ==, G_PASTE_CLIPBOARD_TEXT_DROP);
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, "pw", TRUE, &value), ==, G_PASTE_CLIPBOARD_TEXT_SET);
    g_assert_null (value);

    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, "a passphrase longer than the limit", FALSE, &value), ==, G_PASTE_CLIPBOARD_TEXT_DROP);
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, "a passphrase longer than the limit", TRUE, &value), ==, G_PASTE_CLIPBOARD_TEXT_SET);
    g_assert_null (value);

    /* However large. */
    g_autofree gchar *blob = g_strnfill (1024 * 1024, 'x');

    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, blob, TRUE, &value), ==, G_PASTE_CLIPBOARD_TEXT_SET);
    g_assert_null (value);

    /* Nothing is still nothing, marked or not. */
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, "", TRUE, &value), ==, G_PASTE_CLIPBOARD_TEXT_DROP);
    g_assert_null (value);

    g_paste_clipboard_content_clear (&content);
}

static void
test_unidentified_selection (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    gboolean expiry = variant >= 6;
    guint outcome = variant % 6;
    g_autoptr (PasswordFixture) fixture = password_fixture_new (60);
    TestClipboard *clipboard = fixture->clipboard;
    GPasteClipboardUpdate *pending = clipboard->pending;
    const gchar *text = "x";

    if (expiry)
        g_paste_clipboards_manager_expire_password (fixture->manager);
    else
    {
        GPasteItem *rich = g_paste_text_item_new ("same secret");

        g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                     g_bytes_new_static ("<b>same secret</b>", 18)));
        g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));
        g_paste_history_add (fixture->history, rich);
        const GPasteDaemonMethods methods = {
            .history = fixture->history,
            .settings = fixture->settings,
            .clipboards_manager = fixture->manager,
        };
        g_autoptr (GError) error = NULL;

        g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
        g_assert_no_error (error);
    }

    guint publications = clipboard->publications;

    if (outcome == 0 || outcome == 4)
        g_paste_settings_set_min_text_item_size (fixture->settings, 20);
    else if (outcome == 1)
    {
        g_paste_settings_set_max_text_item_size (fixture->settings, 1);
        text = "unrelated copy";
    }

    if (outcome == 2)
    {
        /* Both reads fail without identifying the selection. */
        g_paste_clipboard_update_maybe_done (pending);
        g_paste_clipboard_update_maybe_done (pending);
    }
    else if (outcome == 3)
    {
        pending->guard.last_read -= (gint64) pending->guard.timeout * G_USEC_PER_SEC;
        g_source_set_ready_time (g_main_context_find_source_by_id (NULL, pending->guard.timeout_id), 0);
        gint64 deadline = g_get_monotonic_time () + G_USEC_PER_SEC;

        while (!pending->concluded && g_get_monotonic_time () < deadline)
        {
            g_main_context_iteration (NULL, FALSE);
            g_usleep (1000);
        }
        g_assert_true (pending->concluded);
        g_paste_clipboard_update_maybe_done (pending);
        g_paste_clipboard_update_maybe_done (pending);
    }
    else
        password_read_ready (fixture, outcome >= 4 ? "same secret" : text);

    if (outcome >= 4)
    {
        /* An exact duplicate remains identifiable even outside the size
         * policy. */
        g_assert_cmpuint (clipboard->publications, ==, publications + 1);
        g_assert_cmpstr (clipboard->content.str, ==, expiry ? "replacement" : "same secret");
    }
    else
    {
        g_assert_cmpuint (clipboard->publications, ==, publications);
        g_assert_null (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)));
        g_assert_false (g_paste_clipboard_provider_is_empty (G_PASTE_CLIPBOARD_PROVIDER (clipboard)));
    }
}

/* A selection emptied under a password -- a password manager clearing what it
 * copied -- is left empty: re-owning it with the history's head would put that
 * password straight back, for good under a password-timeout of 0. */
static void
test_emptied_under_password (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    g_paste_history_add (history, g_paste_text_item_new ("older copy"));
    g_paste_history_add (history, g_paste_password_item_new (NULL, "copied secret", 0));
    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_track_changes (settings, FALSE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    g_assert_cmpuint (clipboard->publications, ==, 0);
    g_assert_true (is_empty (G_PASTE_CLIPBOARD_PROVIDER (clipboard)));
}

static void
capture_expiry (GObject      *source G_GNUC_UNUSED,
                GAsyncResult *result,
                gpointer      user_data)
{
    GAsyncResult **captured = user_data;

    *captured = g_object_ref (result);
}

typedef enum
{
    /* The owner releases the selection. */
    FOLLOW_RELEASE_RELEASED,
    /* The owner writes an empty text that carries the hint, as a password
     * manager clearing its copy does. */
    FOLLOW_RELEASE_EMPTY_SECRET,
    /* The owner writes an empty text with no hint on it. */
    FOLLOW_RELEASE_EMPTY_TEXT,
    /* The owner puts content of another kind there, none of it text. */
    FOLLOW_RELEASE_REPLACED,
    /* The owner puts a text there, which the synchronization carries over. */
    FOLLOW_RELEASE_NEW_TEXT,
    /* The password on both selections is one the user selected, not a copy the
     * synchronization made; the owner releases the selection. */
    FOLLOW_RELEASE_USER_SELECTED,
    /* The user selects the password the synchronization had already copied;
     * the owner releases the selection. */
    FOLLOW_RELEASE_SELECTED_AFTER_SYNC,
    /* The synchronized copy's timeout is edited; the owner releases the
     * selection. */
    FOLLOW_RELEASE_EDITED_TIMEOUT,
    /* The synchronization is switched off, and the owner puts another password
     * there. */
    FOLLOW_RELEASE_NEW_PASSWORD_UNSYNCED,
    /* The owner's clear is read with nothing identified and no answer about the
     * hint, and only a later identifying read finds the selection emptied. */
    FOLLOW_RELEASE_FOUND_BY_IDENTIFYING,
    /* The owner copies that very password onto the other selection itself, then
     * puts content that is not text on the first one. */
    FOLLOW_RELEASE_OWNER_RECOPIED,
    /* The same, with the value copied as plain text: no password is read. */
    FOLLOW_RELEASE_OWNER_SAME_VALUE,
    /* A named password of the same value has its timeout edited, which moves the
     * copy's record onto that other object; the owner releases the selection. */
    FOLLOW_RELEASE_TWIN_EDITED,
} FollowRelease;

/* A password leaving its selection, with nothing the synchronization carries
 * over in its place, takes the copy the synchronization made of it off the other
 * selection too: that copy would otherwise outlive the one it was made from, for
 * good under a password-timeout of 0. A release, an empty text with or without
 * the hint, content that is not text, and another password with the
 * synchronization off all leave nothing to carry over -- whether the read of the
 * change finds that, or only a later identifying read does; a text is carried
 * over and replaces the copy itself. The emptied selection stays as its owner
 * left it, and the history as it was.
 *
 * A password the user selected is theirs on both selections, even over a copy
 * the synchronization had made, and so is what an owner put there itself, that
 * very value included; an edited timeout, its own or a same-value twin's, leaves
 * the copy the synchronization's. See
 * retire_synced_copies (). */
static void
test_follow_release (gconstpointer user_data)
{
    FollowRelease variant = GPOINTER_TO_UINT (user_data);
    gboolean user_selected = (variant == FOLLOW_RELEASE_USER_SELECTED);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    GBytes *hint = g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    g_paste_settings_set_synchronize_clipboards (settings, !user_selected);
    g_paste_settings_set_password_timeout (settings, 0);
    g_paste_settings_set_min_text_item_size (settings, 1);
    g_paste_history_add (history, g_paste_text_item_new ("older copy"));

    g_autoptr (GPasteItem) twin = g_paste_password_item_new ("named", "copied secret", 0);

    if (variant == FOLLOW_RELEASE_TWIN_EDITED)
        g_paste_history_add (history, g_object_ref (twin));
    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&primary->content, "older copy");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);

    GPasteClipboardUpdate *pending;

    if (user_selected)
    {
        g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "copied secret", 0);

        g_paste_history_add (history, g_object_ref (password));
        g_paste_clipboards_manager_select (manager, password);
    }
    else
    {
        clipboard->defer_reads = TRUE;
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        pending = clipboard->pending;
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), hint);
        text_ready (clipboard, settings, pending, "copied secret");
        g_paste_clipboard_update_maybe_done (pending);
    }

    g_assert_cmpstr (primary->content.str, ==, "copied secret");
    g_assert_true (G_PASTE_IS_PASSWORD_ITEM (primary->published));

    g_autoptr (GPasteItem) password = g_object_ref (primary->published);

    if (variant == FOLLOW_RELEASE_SELECTED_AFTER_SYNC)
        g_paste_clipboards_manager_select (manager, password);
    else if (variant == FOLLOW_RELEASE_EDITED_TIMEOUT)
    {
        g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (password), 60);
        g_paste_clipboards_manager_rearm_password (manager, password);
    }
    else if (variant == FOLLOW_RELEASE_NEW_PASSWORD_UNSYNCED)
        g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    else if (variant == FOLLOW_RELEASE_OWNER_RECOPIED || variant == FOLLOW_RELEASE_OWNER_SAME_VALUE)
    {
        primary->defer_reads = TRUE;
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));
        pending = primary->pending;
        if (variant == FOLLOW_RELEASE_OWNER_RECOPIED)
            g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), hint);
        text_ready (primary, settings, pending, "copied secret");
        g_paste_clipboard_update_maybe_done (pending);
        primary->defer_reads = FALSE;
    }
    else if (variant == FOLLOW_RELEASE_TWIN_EDITED)
    {
        g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (twin), 60);
        g_paste_clipboards_manager_rearm_password (manager, twin);
    }

    guint primary_publications = primary->publications;
    g_autoptr (GAsyncResult) expiry = NULL;

    switch (variant)
    {
    case FOLLOW_RELEASE_RELEASED:
    case FOLLOW_RELEASE_USER_SELECTED:
    case FOLLOW_RELEASE_SELECTED_AFTER_SYNC:
    case FOLLOW_RELEASE_EDITED_TIMEOUT:
    case FOLLOW_RELEASE_TWIN_EDITED:
        clipboard->defer_reads = FALSE;
        g_paste_clipboard_content_clear (&clipboard->content);
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        g_assert_true (is_empty (G_PASTE_CLIPBOARD_PROVIDER (clipboard)));
        break;
    case FOLLOW_RELEASE_EMPTY_SECRET:
    case FOLLOW_RELEASE_EMPTY_TEXT:
    case FOLLOW_RELEASE_NEW_TEXT:
    case FOLLOW_RELEASE_NEW_PASSWORD_UNSYNCED:
        clipboard->defer_reads = TRUE;
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        pending = clipboard->pending;
        if (variant == FOLLOW_RELEASE_EMPTY_SECRET || variant == FOLLOW_RELEASE_NEW_PASSWORD_UNSYNCED)
            g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), hint);
        text_ready (clipboard, settings, pending,
                    (variant == FOLLOW_RELEASE_NEW_TEXT) ? "new text" : (variant == FOLLOW_RELEASE_NEW_PASSWORD_UNSYNCED) ? "another secret" : "");
        g_paste_clipboard_update_maybe_done (pending);
        if (variant != FOLLOW_RELEASE_NEW_TEXT)
            g_assert_cmpstr (clipboard->content.str, ==, (variant == FOLLOW_RELEASE_NEW_PASSWORD_UNSYNCED) ? "another secret" : "");
        break;
    case FOLLOW_RELEASE_FOUND_BY_IDENTIFYING:
        /* The clear is read with the hint offered and not served, and both text
         * replies fail: the record stands unconfirmed, the copy with it. */
        clipboard->defer_reads = TRUE;
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        pending = clipboard->pending;
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);
        g_paste_clipboard_update_maybe_done (pending);
        g_paste_clipboard_update_maybe_done (pending);
        g_assert_cmpstr (primary->content.str, ==, "copied secret");

        /* A wait for expiry asks for the identifying read, which finds it. */
        g_paste_clipboards_manager_expire_password_async (manager, capture_expiry, &expiry);
        pending = clipboard->pending;
        g_assert_nonnull (pending);
        text_ready (clipboard, settings, pending, "");
        g_paste_clipboard_update_maybe_done (pending);
        pump_for (10);
        g_assert_nonnull (expiry);
        g_assert_true (g_paste_clipboards_manager_expire_password_finish (manager, expiry, NULL));
        break;
    case FOLLOW_RELEASE_OWNER_RECOPIED:
    case FOLLOW_RELEASE_OWNER_SAME_VALUE:
    case FOLLOW_RELEASE_REPLACED:
        /* What a read leaves for content it holds no text or image of. */
        clipboard->defer_reads = FALSE;
        g_paste_clipboard_content_set_ignored (&clipboard->content);
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        break;
    }

    switch (variant)
    {
    case FOLLOW_RELEASE_USER_SELECTED:
    case FOLLOW_RELEASE_SELECTED_AFTER_SYNC:
    case FOLLOW_RELEASE_OWNER_RECOPIED:
    case FOLLOW_RELEASE_OWNER_SAME_VALUE:
        g_assert_cmpstr (primary->content.str, ==, "copied secret");
        g_assert_cmpuint (primary->publications, ==, primary_publications);
        break;
    case FOLLOW_RELEASE_NEW_TEXT:
        g_assert_cmpstr (primary->content.str, ==, "new text");
        break;
    default:
        g_assert_cmpstr (primary->content.str, ==, "older copy");
        break;
    }

    /* The history is left as it was, whichever of the two happened -- but for
     * the text copied, which is a copy like any other. */
    if (variant != FOLLOW_RELEASE_NEW_TEXT)
        g_assert_true (G_PASTE_IS_PASSWORD_ITEM (g_paste_history_get (history, 0)));
}

/* A read that identifies nothing and gets no answer about the hint must not be
 * taken for the selection having moved on from the password recorded for it:
 * the record stays, a sync out of that selection is refused rather than copying
 * the cleartext as bare text, and an overdue countdown waits for the read that
 * does identify the selection instead of being dropped.
 *
 * A hint that *is* served with no text beside it is the same case (the
 * secret_without_text variants): it says the selection carries a secret, not
 * which one, so it cannot tell the recorded password from another, and taking
 * it for a move would drop the record and its countdown over a cleartext that
 * may well still be the one they were for (see confirm_password ()). */
static void
test_unconfirmed_password (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    gboolean expiry = variant & 1;
    gboolean served = variant & 2;
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "same secret", 37);

    g_paste_history_add (history, g_paste_text_item_new ("replacement"));
    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_track_changes (settings, FALSE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboard_content_set_text (&primary->content, "destination");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    g_paste_clipboard_provider_select_item (G_PASTE_CLIPBOARD_PROVIDER (clipboard), password);
    g_paste_clipboards_manager_rearm_password (manager, password);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    GPasteClipboardUpdate *pending = clipboard->pending;
    GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);
    guint publications = clipboard->publications;

    /* The hint is offered and not served (or served, for the secret_without_text
     * variants), and both text replies fail. */
    g_autoptr (GBytes) secret = g_bytes_new_static ("secret", 6);

    g_paste_clipboard_update_on_mime_read (hint, (served) ? secret : NULL);
    g_paste_clipboard_update_maybe_done (pending);
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_null (clipboard->pending);
    g_assert_null (get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)));

    g_paste_clipboards_manager_sync_from_to (manager, TRUE);
    g_assert_cmpuint (clipboard->sync_reads, ==, 0);
    g_assert_cmpuint (primary->publications, ==, 0);
    g_assert_cmpstr (primary->content.str, ==, "destination");

    if (!expiry)
        return;

    /* Overdue, and nothing published over a selection nobody identified. */
    g_paste_clipboards_manager_expire_password (manager);
    g_assert_cmpuint (clipboard->publications, ==, publications);

    /* The next read identifies the password still sitting there, which is what
     * the overdue countdown was waiting for. */
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    pending = clipboard->pending;
    text_ready (clipboard, settings, pending, "same secret");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpstr (clipboard->content.str, ==, "replacement");
}

typedef struct
{
    GPasteItem           *item;
    GPasteClipboardSecret secret;
} ReadResult;

static void
capture_read (GPasteClipboardProvider *provider G_GNUC_UNUSED,
              GPasteItem              *item,
              gboolean                 superseded,
              GPasteClipboardSecret    secret,
              gpointer                 user_data)
{
    ReadResult *result = user_data;

    g_assert_false (superseded);
    result->item = item;
    result->secret = secret;
}

/* A hint read that fails says nothing, but one served empty is the owner
 * answering with a value that does not match: that is a no. */
static void
test_hint_reply_without_value (gconstpointer user_data)
{
    gboolean served = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GBytes) empty = g_bytes_new_static ("", 0);
    ReadResult result = { 0 };

    clipboard->is_clipboard = TRUE;

    GPasteClipboardUpdate *pending = g_paste_clipboard_update_new (G_PASTE_CLIPBOARD_PROVIDER (clipboard), settings,
                                                                     CLIPBOARD_CONTENT_TEXT, &clipboard->pending,
                                                                     &clipboard->content, capture_read, &result);
    GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    g_paste_clipboard_update_on_mime_read (hint, (served) ? empty : NULL);
    /* The text read fails. */
    g_paste_clipboard_update_maybe_done (pending);

    g_assert_null (result.item);
    g_assert_cmpint (result.secret, ==, (served) ? CLIPBOARD_SECRET_NO : CLIPBOARD_SECRET_UNKNOWN);
}

static void
test_secret_text_policy (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    gboolean same_text = variant & 1;
    gboolean hint_first = variant & 2;
    gboolean is_clipboard = variant & 4;
    const gchar *text = (same_text) ? "same secret" : "  padded secret  ";
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    ReadResult result = { 0 };

    clipboard->is_clipboard = is_clipboard;
    g_paste_settings_set_min_text_item_size (settings, 1);
    g_paste_settings_set_max_text_item_size (settings, 1000);
    g_paste_settings_set_trim_items (settings, TRUE);
    g_paste_settings_set_password_timeout (settings, 37);
    g_paste_clipboard_content_set_text (&clipboard->content, "same secret");

    GPasteClipboardUpdate *pending = g_paste_clipboard_update_new (G_PASTE_CLIPBOARD_PROVIDER (clipboard), settings,
                                                                     CLIPBOARD_CONTENT_TEXT, &clipboard->pending,
                                                                     &clipboard->content, capture_read, &result);
    g_paste_clipboard_update_add_read (pending);
    GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);
    GBytes *bytes = g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    if (hint_first)
        g_paste_clipboard_update_on_mime_read (hint, bytes);
    text_ready (clipboard, settings, pending, text);
    if (!hint_first)
        g_paste_clipboard_update_on_mime_read (hint, bytes);
    g_paste_clipboard_update_maybe_done (pending);

    g_autoptr (GPasteItem) item = result.item;

    g_assert_cmpint (result.secret, ==, CLIPBOARD_SECRET_YES);
    g_assert_true (G_PASTE_IS_PASSWORD_ITEM (item));
    g_assert_cmpstr (g_paste_item_get_real_value (item), ==, text);
    g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (item)), ==, 37);
    g_assert_cmpstr (clipboard->content.str, ==, text);
    g_assert_cmpuint (clipboard->publications, ==, 0);
}

/* A representation landing on an update that has moved on is dropped where it
 * lands, rather than held until the update's last read reports -- which a read
 * that never reports puts off for good
 * (g_paste_clipboard_update_on_mime_read ()). */
static void
test_late_mime_read (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);

    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&clipboard->content, "previous copy");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;

    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *first = clipboard->pending;
    GPasteClipboardMimeCtx *html = g_paste_clipboard_update_add_special_mime_read (first, G_PASTE_SPECIAL_MIME_TEXT_HTML);

    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *second = clipboard->pending;

    assert_superseded (first);

    g_autoptr (GBytes) late = g_bytes_new_static ("<b>older copy</b>", 17);

    g_paste_clipboard_update_on_mime_read (html, late);
    /* Its text read is still out, so the update is still there to look at. */
    g_assert_null (first->mimes.special_mime[G_PASTE_SPECIAL_MIME_TEXT_HTML]);

    text_ready (clipboard, settings, first, "older copy");
    g_paste_clipboard_update_maybe_done (first);
    text_ready (clipboard, settings, second, "new copy");
    g_paste_clipboard_update_maybe_done (second);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "new copy");
}

static void
test_restart_completion_window (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    ExpiryCounter counter = { fixture->manager, 0 };

    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_passwords_expired, &counter);
    password_read_ready (fixture, "same secret");
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
    g_assert_cmpuint (counter.completed, ==, 0);

    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 1);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}


static void
test_stale_strip_after_publication (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    GPasteItem *rich = g_paste_text_item_new ("rich text");
    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                  g_bytes_new_static ("<b>rich text</b>", 16)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));
    g_paste_history_add (history, rich);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&clipboard->content, "rich text");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *old = clipboard->pending;
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .clipboards_manager = manager };
    g_autoptr (GError) error = NULL;
    g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
    g_assert_no_error (error);

    /* The sync shortcut publishes through this provider method. */
    g_paste_clipboard_provider_select_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard), "unrelated sync");
    text_ready (clipboard, settings, old, "rich text");
    g_paste_clipboard_update_maybe_done (old);
    g_assert_cmpstr (clipboard->content.str, ==, "unrelated sync");
    guint publications = clipboard->publications;

    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *fresh = clipboard->pending;
    text_ready (clipboard, settings, fresh, "rich text");
    fresh->mimes.special_mime[G_PASTE_SPECIAL_MIME_TEXT_HTML] = g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                                      g_bytes_new_static ("<i>rich text</i>", 16));
    g_paste_clipboard_update_maybe_done (fresh);
    /* History deduplication may retain its plain head; the stale strip must
     * not publish over this independently copied rich selection. */
    g_assert_cmpuint (clipboard->publications, ==, publications);
}

static void
test_sync_copy_order (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    gboolean newest_first = variant & 1;
    gboolean trim = variant & 2;
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);

    g_paste_settings_set_synchronize_clipboards (settings, TRUE);
    g_paste_settings_set_trim_items (settings, trim);
    primary->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&primary->content, "initial primary");
    g_paste_clipboard_content_set_text (&clipboard->content, "initial clipboard");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    primary->defer_reads = clipboard->defer_reads = TRUE;

    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));
    GPasteClipboardUpdate *older = primary->pending;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *newer = clipboard->pending;

    if (newest_first)
    {
        text_ready (clipboard, settings, newer, "newer copy");
        g_paste_clipboard_update_maybe_done (newer);
    }
    text_ready (primary, settings, older, trim ? " older copy " : "older copy");
    g_paste_clipboard_update_maybe_done (older);
    if (!newest_first)
    {
        g_assert_true (clipboard->pending == newer);
        text_ready (clipboard, settings, newer, "newer copy");
        g_paste_clipboard_update_maybe_done (newer);
    }

    g_assert_cmpstr (clipboard->content.str, ==, "newer copy");
    g_assert_cmpstr (primary->content.str, ==, "newer copy");
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "newer copy");
    /* Synchronizing the newer copy replaces the older owner. Its incomplete
     * read is cleanup-only, just like a read overtaken on the same selection;
     * completed copies are retained, but capture of every owner is not
     * promised. */
    g_assert_cmpuint (g_paste_history_get_length (history), ==, newest_first ? 1 : 2);
}

static void
test_expiry_retires_head_before_flush (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_FILE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_paste_history_load (history, "expiry-handover");
    g_paste_history_add (history, g_paste_text_item_new ("replacement"));
    g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "same secret", 37);
    g_paste_history_add (history, g_object_ref (password));
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    clipboard->is_clipboard = TRUE;
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    g_paste_clipboards_manager_select (manager, password);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *pending = clipboard->pending;

    g_paste_clipboards_manager_expire_password (manager);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "replacement");
    g_assert_cmpstr (clipboard->content.str, ==, "same secret");
    g_paste_history_flush (history);
    g_autoptr (GPasteHistory) successor = g_paste_history_new (settings);
    g_paste_history_load (successor, "expiry-handover");
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (successor, 0)), ==, "replacement");

    text_ready (clipboard, settings, pending, "same secret");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpstr (clipboard->content.str, ==, "replacement");
}

static void
test_expiry_retires_head_with_other_reading (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (1);
    password_read_ready (fixture, "same secret");
    g_paste_history_add (fixture->history, g_object_ref (fixture->password));
    g_autoptr (TestClipboard) primary = make_clipboard (fixture->settings);
    g_paste_clipboard_content_set_text (&primary->content, "same secret");
    g_paste_clipboards_manager_add_clipboard (fixture->manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (fixture->manager);
    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
    primary->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));
    GPasteClipboardUpdate *pending = primary->pending;
    pump_for (1100);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (fixture->history, 0)), ==, "replacement");
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
    text_ready (primary, fixture->settings, pending, "same secret");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpstr (primary->content.str, ==, "replacement");
    primary->defer_reads = FALSE;
    g_paste_clipboard_content_clear (&primary->content);
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_assert_cmpstr (primary->content.str, ==, "replacement");
}

static void
test_sync_after_expiry_publication (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (1);
    password_read_ready (fixture, "same secret");
    g_paste_settings_set_synchronize_clipboards (fixture->settings, TRUE);
    g_autoptr (TestClipboard) primary = make_clipboard (fixture->settings);
    g_paste_clipboard_content_set_text (&primary->content, "initial primary");
    g_paste_clipboards_manager_add_clipboard (fixture->manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
    g_paste_clipboards_manager_activate (fixture->manager);
    primary->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));
    GPasteClipboardUpdate *pending = primary->pending;

    pump_for (1100);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
    text_ready (primary, fixture->settings, pending, "new copy");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpstr (primary->content.str, ==, "new copy");
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "new copy");
}

static void
test_expiry_rechecks_new_read (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    g_autoptr (GAsyncResult) result = NULL;
    g_paste_clipboards_manager_expire_password_async (fixture->manager, capture_expiry, &result);
    password_read_ready (fixture, "same secret");
    pump_for (10);
    g_assert_nonnull (result);
    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    g_autoptr (GError) error = NULL;
    g_assert_false (g_paste_clipboards_manager_expire_password_finish (fixture->manager, result, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PENDING);
    ExpiryCounter counter = { fixture->manager, 0 };
    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_passwords_expired, &counter);
    password_read_ready (fixture, "same secret");
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 1);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

typedef enum
{
    EMPTY_TEXT_HINTED,
    EMPTY_TEXT_PLAIN,
    /* The hint is offered and not served. */
    EMPTY_TEXT_UNSERVED_HINT,
} EmptyText;

/* A password manager clearing what it copied by writing an empty text has
 * answered what the selection holds: the empty string, whatever the hint beside
 * it said or failed to say. The record of the password it replaced is retired
 * rather than left unconfirmed, which would fail every wait for expiry -- a
 * re-exec and a storage migration -- until the next copy (see the DROP case in
 * g_paste_clipboard_update_conclude ()). */
static void
test_empty_text_retires_record (gconstpointer user_data)
{
    EmptyText variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    GBytes *hint = g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    g_paste_settings_set_password_timeout (settings, 60);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_history_add (history, g_paste_text_item_new ("older copy"));
    clipboard->is_clipboard = TRUE;
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;

    const gchar *values[] = { "copied secret", "" };

    for (guint i = 0; i < G_N_ELEMENTS (values); ++i)
    {
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

        GPasteClipboardUpdate *pending = clipboard->pending;

        if (!i || variant == EMPTY_TEXT_HINTED)
            g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), hint);
        else if (variant == EMPTY_TEXT_UNSERVED_HINT)
            g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);
        text_ready (clipboard, settings, pending, values[i]);
        g_paste_clipboard_update_maybe_done (pending);
    }

    g_autoptr (GAsyncResult) result = NULL;
    g_autoptr (GError) error = NULL;

    g_paste_clipboards_manager_expire_password_async (manager, capture_expiry, &result);
    pump_for (10);
    g_assert_null (clipboard->pending);
    g_assert_nonnull (result);
    g_assert_true (g_paste_clipboards_manager_expire_password_finish (manager, result, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (clipboard->content.str, ==, "");
}

/* The deadline's data is a weak reference of its own, which nothing outside
 * the manager can match it by: walk back from a fresh source id instead, the
 * newest deadline having been attached before it. */
static GSource *
find_source_named (const gchar *name)
{
    g_autoptr (GSource) probe = g_idle_source_new ();

    g_source_set_name (probe, "[GPaste] test source id probe");
    guint top = g_source_attach (probe, NULL);

    g_source_destroy (probe);

    for (guint id = top - 1; id > 0; --id)
    {
        GSource *source = g_main_context_find_source_by_id (NULL, id);

        if (source && !g_strcmp0 (g_source_get_name (source), name))
            return source;
    }

    return NULL;
}

static void
test_expiry_deadline_with_changing_owner (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    g_autoptr (GAsyncResult) result = NULL;
    g_paste_clipboards_manager_expire_password_async (fixture->manager, capture_expiry, &result);
    /* Exercise the shipped manager and its actual source, without a second
     * GType implementation or a minute of wall-clock waiting. */
    GSource *wait = find_source_named ("[GPaste] password expiry deadline");
    g_assert_nonnull (wait);
    g_source_set_ready_time (wait, g_get_monotonic_time () + 100 * 1000);
    gint64 deadline = g_get_monotonic_time () + 3 * G_USEC_PER_SEC;

    while (!result && g_get_monotonic_time () < deadline)
    {
        GPasteClipboardUpdate *old = fixture->clipboard->pending;
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
        text_ready (fixture->clipboard, fixture->settings, old, "same secret");
        g_paste_clipboard_update_maybe_done (old);
        pump_for (50);
    }

    g_assert_nonnull (result);
    g_autoptr (GError) error = NULL;
    g_assert_false (g_paste_clipboards_manager_expire_password_finish (fixture->manager, result, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    GPasteClipboardUpdate *pending = fixture->clipboard->pending;
    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "same secret");
    text_ready (fixture->clipboard, fixture->settings, pending, "new copy");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "same secret");
}

/* A later dispatch is essential: completing a task in its creation iteration
 * makes GTask defer by itself and would miss an inline restart callback. */
typedef struct
{
    PasswordFixture *fixture;
    gboolean         inside_mutation;
    gboolean         completed;
} RestartOrder;

static void
check_restart_order (GObject      *source G_GNUC_UNUSED,
                     GAsyncResult *result,
                     gpointer      user_data)
{
    RestartOrder *order = user_data;
    g_autoptr (GError) error = NULL;

    g_assert_false (order->inside_mutation);
    g_assert_true (g_paste_clipboards_manager_expire_password_finish (order->fixture->manager, result, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (order->fixture->history, 0)), ==, "new tracked copy");
    order->completed = TRUE;
}

static gboolean
finish_restart_read (gpointer user_data)
{
    RestartOrder *order = user_data;

    order->inside_mutation = TRUE;
    password_read_ready (order->fixture, "new tracked copy");
    g_assert_false (order->completed);
    order->inside_mutation = FALSE;
    return G_SOURCE_REMOVE;
}

static void
test_restart_waits_for_history (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    /* Capture tracking at read start, as the real manager does. */
    password_read_ready (fixture, "same secret");
    g_paste_settings_set_track_changes (fixture->settings, TRUE);
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    RestartOrder order = { .fixture = fixture };

    g_paste_clipboards_manager_expire_password_async (fixture->manager, check_restart_order, &order);
    g_idle_add (finish_restart_read, &order);
    pump_for (20);
    g_assert_true (order.completed);
}

static void
test_expiry_independent_deadlines (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    g_autoptr (GAsyncResult) first = NULL;
    g_autoptr (GAsyncResult) second = NULL;

    g_paste_clipboards_manager_expire_password_async (fixture->manager, capture_expiry, &first);
    GSource *deadline = find_source_named ("[GPaste] password expiry deadline");
    g_assert_nonnull (deadline);
    g_paste_clipboards_manager_expire_password_async (fixture->manager, capture_expiry, &second);
    /* Only the older request reaches its deadline; the later one must retain
     * both its budget and the force flag that makes the read complete it. */
    g_source_set_ready_time (deadline, 0);
    pump_for (10);
    g_assert_nonnull (first);
    g_assert_null (second);
    g_autoptr (GError) error = NULL;
    g_assert_false (g_paste_clipboards_manager_expire_password_finish (fixture->manager, first, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    g_clear_error (&error);
    password_read_ready (fixture, "same secret");
    pump_for (10);
    g_assert_nonnull (second);
    g_assert_true (g_paste_clipboards_manager_expire_password_finish (fixture->manager, second, &error));
    g_assert_no_error (error);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

static void
test_expiry_pending_preserves_other_waiter (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    g_autoptr (GAsyncResult) first = NULL;
    g_autoptr (GAsyncResult) second = NULL;
    g_autoptr (GAsyncResult) retry = NULL;

    g_paste_clipboards_manager_expire_password_async (fixture->manager, capture_expiry, &first);
    g_paste_clipboards_manager_expire_password_async (fixture->manager, capture_expiry, &second);
    password_read_ready (fixture, "same secret");
    pump_for (10);
    g_assert_nonnull (first);
    g_assert_nonnull (second);
    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    g_autoptr (GError) error = NULL;
    g_assert_false (g_paste_clipboards_manager_expire_password_finish (fixture->manager, first, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PENDING);
    g_clear_error (&error);
    g_paste_clipboards_manager_expire_password_async (fixture->manager, capture_expiry, &retry);
    /* This caller gives up after PENDING; the first caller's retry still owns
     * a wait and must not depend on the second caller rearming anything. */
    g_assert_false (g_paste_clipboards_manager_expire_password_finish (fixture->manager, second, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PENDING);
    g_clear_error (&error);
    password_read_ready (fixture, "same secret");
    pump_for (10);
    g_assert_nonnull (retry);
    g_assert_true (g_paste_clipboards_manager_expire_password_finish (fixture->manager, retry, &error));
    g_assert_no_error (error);
}

static void
test_restore_rejected_head (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);

    g_paste_settings_set_synchronize_clipboards (settings, TRUE);
    g_paste_history_add (history, g_paste_text_item_new ("fallback"));
    g_paste_history_add (history, g_paste_text_item_new ("unavailable item"));
    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&clipboard->content, "initial");
    g_paste_clipboard_content_set_text (&primary->content, "initial");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    primary->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));
    GPasteClipboardUpdate *old = primary->pending;

    /* Removing an unpublishable head emits selected synchronously inside the
     * restoration. That publication is independent even under maintenance. */
    clipboard->reject_next = TRUE;
    g_paste_clipboard_content_clear (&clipboard->content);
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    text_ready (primary, settings, old, "older copy");
    g_paste_clipboard_update_maybe_done (old);
    g_assert_cmpstr (clipboard->content.str, ==, "fallback");
    g_assert_cmpstr (primary->content.str, ==, "fallback");
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
}

static void
test_independent_publication_copy_order (gconstpointer user_data)
{
    gboolean publish_item = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);

    g_paste_settings_set_synchronize_clipboards (settings, TRUE);
    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&clipboard->content, "initial");
    g_paste_clipboard_content_set_text (&primary->content, "initial");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    primary->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));
    GPasteClipboardUpdate *old = primary->pending;

    /* Publish to one selection only, leaving the other's read live. A manager
     * select writes both and supersedes that read, masking an incorrect
     * serial. */
    if (publish_item)
    {
        g_autoptr (GPasteItem) item = g_paste_text_item_new ("explicit selection");

        g_assert_true (g_paste_clipboard_provider_select_item (G_PASTE_CLIPBOARD_PROVIDER (clipboard), item));
    }
    else
        g_paste_clipboard_provider_select_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard), "explicit selection");

    g_assert_true (primary->pending == old);
    text_ready (primary, settings, old, "older copy");
    g_paste_clipboard_update_maybe_done (old);
    g_assert_cmpstr (clipboard->content.str, ==, "explicit selection");
    g_assert_cmpstr (primary->content.str, ==, "older copy");
}

static void
test_sync_during_classification (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    gboolean secret = variant != 0;
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GPasteItem) initial = (secret) ? g_paste_password_item_new (NULL, "same secret", 37)
                                            : g_paste_text_item_new ("previous copy");

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_track_changes (settings, FALSE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_settings_set_password_timeout (settings, 37);
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    g_paste_clipboards_manager_select (manager, initial);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    GPasteClipboardUpdate *pending = clipboard->pending;
    GPasteClipboardMimeCtx *hint = (secret) ? g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT) : NULL;
    guint publications = primary->publications;

    g_paste_clipboards_manager_sync_from_to (manager, TRUE);
    g_paste_clipboards_manager_sync_from_to (manager, TRUE);
    g_assert_cmpuint (clipboard->sync_reads, ==, 0);
    g_assert_cmpuint (primary->publications, ==, publications);
    text_ready (clipboard, settings, pending, (secret) ? "same secret" : "new ordinary copy");
    if (hint)
        g_paste_clipboard_update_on_mime_read (hint, (variant == 2) ? NULL : g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    g_paste_clipboard_update_maybe_done (pending);

    g_assert_cmpuint (primary->publications, ==, publications + 1);
    g_assert_cmpuint (clipboard->sync_reads, ==, (secret) ? 0 : 1);
    g_assert_cmpstr (primary->content.str, ==, (secret) ? "same secret" : "new ordinary copy");
    g_assert_cmpint (G_PASTE_IS_PASSWORD_ITEM (primary->published), ==, secret);
    if (secret)
    {
        /* A sync during classification must preserve the source's countdown. */
        g_paste_clipboards_manager_expire_password (manager);
        g_assert_cmpstr (clipboard->content.str, ==, "");
        g_assert_cmpstr (primary->content.str, ==, "");
    }
}

static void
test_stale_sync_reply (gconstpointer user_data)
{
    gboolean source_changed = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    clipboard->is_clipboard = TRUE;
    clipboard->defer_sync = TRUE;
    g_paste_settings_set_track_changes (settings, FALSE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboard_content_set_text (&clipboard->content, "ordinary source");
    g_paste_clipboard_content_set_text (&primary->content, "destination");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    g_paste_clipboards_manager_sync_from_to (manager, TRUE);
    g_assert_nonnull (clipboard->sync);

    TestClipboard *changed = (source_changed) ? clipboard : primary;

    g_paste_clipboard_content_set_text (&changed->content, "new copy");
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (changed));

    /* Taken off the mock first: a retried sync stores its own data there. */
    GPasteClipboardSyncData *late = g_steal_pointer (&clipboard->sync);

    g_paste_clipboard_sync_data_deliver (late, "late source text");
    g_paste_clipboard_sync_data_free (late);
    g_assert_cmpuint (primary->publications, ==, 0);
    g_assert_cmpstr (primary->content.str, ==, (source_changed) ? "destination" : "new copy");

    /* A source that moved is synced again, as it holds now; a destination that
     * moved superseded the write, and nothing is asked again. */
    g_assert_cmpuint (clipboard->sync_reads, ==, (source_changed) ? 2 : 1);
    if (!source_changed)
    {
        g_assert_null (clipboard->sync);
        return;
    }

    g_paste_clipboard_sync_data_deliver (clipboard->sync, "new copy");
    g_paste_clipboard_sync_data_free (g_steal_pointer (&clipboard->sync));
    g_assert_cmpuint (primary->publications, ==, 1);
    g_assert_cmpstr (primary->content.str, ==, "new copy");
}

/* A destination re-owned while the sync reply is out is one that reply may not go
 * back over. That re-own is the read's own, made as it concludes on a text the
 * trim policy shortened (g_paste_clipboard_update_conclude ()), so the only thing
 * that can tell the reply its pair has moved is the generation the announcement
 * carries (g_paste_clipboards_manager_published ()) -- which is why it is taken
 * there and not by the manager's own publishing routes, this write being none of
 * them. Left standing, the reply puts the text as it was read back over the
 * trimmed text that replaced it. */
static void
test_sync_reply_after_trimmed_reown (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    /* The clipboard is the destination here: re-owning a trimmed text is the one
     * selection's business that serves it (g_paste_clipboard_update_conclude ()). */
    clipboard->is_clipboard = TRUE;
    primary->defer_sync = TRUE;
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_settings_set_trim_items (settings, TRUE);
    g_paste_clipboard_content_set_text (&clipboard->content, "destination");
    g_paste_clipboard_content_set_text (&primary->content, "source text");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);

    /* The destination is making a copy of its own while the sync is asked for. */
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    GPasteClipboardUpdate *pending = clipboard->pending;

    g_paste_clipboards_manager_sync_from_to (manager, FALSE);
    g_assert_nonnull (primary->sync);

    text_ready (clipboard, settings, pending, "  padded copy  ");
    g_paste_clipboard_update_maybe_done (pending);
    pump_for (10);
    g_assert_cmpstr (clipboard->content.str, ==, "padded copy");

    guint publications = clipboard->publications;

    g_paste_clipboard_sync_data_deliver (primary->sync, "source text");
    g_paste_clipboard_sync_data_free (g_steal_pointer (&primary->sync));
    g_assert_cmpuint (clipboard->publications, ==, publications);
    g_assert_cmpstr (clipboard->content.str, ==, "padded copy");
}

static void
on_unconfirmed_expiry (GObject      *source G_GNUC_UNUSED,
                       GAsyncResult *result,
                       gpointer      user_data)
{
    ExpiryCounter *counter = user_data;
    g_autoptr (GError) error = NULL;

    g_assert_false (g_paste_clipboards_manager_expire_password_finish (counter->manager, result, &error));
    /* G_PASTE_ERROR: the daemon answers a Reexecute caller with it as it is. */
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    ++counter->completed;
}

/* A timeout edit made while a hinted read is out keeps the edited deadline:
 * the item the read captures only carries the password-timeout default. */
static void
test_timeout_edit_hinted_read (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);

    g_paste_settings_set_password_timeout (fixture->settings, 0);
    GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (fixture->clipboard->pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), 1);
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    g_paste_clipboard_update_on_mime_read (hint, g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    password_read_ready (fixture, "same secret");
    pump_for (1100);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

/* A padded password re-asserted with its hint unanswered stays the password it
 * was recorded as: no trimmed text is re-owned over it nor added to the history
 * (see the text policy in g_paste_clipboard_update_conclude ()). */
static void
test_known_secret_failed_hint (gconstpointer user_data)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_trim_items (settings, TRUE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    g_paste_settings_set_password_timeout (settings, 37);
    clipboard->defer_reads = TRUE;

    /* The first read's hint answers, the re-assertion's does not. */
    for (guint read = 0; read < 2; ++read)
    {
        if (read == 1 && GPOINTER_TO_INT (user_data))
        {
            g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
            GPasteClipboardUpdate *failed = clipboard->pending;
            GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (failed, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

            g_paste_clipboard_update_on_mime_read (hint, NULL);
            g_paste_clipboard_update_maybe_done (failed);
            g_paste_clipboard_update_maybe_done (failed);
            g_assert_cmpint (clipboard->content.kind, ==, CLIPBOARD_CONTENT_IGNORED);
        }

        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

        GPasteClipboardUpdate *pending = clipboard->pending;
        GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

        g_paste_clipboard_update_on_mime_read (hint, (read == 0) ? g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT) : NULL);
        text_ready (clipboard, settings, pending, "  known secret  ");
        g_paste_clipboard_update_maybe_done (pending);
    }

    g_autoptr (GPasteItem) head = g_paste_history_dup (history, 0);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_true (G_PASTE_IS_PASSWORD_ITEM (head));
    g_assert_cmpstr (g_paste_item_get_real_value (head), ==, "  known secret  ");
    g_assert_true (!clipboard->published || G_PASTE_IS_PASSWORD_ITEM (clipboard->published));
}

/* Conclude the fixture's pending read with its text read failed and its hint
 * unanswered, which leaves the recorded password unconfirmed. */
static void
fail_password_read (PasswordFixture *fixture)
{
    GPasteClipboardUpdate *pending = fixture->clipboard->pending;
    GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    g_paste_clipboard_update_on_mime_read (hint, NULL);
    /* The mock's text read, then the read update_new () counts in for its caller. */
    g_paste_clipboard_update_maybe_done (pending);
    g_paste_clipboard_update_maybe_done (pending);
}

/* A timed password nobody could identify is asked about again rather than
 * reported as a failure straight away: nothing else would ever come to resolve
 * the record, so `gpaste-client daemon-reexec` would refuse for as long as the
 * selection stood still (reread_unconfirmed ()).
 *
 * @user_data says whether that second read identifies the selection. One that
 * does resolves the expiry and the password comes off; one that fails the same
 * way is the answer the caller is owed, and the password waits for the next read
 * that identifies the selection. */
static void
test_unconfirmed_password_shutdown (gconstpointer user_data)
{
    gboolean answered = GPOINTER_TO_INT (user_data);
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    ExpiryCounter counter = { fixture->manager, 0 };

    fail_password_read (fixture);

    guint publications = fixture->clipboard->publications;

    g_paste_clipboards_manager_expire_password_async (fixture->manager,
                                                      (answered) ? on_passwords_expired : on_unconfirmed_expiry,
                                                      &counter);
    /* The waiter asked the selection again, and is held open by that read. */
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 0);
    g_assert_nonnull (fixture->clipboard->pending);

    if (answered)
        password_read_ready (fixture, "same secret");
    else
        fail_password_read (fixture);
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 1);

    if (answered)
    {
        g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
        return;
    }

    g_assert_cmpuint (fixture->clipboard->publications, ==, publications);

    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    password_read_ready (fixture, "same secret");
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

/* A countdown that runs out while the record is unconfirmed marks the password
 * overdue rather than taking it off, and asks for the read that settles it: no
 * other read is owed, so with nothing asked for here the deadline would wait on
 * a copy the user may never make and the password would sit on the selection for
 * the rest of the session.
 *
 * @user_data says whether that read identifies the selection. One that does
 * spends the deadline there and then; one that fails the same way leaves the
 * password overdue, which is the selection genuinely not answering rather than a
 * deadline nobody is waiting on. */
static void
test_overdue_password_is_reread (gconstpointer user_data)
{
    gboolean answered = GPOINTER_TO_INT (user_data);
    g_autoptr (PasswordFixture) fixture = password_fixture_new (1);

    fail_password_read (fixture);
    g_assert_null (fixture->clipboard->pending);

    guint publications = fixture->clipboard->publications;

    pump_for (1100);

    /* The countdown has run out and asked the selection what it carries. */
    g_assert_cmpuint (fixture->clipboard->publications, ==, publications);
    g_assert_nonnull (fixture->clipboard->pending);

    if (!answered)
    {
        /* Nothing is published over a selection nobody has identified, so the
         * password stays where it is and the deadline stays owed. */
        fail_password_read (fixture);
        pump_for (10);
        g_assert_cmpuint (fixture->clipboard->publications, ==, publications);

        /* And the copy the user does make next is what identifies it, which
         * spends the deadline that was waiting. */
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
        password_read_ready (fixture, "same secret");
        pump_for (10);
        g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
        return;
    }

    password_read_ready (fixture, "same secret");
    pump_for (10);

    /* Identified as still carrying it, so the deadline is spent: the password
     * comes off and the history's head goes there instead. */
    g_assert_cmpuint (fixture->clipboard->publications, ==, publications + 1);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

/* The read a waiter asks for is a question about the selection, not a copy made
 * on it: nothing enters the history and the other selection is left alone, where
 * a copy the user really made is what it follows. */
static void
test_unconfirmed_reread_is_not_a_copy (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    g_autoptr (TestClipboard) primary = make_clipboard (fixture->settings);
    ExpiryCounter counter = { fixture->manager, 0 };

    g_paste_clipboard_content_set_text (&primary->content, "mouse selection");
    g_paste_clipboards_manager_add_clipboard (fixture->manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_settings_set_synchronize_clipboards (fixture->settings, TRUE);

    fail_password_read (fixture);

    guint publications = primary->publications;
    guint length = g_paste_history_get_length (fixture->history);

    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_passwords_expired, &counter);
    pump_for (10);
    password_read_ready (fixture, "a plain copy");
    pump_for (10);

    g_assert_cmpuint (counter.completed, ==, 1);
    g_assert_cmpuint (primary->publications, ==, publications);
    g_assert_cmpstr (primary->content.str, ==, "mouse selection");
    g_assert_cmpuint (g_paste_history_get_length (fixture->history), ==, length);
}

/* The read a waiter asks for retires a record only once the selection has moved
 * on from what it names, as notify_finish () does: an owner re-asserting the
 * same text with no hint on it has moved nothing, and dropping the record there
 * would leave the cleartext to travel as bare text. */
static void
test_unconfirmed_reread_keeps_the_record (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (0);
    g_autoptr (TestClipboard) primary = make_clipboard (fixture->settings);
    ExpiryCounter counter = { fixture->manager, 0 };

    g_paste_clipboards_manager_add_clipboard (fixture->manager, G_PASTE_CLIPBOARD_PROVIDER (primary));

    fail_password_read (fixture);

    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_passwords_expired, &counter);
    pump_for (10);
    password_read_ready (fixture, "same secret");
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 1);

    /* The record still names what the selection carries, so a sync carries it
     * over as the password it is rather than as its cleartext. */
    g_paste_clipboards_manager_sync_from_to (fixture->manager, TRUE);
    g_assert_nonnull (primary->published);
    g_assert_true (G_PASTE_IS_PASSWORD_ITEM (primary->published));
}

/* And the other way round: a selection that did move on has its record retired
 * there and then, rather than left for whichever route asks next. A record still
 * standing is what refresh_text () refuses a strip on, so the user asking to
 * drop an item's rich flavours would be answered with nothing at all. */
static void
test_unconfirmed_reread_retires_the_record (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (0);
    ExpiryCounter counter = { fixture->manager, 0 };
    GPasteItem *rich = g_paste_text_item_new ("a new copy");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>a new copy</b>", 17)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));

    g_paste_history_add (fixture->history, rich);
    fail_password_read (fixture);

    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_passwords_expired, &counter);
    pump_for (10);
    password_read_ready (fixture, "a new copy");
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 1);

    guint publications = fixture->clipboard->publications;
    const GPasteDaemonMethods methods = { .history = fixture->history, .settings = fixture->settings, .clipboards_manager = fixture->manager };
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
    g_assert_no_error (error);

    g_assert_cmpuint (fixture->clipboard->publications, ==, publications + 1);
    g_assert_nonnull (fixture->clipboard->published);
    g_assert_cmpstr (g_paste_item_get_uuid (fixture->clipboard->published), ==, uuid);
    g_assert_null (g_paste_item_get_special_values (fixture->clipboard->published));
}

/* A countdown disabled before a failed read stays disabled once a later read
 * identifies the selection, past the deadline it had. */
static void
test_edit_before_failed_read (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (1);

    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), 0);
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    fail_password_read (fixture);
    pump_for (1100);
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    password_read_ready (fixture, "same secret");
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "same secret");
}

typedef struct
{
    ExpiryCounter    counter;
    PasswordFixture *fixture;
    gboolean         saw_new_copy;
    gboolean         read_done;
} ExpiryHistoryCheck;

static void
on_expired_check_history (GObject      *source,
                          GAsyncResult *result,
                          gpointer      user_data)
{
    ExpiryHistoryCheck *check = user_data;

    on_passwords_expired (source, result, &check->counter);

    GPasteItem *head = g_paste_history_get (check->fixture->history, 0);

    check->saw_new_copy = g_str_equal (g_paste_item_get_value (head), "new copy");
}

static gboolean
complete_new_copy_read (gpointer user_data)
{
    ExpiryHistoryCheck *check = user_data;

    password_read_ready (check->fixture, "new copy");
    check->read_done = TRUE;
    return G_SOURCE_REMOVE;
}

/* Forced expiry answers only once the read that resolved it has updated the
 * history, even though GTask may invoke the callback inline. */
static void
test_forced_expiry_after_history_update (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);
    ExpiryHistoryCheck check = { .counter = { fixture->manager, 0 }, .fixture = fixture };
    /* Tracking policy is captured when the read starts. */
    GPasteClipboardUpdate *untracked = fixture->clipboard->pending;

    g_paste_settings_set_track_changes (fixture->settings, TRUE);
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    text_ready (fixture->clipboard, fixture->settings, untracked, "stale");
    g_paste_clipboard_update_maybe_done (untracked);
    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_expired_check_history, &check);
    g_source_set_name_by_id (g_timeout_add (5, complete_new_copy_read, &check), "[GPaste] test new copy read");
    while (!check.read_done || !check.counter.completed)
        g_main_context_iteration (NULL, TRUE);
    g_assert_true (check.saw_new_copy);
}

/* A rich-text refresh pending on a read hands back an item the history owns:
 * adding it moves that entry rather than letting growing-lines duplicate it. */
static void
test_strip_growing_duplicate (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    GPasteItem *rich = g_paste_text_item_new ("rich text");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML, g_bytes_new_static ("<b>rich text</b>", 16)));

    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));

    g_paste_history_add (history, rich);
    g_paste_history_add (history, g_paste_text_item_new ("middle"));
    g_paste_history_add (history, g_paste_text_item_new ("rich"));
    g_paste_settings_set_growing_lines (settings, TRUE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);

    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    clipboard->is_clipboard = TRUE;
    g_paste_clipboard_content_set_text (&clipboard->content, "rich");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    GPasteClipboardUpdate *pending = clipboard->pending;
    GPasteDaemonMethods methods = { .history = history, .settings = settings, .clipboards_manager = manager };
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
    g_assert_no_error (error);
    text_ready (clipboard, settings, pending, "rich text");
    g_paste_clipboard_update_maybe_done (pending);

    const GPtrArray *items = g_paste_history_get_history (history);
    guint matches = 0;

    for (guint i = 0; i < items->len; ++i)
    {
        if (g_str_equal (uuid, g_paste_item_get_uuid (g_ptr_array_index (items, i))))
            ++matches;
    }
    g_assert_cmpuint (matches, ==, 1);
}

static void
test_reassert_preserves_timeout (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (1);

    g_paste_settings_set_password_timeout (fixture->settings, 0);
    GPasteClipboardUpdate *pending = fixture->clipboard->pending;
    GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    g_paste_clipboard_update_on_mime_read (hint, g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    password_read_ready (fixture, "same secret");
    pump_for (1100);
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

static void
test_edit_after_failed_read (gconstpointer user_data)
{
    gboolean cancel = GPOINTER_TO_INT (user_data);
    g_autoptr (PasswordFixture) fixture = password_fixture_new (cancel ? 1 : 37);

    fail_password_read (fixture);
    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), cancel ? 0 : 1);
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    pump_for (1100);

    /* Resolve the selection after a shortened deadline would have elapsed. */
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
    password_read_ready (fixture, "same secret");
    g_assert_cmpstr (fixture->clipboard->content.str, ==, cancel ? "same secret" : "replacement");
}

static void
test_sync_finishes_elapsed_edit (gconstpointer user_data)
{
    gboolean automatic = GPOINTER_TO_INT (user_data);
    g_autoptr (PasswordFixture) fixture = password_fixture_new (0);

    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), 1);
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    fail_password_read (fixture);
    pump_for (1100);

    g_autoptr (TestClipboard) primary = make_clipboard (fixture->settings);

    g_paste_clipboards_manager_add_clipboard (fixture->manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (fixture->manager);
    g_paste_settings_set_synchronize_clipboards (fixture->settings, automatic);
    g_paste_settings_set_password_timeout (fixture->settings, 0);
    primary->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));

    GPasteClipboardUpdate *pending = primary->pending;
    GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    g_paste_clipboard_update_on_mime_read (hint, g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    text_ready (primary, fixture->settings, pending, "same secret");
    g_paste_clipboard_update_maybe_done (pending);

    if (!automatic)
        g_paste_clipboards_manager_sync_from_to (fixture->manager, FALSE);

    /* Publishing resolves the destination's identity and its overdue edit. */
    g_assert_cmpstr (fixture->clipboard->content.str, ==, "replacement");
}

static void
test_strip_preserves_idle_password (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (0);

    password_read_ready (fixture, "same secret");
    GPasteItem *rich = g_paste_text_item_new ("same secret");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                  g_bytes_new_static ("<b>same secret</b>", 18)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));

    g_paste_history_add (fixture->history, rich);
    guint publications = fixture->clipboard->publications;
    const GPasteDaemonMethods methods = { .history = fixture->history, .settings = fixture->settings, .clipboards_manager = fixture->manager };
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (fixture->clipboard->publications, ==, publications);
    g_assert_true (G_PASTE_IS_PASSWORD_ITEM (fixture->clipboard->published));
    g_assert_null (g_paste_item_get_special_values (g_paste_history_get_by_uuid (fixture->history, uuid)));
}

/* Variants: 0 removes a text head, 1 has the provider refuse it, 2 removes a
 * password head. A password becoming the head that way is not published, and
 * what was removed does not stay on the selection: it is cleared. */
static void
test_automatic_head_skips_password (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    gboolean refuse = variant == 1;
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    GPasteItem *password = g_paste_password_item_new (NULL, "secret", 0);
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (password));

    g_paste_history_add (history, password);
    g_paste_history_add (history, (variant == 2) ? g_paste_password_item_new (NULL, "removed secret", 0) : g_paste_text_item_new ("unavailable"));
    clipboard->refuse_value = refuse ? "unavailable" : NULL;
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    if (variant == 2)
        g_assert_true (g_paste_history_select (history, g_paste_item_get_uuid (g_paste_history_get (history, 0))));
    if (!refuse)
        g_paste_history_remove (history, 0);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_cmpstr (clipboard->content.str, ==, "");
    g_assert_false (G_PASTE_IS_PASSWORD_ITEM (clipboard->published));

    /* Explicit selection must still publish the password at the head. */
    g_assert_true (g_paste_history_select (history, uuid));
    g_assert_true (G_PASTE_IS_PASSWORD_ITEM (clipboard->published));
}

/* The clearing a password head leaves behind reaches every selection, the way
 * the publication of any other new head would: a primary carrying a mouse
 * selection of its own is emptied too. Pinned here so that deselect_password ()'s
 * guarded write is not taken for the rule (clear ()). */
static void
test_automatic_head_clears_every_selection (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_history_add (history, g_paste_password_item_new (NULL, "secret", 0));
    g_paste_history_add (history, g_paste_text_item_new ("removed"));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboard_content_set_text (&primary->content, "mouse selection");

    g_paste_history_remove (history, 0);

    g_assert_cmpstr (clipboard->content.str, ==, "");
    g_assert_cmpstr (primary->content.str, ==, "");
}

/* Removing the only item leaves no head to publish over what it left on the
 * selections, and no head to withhold either, so a password goes off them
 * because its *entry* left the history -- announced from whatever position it
 * held (GPasteHistory::password-dropped, and /clipboard/dropped_password for one
 * that is not the head) -- and not through the clear remove_common () owes a
 * password head. Without it, a password removed as the last item would keep its
 * cleartext on every selection for the rest of the session, with the entry that
 * recorded it gone too.
 *
 * @user_data says which kind went. Ordinary text stays where it is -- emptying
 * the history does not take it off either, and it is the user's own copy. */
static void
test_automatic_head_clears_removed_password (gconstpointer user_data)
{
    gboolean password = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    const gchar *value = (password) ? "the only secret" : "the only copy";

    clipboard->is_clipboard = TRUE;
    g_paste_history_add (history, (password) ? g_paste_password_item_new (NULL, value, 0) : g_paste_text_item_new (value));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    /* Explicitly selected, a password reaching a selection no other way. */
    g_assert_true (g_paste_history_select (history, g_paste_item_get_uuid (g_paste_history_get (history, 0))));
    g_assert_cmpstr (clipboard->content.str, ==, value);

    g_paste_history_remove (history, 0);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_assert_cmpstr (clipboard->content.str, ==, (password) ? "" : value);
}

/* Emptying a history takes every entry with it, so a password sitting on a
 * selection is given up for the reason above -- its entry left -- rather than
 * through any clear: there is no head left to publish, and emptying emits no
 * "selected" at all. Ordinary text is the user's own copy and stays.
 *
 * @user_data says which kind was at the head. */
static void
test_emptied_history_clears_password (gconstpointer user_data)
{
    gboolean password = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    const gchar *value = (password) ? "the emptied secret" : "the emptied copy";

    clipboard->is_clipboard = TRUE;
    g_paste_history_add (history, (password) ? g_paste_password_item_new (NULL, value, 0) : g_paste_text_item_new (value));
    g_paste_history_add (history, g_paste_text_item_new ("older"));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_assert_true (g_paste_history_select (history, g_paste_item_get_uuid (g_paste_history_get (history, 1))));
    g_assert_cmpstr (clipboard->content.str, ==, value);

    g_paste_history_empty (history);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_assert_cmpstr (clipboard->content.str, ==, (password) ? "" : value);
}

/* A read saying the selection carries a secret it could make no item of leaves a
 * record nothing else will ever retire (@sensitive on _Clipboard), and the
 * re-read a waiter for expiry asks for is no exception: it answers through
 * identify_ready (), where a hint that does not come back says nothing about the
 * cleartext still sitting there -- the record is precisely what is kept for it.
 * Retiring it would have the next sync publish that cleartext onto the other
 * selection, stripped of the hint that keeps other clipboard managers from
 * recording it. */
static void
test_unrecorded_secret_reread_keeps_the_record (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "an edited secret", 1);
    ExpiryCounter counter = { manager, 0 };

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboard_content_set_text (&primary->content, "the other selection");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;

    /* A hint that came back over a text read that failed: a secret on the
     * selection with nothing to record it as. */
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    GPasteClipboardUpdate *pending = clipboard->pending;
    GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    g_paste_clipboard_update_on_mime_read (hint, g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    g_paste_clipboard_update_maybe_done (pending);
    g_paste_clipboard_update_maybe_done (pending);

    /* Then a timeout edit made while the next read is out, and that read
     * concluded with neither a text nor an answer about the hint: the selection
     * is now one nobody has identified, which is what has a waiter ask it
     * again -- and what keeps the record above standing meanwhile. */
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_rearm_password (manager, password);
    pending = clipboard->pending;
    hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);
    g_paste_clipboard_update_on_mime_read (hint, NULL);
    g_paste_clipboard_update_maybe_done (pending);
    g_paste_clipboard_update_maybe_done (pending);

    /* The re-read comes back with a text and, again, no answer about the hint. */
    g_paste_clipboards_manager_expire_password_async (manager, on_passwords_expired, &counter);
    pump_for (10);
    g_assert_nonnull (clipboard->pending);
    pending = clipboard->pending;
    hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);
    g_paste_clipboard_update_on_mime_read (hint, NULL);
    text_ready (clipboard, settings, pending, "what the read did see");
    g_paste_clipboard_update_maybe_done (pending);
    pump_for (10);
    g_assert_cmpuint (counter.completed, ==, 1);

    /* So the other selection is left alone, keeping its own copy. */
    guint publications = primary->publications;

    g_paste_clipboards_manager_sync_from_to (manager, TRUE);
    g_assert_cmpuint (primary->publications, ==, publications);
    g_assert_cmpstr (primary->content.str, ==, "the other selection");
}

/* A hint that never answered does not make a password: the text is trimmed and
 * deduplicated like any other, but never re-owned, which would put the stripped
 * text over what may be a secret. */
static void
test_unknown_hint_text_policy (gconstpointer user_data)
{
    gboolean is_clipboard = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    clipboard->is_clipboard = is_clipboard;
    g_paste_settings_set_trim_items (settings, TRUE);
    g_paste_settings_set_primary_to_history (settings, TRUE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;

    /* A first copy, then the owner re-asserting it: both hints go unanswered. */
    for (guint copy = 0; copy < 2; ++copy)
    {
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

        GPasteClipboardUpdate *pending = clipboard->pending;
        GPasteClipboardMimeCtx *hint = g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

        g_paste_clipboard_update_on_mime_read (hint, NULL);
        text_ready (clipboard, settings, pending, "  padded text  ");
        g_paste_clipboard_update_maybe_done (pending);
    }

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (history, 0)), ==, "padded text");
    g_assert_cmpuint (clipboard->publications, ==, 0);
}

/* A text the size policy turns down leaves no value behind, its hint unanswered
 * or not: the cache would be holding the selection's own bytes, which for this one
 * text can be megabytes (set_ignored ()). What it would have been kept for -- a
 * standing password record to match -- survives it anyway: a cache with no value
 * is a read that saw nothing, so the record stands unconfirmed instead of being
 * retired. */
static void
test_unknown_hint_dropped_text (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_max_text_item_size (settings, 8);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    GPasteClipboardUpdate *pending = clipboard->pending;

    /* The selection offers the hint and never serves it, so nothing is known
     * about what it carries. */
    g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);
    text_ready (clipboard, settings, pending, "a text the policy turns down");
    g_paste_clipboard_update_maybe_done (pending);

    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_assert_null (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)));
    /* Not empty for all that: the selection has an owner, so nothing is put back
     * on it. */
    g_assert_false (g_paste_clipboard_provider_is_empty (G_PASTE_CLIPBOARD_PROVIDER (clipboard)));
    g_assert_cmpuint (clipboard->publications, ==, 0);
}

/* A password's entry leaving the history takes its cleartext off the selections
 * carrying it, from whatever position that entry held: with password-timeout at 0
 * there is no countdown to take it off, and the entry the user just deleted was
 * the last thing recording the exposure. The replacement is the countdown's own --
 * the first item that is not a password, or the empty string where the history has
 * none left.
 *
 * And only those selections: the primary here has moved on to a copy of its own,
 * which is the user's and not ours to overwrite. A head being removed is the other
 * story (clear (), the /clipboard/automatic_head tests): that one publishes over
 * every selection, because it is the publication the new head is owed.
 *
 * @user_data says how the entry goes: deleted where it sits, or emptied out with
 * the rest of the history. */
static void
test_dropped_password (gconstpointer user_data)
{
    gboolean emptied = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    GPasteItem *password = g_paste_password_item_new (NULL, "the secret", 0);
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (password));

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_history_add (history, password);
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));

    /* Explicitly selected, a password reaching a selection no other way, and then
     * left behind by a later copy: the entry is no longer the head, which is the
     * whole point -- the history's head says nothing about what a selection is
     * carrying. */
    g_assert_true (g_paste_history_select (history, uuid));
    g_assert_cmpstr (clipboard->content.str, ==, "the secret");
    g_assert_cmpstr (primary->content.str, ==, "the secret");
    g_paste_history_add (history, g_paste_text_item_new ("a later copy"));

    /* The primary moves on with a copy of the user's own, which retires the record
     * kept for it. */
    g_paste_clipboard_content_set_text (&primary->content, "mouse selection");
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (primary));

    guint publications = primary->publications;

    if (emptied)
        g_paste_history_empty (history);
    else
        g_assert_true (g_paste_history_remove_by_uuid (history, uuid));

    g_assert_cmpstr (clipboard->content.str, ==, (emptied) ? "" : "a later copy");
    g_assert_cmpstr (primary->content.str, ==, "mouse selection");
    g_assert_cmpuint (primary->publications, ==, publications);
}

static void
on_disposed_expiry (GObject      *source G_GNUC_UNUSED,
                    GAsyncResult *result,
                    gpointer      user_data)
{
    guint *completed = user_data;
    g_autoptr (GError) error = NULL;

    g_assert_true (g_task_propagate_boolean (G_TASK (result), &error));
    g_assert_no_error (error);
    ++*completed;
}

static void
test_expiry_idle_lifetime (gconstpointer user_data)
{
    gboolean dispose = GPOINTER_TO_INT (user_data);
    g_autoptr (PasswordFixture) fixture = password_fixture_new (0);
    guint completed = 0;

    password_read_ready (fixture, "same secret");
    gpointer manager = fixture->manager;

    g_object_add_weak_pointer (G_OBJECT (manager), &manager);
    g_paste_clipboards_manager_expire_password_async (fixture->manager, on_disposed_expiry, &completed);
    if (dispose)
    {
        g_object_run_dispose (G_OBJECT (fixture->manager));
        g_object_run_dispose (G_OBJECT (fixture->manager));
    }
    g_clear_object (&fixture->manager);
    g_assert_null (manager);
    pump_for (10);
    g_assert_cmpuint (completed, ==, 1);
}

/* The selection carries ordinary text -- no hint on it, which is what every
 * MakePassword is made of -- and the user gives that text a countdown while a
 * read of the selection is still out. The edit is resolved on the value alone,
 * exactly as it would have been with no read in flight, so the countdown runs
 * and takes the cleartext back off (finish_expiry ()). The owner saying the text
 * is no secret keeps the read's own item out of the password's place in the
 * history, and nothing more. */
static void
test_timeout_edit_plain_selection (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "same secret", 0);

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_track_changes (settings, FALSE);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_history_add (history, g_paste_text_item_new ("replacement"));
    g_paste_history_add (history, g_object_ref (password));
    g_paste_clipboard_content_set_text (&clipboard->content, "previous copy");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));

    GPasteClipboardUpdate *pending = clipboard->pending;

    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (password), 1);
    g_paste_clipboards_manager_rearm_password (manager, password);
    text_ready (clipboard, settings, pending, "same secret");
    g_paste_clipboard_update_maybe_done (pending);

    /* Armed, not published: the value is already on the selection. */
    g_assert_cmpstr (clipboard->content.str, ==, "same secret");

    pump_for (1100);
    g_assert_cmpstr (clipboard->content.str, ==, "replacement");
}


/* A failed read leaves the exposure recorded but its cache unknown. The next
 * ordinary-text read of the same value must create an item, which can evict the
 * password and synchronously replace the source cache during history_add (). */
static void
test_sync_password_eviction (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "secret value", 0);

    g_paste_settings_set_max_history_size (settings, 5);
    g_paste_settings_set_synchronize_clipboards (settings, TRUE);
    clipboard->is_clipboard = TRUE;
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    g_paste_history_add (history, g_object_ref (password));
    g_paste_clipboards_manager_select (manager, password);

    for (guint i = 0; i < 4; ++i)
    {
        g_autofree gchar *value = g_strdup_printf ("filler %u", i);

        g_paste_history_add (history, g_paste_text_item_new (value));
    }

    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *pending = clipboard->pending;

    g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);
    g_paste_clipboard_update_maybe_done (pending);
    g_paste_clipboard_update_maybe_done (pending);
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    pending = clipboard->pending;
    text_ready (clipboard, settings, pending, "secret value");
    g_paste_clipboard_update_maybe_done (pending);

    g_assert_null (g_paste_history_get_by_uuid (history, g_paste_item_get_uuid (password)));
    g_assert_cmpstr (clipboard->content.str, ==, "secret value");
    g_assert_cmpstr (primary->content.str, ==, "secret value");
    g_assert_true (G_PASTE_IS_TEXT_ITEM (clipboard->published));
    g_assert_true (G_PASTE_IS_TEXT_ITEM (primary->published));
}

static void
test_dropped_password_distinct_entry (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GPasteItem) first = g_paste_password_item_new ("first", "same value", 0);
    g_autoptr (GPasteItem) second = g_paste_password_item_new ("second", "same value", 0);

    g_paste_history_add (history, g_object_ref (first));
    g_paste_history_add (history, g_object_ref (second));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_select (manager, second);
    g_paste_history_add (history, g_paste_text_item_new ("unrelated"));

    guint publications = clipboard->publications;

    g_assert_true (g_paste_history_remove_by_uuid (history, g_paste_item_get_uuid (first)));
    g_assert_cmpstr (clipboard->content.str, ==, "same value");
    g_assert_cmpuint (clipboard->publications, ==, publications);

    /* Removing the selected entry itself must still clear its exposure. */
    g_assert_true (g_paste_history_remove_by_uuid (history, g_paste_item_get_uuid (second)));
    g_assert_cmpstr (clipboard->content.str, ==, "unrelated");
    g_assert_cmpuint (clipboard->publications, ==, publications + 1);
}

static void
test_strip_after_unconfirmed_read (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "rich text", 0);
    GPasteItem *rich = g_paste_text_item_new ("rich text");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>rich text</b>", 16)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));

    g_paste_history_add (history, rich);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    g_paste_clipboards_manager_select (manager, password);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *pending = clipboard->pending;

    g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);
    g_paste_clipboard_update_maybe_done (pending);
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_null (clipboard->pending);

    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .clipboards_manager = manager };
    g_autoptr (GError) error = NULL;
    guint publications = clipboard->publications;

    g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
    g_assert_no_error (error);
    g_assert_cmpuint (clipboard->publications, ==, publications);

    /* A strip asks for the missing identity without waiting for another copy. */
    g_assert_nonnull (clipboard->pending);
    pending = clipboard->pending;
    text_ready (clipboard, settings, pending, "rich text");
    pending->mimes.special_mime[G_PASTE_SPECIAL_MIME_TEXT_HTML] = g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                                           g_bytes_new_static ("<b>rich text</b>", 16));
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpuint (clipboard->publications, ==, publications + 1);
    g_assert_null (g_paste_item_get_special_values (clipboard->published));
    g_assert_null (g_paste_item_get_special_values (g_paste_history_get_by_uuid (history, uuid)));
}

/* An empty marked payload has no history entry, but its marker still forbids a
 * bare-text sync. Ordinary content subsequently read must retire that state. */
static void
test_empty_sensitive_selection (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_clipboard_content_set_text (&primary->content, "destination");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *pending = clipboard->pending;

    g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT),
                                           g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    text_ready (clipboard, settings, pending, "");
    g_paste_clipboard_update_maybe_done (pending);
    g_paste_clipboards_manager_sync_from_to (manager, TRUE);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);
    g_assert_cmpuint (clipboard->sync_reads, ==, 0);
    g_assert_cmpstr (primary->content.str, ==, "destination");

    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    pending = clipboard->pending;
    text_ready (clipboard, settings, pending, "ordinary text");
    g_paste_clipboard_update_maybe_done (pending);
    g_paste_clipboards_manager_sync_from_to (manager, TRUE);
    g_assert_cmpuint (clipboard->sync_reads, ==, 1);
    g_assert_cmpstr (primary->content.str, ==, "ordinary text");
}

static void
test_dropped_password_merge_twin (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    gboolean rename = variant != 0;
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GPasteItem) named = g_paste_password_item_new ("bank", "same value", 0);
    g_autoptr (GPasteItem) anonymous = g_paste_password_item_new (NULL, "same value", 0);

    g_paste_settings_set_synchronize_clipboards (settings, FALSE);
    g_paste_history_add (history, g_object_ref (named));
    g_paste_history_add (history, g_object_ref (anonymous));
    clipboard->is_clipboard = TRUE;
    clipboard->defer_reads = variant == 2;
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;
    if (variant != 2)
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *pending = clipboard->pending;

    g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT),
                                           g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    text_ready (clipboard, settings, pending, "same value");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_true (g_paste_history_get (history, 0) == anonymous);

    guint publications = clipboard->publications;

    g_paste_history_remove_by_uuid (history, g_paste_item_get_uuid (named));
    g_assert_cmpuint (clipboard->publications, ==, publications);
    g_assert_cmpstr (clipboard->content.str, ==, "same value");
    if (rename)
    {
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        pending = clipboard->pending;
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);
        g_paste_clipboard_update_maybe_done (pending);
        g_paste_clipboard_update_maybe_done (pending);
        g_autofree gchar *renamed = g_paste_history_make_password (history, g_paste_item_get_uuid (anonymous), "captured", 0);

        g_assert_nonnull (renamed);
        g_paste_clipboards_manager_rearm_password (manager, anonymous);
    }
    g_paste_history_remove_by_uuid (history, g_paste_item_get_uuid (anonymous));
    if (rename)
    {
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        pending = clipboard->pending;
        text_ready (clipboard, settings, pending, "same value");
        g_paste_clipboard_update_maybe_done (pending);
    }
    g_assert_cmpstr (clipboard->content.str, ==, "");
}

static void
test_merged_password_expiry (gconstpointer user_data)
{
    gboolean bootstrap = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_paste_settings_set_password_timeout (settings, bootstrap ? 0 : 1);
    g_paste_settings_set_synchronize_clipboards (settings, TRUE);
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GPasteItem) plain = g_paste_text_item_new ("fallback");
    g_autoptr (GPasteItem) password = g_paste_password_item_new (NULL, "secret", bootstrap ? 1 : 30);
    g_paste_history_add (history, g_object_ref (plain));
    g_paste_history_add (history, g_object_ref (password));
    clipboard->is_clipboard = TRUE;
    clipboard->defer_reads = bootstrap;
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;
    if (!bootstrap)
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    GPasteClipboardUpdate *pending = clipboard->pending;
    g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    text_ready (clipboard, settings, pending, "secret");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_true (g_paste_history_get (history, 0) == password);
    g_assert_cmpuint (g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (password)), ==, 1);
    if (bootstrap)
        g_paste_clipboards_manager_sync_from_to (manager, TRUE);
    g_assert_true (primary->published == password);
    pump_for (1100);
    g_assert_true (g_paste_history_get (history, 0) == plain);
    g_assert_cmpstr (clipboard->content.str, ==, "fallback");
    g_assert_cmpstr (primary->content.str, ==, "fallback");
}

typedef enum
{
    BOOTSTRAP_BEFORE_LOAD_PLAIN,
    /* Something is added while the load is still in flight. */
    BOOTSTRAP_BEFORE_LOAD_ADD_DURING_LOAD,
    /* A later read leaves the capture's record unconfirmed before the load. */
    BOOTSTRAP_BEFORE_LOAD_UNCONFIRMED,
    /* The same, with the load finishing after the stored timeout has run out. */
    BOOTSTRAP_BEFORE_LOAD_OVERDUE,
} BootstrapBeforeLoad;

/* A bootstrap read that concludes before the history has loaded finds no head to
 * link its capture to; the link is made once the history has loaded, and the
 * countdown then runs for the timeout stored on that entry -- counted from the
 * capture -- rather than for password-timeout (see link_captures ()). The head
 * arriving is an add here, which raises the same update a load does; the load
 * itself, bringing no head, leaves the link waiting.
 *
 * add_during_load: an add made while the load is in flight shows a head the
 * load then throws away, and must not spend the link. The load runs on a
 * context of its own, so that the manager's idle runs while it is still
 * loading, deterministically.
 *
 * unconfirmed: linking reads nothing, so a record a later read left unconfirmed
 * stays so, and a sync out of that selection is still refused.
 *
 * overdue: a link landing past the stored deadline is that countdown running
 * out, and asks for the read that identifies the unconfirmed selection, as the
 * countdown itself would; that read then takes the password off. */
static void
test_bootstrap_before_load (gconstpointer user_data)
{
    BootstrapBeforeLoad variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_NOOP);
    g_paste_settings_set_password_timeout (settings, 0);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    GBytes *hint = g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT);

    clipboard->is_clipboard = TRUE;
    clipboard->defer_reads = TRUE;
    g_paste_clipboard_content_set_text (&primary->content, "destination");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);

    GPasteClipboardUpdate *pending = clipboard->pending;

    g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), hint);
    text_ready (clipboard, settings, pending, "secret");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_cmpuint (g_paste_history_get_length (history), ==, 0);

    gboolean unconfirmed = (variant == BOOTSTRAP_BEFORE_LOAD_UNCONFIRMED || variant == BOOTSTRAP_BEFORE_LOAD_OVERDUE);

    if (unconfirmed)
    {
        /* The hint is offered and not served, and both text replies fail. */
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        pending = clipboard->pending;
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), NULL);
        g_paste_clipboard_update_maybe_done (pending);
        g_paste_clipboard_update_maybe_done (pending);
        g_assert_null (clipboard->pending);
    }

    if (variant == BOOTSTRAP_BEFORE_LOAD_OVERDUE)
        pump_for (1100);

    if (variant == BOOTSTRAP_BEFORE_LOAD_ADD_DURING_LOAD)
    {
        g_autoptr (GMainContext) loading = g_main_context_new ();

        g_main_context_push_thread_default (loading);
        g_paste_history_load_async (history, "clipboard-tests");
        g_main_context_pop_thread_default (loading);

        /* Added under the load, and the manager's idle run while it still is. */
        g_paste_history_add (history, g_paste_text_item_new ("thrown away by the load"));
        pump_for (10);
        g_assert_true (g_paste_history_is_loading (history));

        while (g_paste_history_is_loading (history))
            g_main_context_iteration (loading, TRUE);
    }
    else
    {
        /* The noop backend reads nothing back, so the head is added the moment
         * after. */
        g_paste_history_load (history, "clipboard-tests");
    }

    g_paste_history_add (history, g_paste_password_item_new (NULL, "secret", 1));

    if (variant == BOOTSTRAP_BEFORE_LOAD_OVERDUE)
    {
        pump_for (10);
        pending = clipboard->pending;
        g_assert_nonnull (pending);
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), hint);
        text_ready (clipboard, settings, pending, "secret");
        g_paste_clipboard_update_maybe_done (pending);
        g_assert_cmpstr (clipboard->content.str, ==, "");
        return;
    }

    if (variant == BOOTSTRAP_BEFORE_LOAD_UNCONFIRMED)
    {
        pump_for (10);
        g_paste_clipboards_manager_sync_from_to (manager, TRUE);
        g_assert_cmpuint (clipboard->sync_reads, ==, 0);
        g_assert_cmpstr (primary->content.str, ==, "destination");
        return;
    }

    /* Taken off at the stored timeout, with nothing else in the history to put
     * there instead. */
    pump_for (1100);
    g_assert_cmpstr (clipboard->content.str, ==, "");
}

/* A bootstrap read concluding while the history is still loading is linked once
 * it has loaded, even when something was added in the meantime: the head it
 * sees then is one the load throws away, not the one to link against (see
 * identify_ready ()). The load runs on a context of its own, so that it is
 * still in flight when the read concludes. */
static void
test_bootstrap_during_load (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();

    g_paste_settings_set_storage_backend (settings, G_PASTE_STORAGE_NOOP);
    g_paste_settings_set_password_timeout (settings, 0);
    g_paste_settings_set_synchronize_clipboards (settings, FALSE);

    g_autoptr (GPasteHistory) history = g_paste_history_new (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);
    g_autoptr (GMainContext) loading = g_main_context_new ();

    g_main_context_push_thread_default (loading);
    g_paste_history_load_async (history, "clipboard-tests");
    g_main_context_pop_thread_default (loading);
    g_paste_history_add (history, g_paste_text_item_new ("thrown away by the load"));

    clipboard->is_clipboard = TRUE;
    clipboard->defer_reads = TRUE;
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_activate (manager);

    GPasteClipboardUpdate *pending = clipboard->pending;

    g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT),
                                           g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    text_ready (clipboard, settings, pending, "secret");
    g_paste_clipboard_update_maybe_done (pending);
    pump_for (10);
    g_assert_true (g_paste_history_is_loading (history));

    while (g_paste_history_is_loading (history))
        g_main_context_iteration (loading, TRUE);

    g_paste_history_add (history, g_paste_password_item_new (NULL, "secret", 1));

    pump_for (1100);
    g_assert_cmpstr (clipboard->content.str, ==, "");
}

static void
test_sensitive_followup (gconstpointer user_data)
{
    gboolean sync = GPOINTER_TO_INT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = make_clipboard (settings);
    g_autoptr (TestClipboard) primary = make_clipboard (settings);
    g_autoptr (GPasteClipboardsManager) manager = g_paste_clipboards_manager_new (history, settings);

    clipboard->is_clipboard = TRUE;
    g_paste_settings_set_synchronize_clipboards (settings, sync);
    g_paste_clipboard_content_set_text (&primary->content, "destination");
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (clipboard));
    g_paste_clipboards_manager_add_clipboard (manager, G_PASTE_CLIPBOARD_PROVIDER (primary));
    g_paste_clipboards_manager_activate (manager);
    clipboard->defer_reads = TRUE;

    /* A matching hint without text establishes sensitivity; a later text read
     * with no hint answer cannot retire it. */
    for (guint i = 0; i < 2; ++i)
    {
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        GPasteClipboardUpdate *pending = clipboard->pending;
        g_paste_clipboard_update_on_mime_read (g_paste_clipboard_update_add_sensitive_mime_read (pending, G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT),
                                               (i) ? NULL : g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
        if (i)
        {
            text_ready (clipboard, settings, pending, "secret text");
            pending->mimes.special_mime[G_PASTE_SPECIAL_MIME_TEXT_HTML] = g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                                                   g_bytes_new_static ("<b>secret text</b>", 18));
        }
        else
            g_paste_clipboard_update_maybe_done (pending);
        g_paste_clipboard_update_maybe_done (pending);
    }

    if (sync)
    {
        g_assert_cmpstr (primary->content.str, ==, "destination");
        g_assert_cmpuint (primary->publications, ==, 0);
        g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (clipboard));
        GPasteClipboardUpdate *pending = clipboard->pending;

        text_ready (clipboard, settings, pending, "ordinary text");
        g_paste_clipboard_update_maybe_done (pending);
        g_assert_cmpstr (primary->content.str, ==, "ordinary text");
        return;
    }

    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (g_paste_history_get (history, 0)));
    const GPasteDaemonMethods methods = { .history = history, .settings = settings, .clipboards_manager = manager };
    g_autoptr (GError) error = NULL;

    g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
    g_assert_no_error (error);
    g_assert_nonnull (clipboard->pending);
    GPasteClipboardUpdate *pending = clipboard->pending;

    text_ready (clipboard, settings, pending, "secret text");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_nonnull (clipboard->published);
    g_assert_null (g_paste_item_get_special_values (clipboard->published));
}

static void
test_queued_strip (gconstpointer user_data)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (0);
    GPasteItem *rich = g_paste_text_item_new ("rich text");

    g_paste_item_add_special_value (rich, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML,
                                                                   g_bytes_new_static ("<b>rich text</b>", 16)));
    g_autofree gchar *uuid = g_strdup (g_paste_item_get_uuid (rich));

    g_paste_history_add (fixture->history, rich);
    fail_password_read (fixture);

    const GPasteDaemonMethods methods = {
        .history = fixture->history,
        .settings = fixture->settings,
        .clipboards_manager = fixture->manager,
    };
    g_autoptr (GError) error = NULL;

    for (guint i = 0; i < 100; ++i)
    {
        g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
        g_assert_no_error (error);
    }

    g_assert_nonnull (fixture->clipboard->pending);
    fail_password_read (fixture);

    /* A burst after an unanswered retry shares that attempt. */
    for (guint i = 0; i < 100; ++i)
    {
        g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
        g_assert_no_error (error);
        g_assert_null (fixture->clipboard->pending);
    }

    if (GPOINTER_TO_INT (user_data) != 1)
    {
        if (GPOINTER_TO_INT (user_data) == 2)
        {
            pump_for (1100);
            g_paste_daemon_methods_strip_rich_text (&methods, uuid, &error);
            g_assert_no_error (error);
            g_assert_nonnull (fixture->clipboard->pending);
        }
        else
            g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (fixture->clipboard));
        password_read_ready (fixture, "rich text");
        g_assert_nonnull (fixture->clipboard->published);
        g_assert_cmpstr (g_paste_item_get_uuid (fixture->clipboard->published), ==, uuid);
        g_assert_null (g_paste_item_get_special_values (fixture->clipboard->published));
        return;
    }

    gpointer plain = g_paste_history_get_by_uuid (fixture->history, uuid);

    g_object_add_weak_pointer (G_OBJECT (plain), &plain);
    g_paste_history_empty (fixture->history);
    g_paste_history_flush (fixture->history);
    g_assert_null (plain);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_data_func ("/clipboard/dropped_password/merge_twin", GINT_TO_POINTER (FALSE), test_dropped_password_merge_twin);
    g_test_add_data_func ("/clipboard/dropped_password/renamed_twin", GINT_TO_POINTER (TRUE), test_dropped_password_merge_twin);
    g_test_add_data_func ("/clipboard/dropped_password/bootstrap_twin", GUINT_TO_POINTER (2), test_dropped_password_merge_twin);
    g_test_add_data_func ("/clipboard/merged_password/expiry", GINT_TO_POINTER (FALSE), test_merged_password_expiry);
    g_test_add_data_func ("/clipboard/merged_password/bootstrap_expiry", GINT_TO_POINTER (TRUE), test_merged_password_expiry);
    g_test_add_data_func ("/clipboard/sensitive_followup/strip", GINT_TO_POINTER (FALSE), test_sensitive_followup);
    g_test_add_data_func ("/clipboard/sensitive_followup/sync", GINT_TO_POINTER (TRUE), test_sensitive_followup);
    g_test_add_data_func ("/clipboard/queued_strip/lifetime", GINT_TO_POINTER (TRUE), test_queued_strip);
    g_test_add_data_func ("/clipboard/queued_strip/retry", GINT_TO_POINTER (FALSE), test_queued_strip);
    g_test_add_data_func ("/clipboard/queued_strip/retry_unchanged_owner", GINT_TO_POINTER (2), test_queued_strip);
    g_test_add_func ("/clipboard/sync/password_eviction", test_sync_password_eviction);
    g_test_add_func ("/clipboard/empty_sensitive_selection", test_empty_sensitive_selection);
    g_test_add_func ("/clipboard/dropped_password/distinct_entry", test_dropped_password_distinct_entry);
    g_test_add_func ("/clipboard/strip_after_unconfirmed_read", test_strip_after_unconfirmed_read);
    g_test_add_func ("/clipboard/expiry/history_before_restart", test_restart_waits_for_history);
    g_test_add_func ("/clipboard/expiry/independent_deadlines", test_expiry_independent_deadlines);
    g_test_add_func ("/clipboard/expiry/pending_preserves_waiter", test_expiry_pending_preserves_other_waiter);
    g_test_add_func ("/clipboard/restore_rejected_head", test_restore_rejected_head);
    g_test_add_data_func ("/clipboard/sync/independent_item", GINT_TO_POINTER (TRUE), test_independent_publication_copy_order);
    g_test_add_data_func ("/clipboard/sync/independent_text", GINT_TO_POINTER (FALSE), test_independent_publication_copy_order);
    g_test_add_func ("/clipboard/sync/after_expiry_publication", test_sync_after_expiry_publication);
    g_test_add_func ("/clipboard/reassert_timeout", test_reassert_preserves_timeout);
    g_test_add_data_func ("/clipboard/edit_after_failure/shorten", GINT_TO_POINTER (FALSE), test_edit_after_failed_read);
    g_test_add_data_func ("/clipboard/edit_after_failure/cancel", GINT_TO_POINTER (TRUE), test_edit_after_failed_read);
    g_test_add_data_func ("/clipboard/sync_elapsed_edit/manual", GINT_TO_POINTER (FALSE), test_sync_finishes_elapsed_edit);
    g_test_add_data_func ("/clipboard/sync_elapsed_edit/automatic", GINT_TO_POINTER (TRUE), test_sync_finishes_elapsed_edit);
    g_test_add_func ("/clipboard/strip_idle_password", test_strip_preserves_idle_password);
    g_test_add_data_func ("/clipboard/unknown_hint/primary", GINT_TO_POINTER (FALSE), test_unknown_hint_text_policy);
    g_test_add_func ("/clipboard/unknown_hint/dropped", test_unknown_hint_dropped_text);
    g_test_add_data_func ("/clipboard/dropped_password/removed", GINT_TO_POINTER (FALSE), test_dropped_password);
    g_test_add_data_func ("/clipboard/dropped_password/emptied", GINT_TO_POINTER (TRUE), test_dropped_password);
    g_test_add_data_func ("/clipboard/unknown_hint/clipboard", GINT_TO_POINTER (TRUE), test_unknown_hint_text_policy);
    g_test_add_data_func ("/clipboard/automatic_head/removal", GUINT_TO_POINTER (0), test_automatic_head_skips_password);
    g_test_add_data_func ("/clipboard/automatic_head/refusal", GUINT_TO_POINTER (1), test_automatic_head_skips_password);
    g_test_add_data_func ("/clipboard/automatic_head/removed_password", GUINT_TO_POINTER (2), test_automatic_head_skips_password);
    g_test_add_func ("/clipboard/automatic_head/every_selection", test_automatic_head_clears_every_selection);
    g_test_add_data_func ("/clipboard/automatic_head/removed_only_password", GINT_TO_POINTER (TRUE), test_automatic_head_clears_removed_password);
    g_test_add_data_func ("/clipboard/automatic_head/removed_only_text", GINT_TO_POINTER (FALSE), test_automatic_head_clears_removed_password);
    g_test_add_data_func ("/clipboard/automatic_head/emptied_password", GINT_TO_POINTER (TRUE), test_emptied_history_clears_password);
    g_test_add_data_func ("/clipboard/automatic_head/emptied_text", GINT_TO_POINTER (FALSE), test_emptied_history_clears_password);
    g_test_add_func ("/clipboard/unrecorded_secret/reread_keeps_record", test_unrecorded_secret_reread_keeps_the_record);
    g_test_add_data_func ("/clipboard/expiry_idle/unref", GINT_TO_POINTER (FALSE), test_expiry_idle_lifetime);
    g_test_add_data_func ("/clipboard/expiry_idle/dispose", GINT_TO_POINTER (TRUE), test_expiry_idle_lifetime);
    g_test_add_data_func ("/clipboard/hint_reply/known_secret_unanswered", GINT_TO_POINTER (FALSE), test_known_secret_failed_hint);
    g_test_add_data_func ("/clipboard/hint_reply/known_secret_uncached", GINT_TO_POINTER (TRUE), test_known_secret_failed_hint);
    g_test_add_func ("/clipboard/timeout_edit/hinted_read", test_timeout_edit_hinted_read);
    g_test_add_data_func ("/clipboard/unconfirmed_password/shutdown", GINT_TO_POINTER (FALSE), test_unconfirmed_password_shutdown);
    g_test_add_data_func ("/clipboard/unconfirmed_password/shutdown_reread", GINT_TO_POINTER (TRUE), test_unconfirmed_password_shutdown);
    g_test_add_data_func ("/clipboard/unconfirmed_password/overdue_reread", GINT_TO_POINTER (TRUE), test_overdue_password_is_reread);
    g_test_add_data_func ("/clipboard/unconfirmed_password/overdue_unanswered", GINT_TO_POINTER (FALSE), test_overdue_password_is_reread);
    g_test_add_func ("/clipboard/unconfirmed_password/reread_is_not_a_copy", test_unconfirmed_reread_is_not_a_copy);
    g_test_add_func ("/clipboard/unconfirmed_password/reread_keeps_record", test_unconfirmed_reread_keeps_the_record);
    g_test_add_func ("/clipboard/unconfirmed_password/reread_retires_record", test_unconfirmed_reread_retires_the_record);
    g_test_add_func ("/clipboard/edit_before_failure/cancel", test_edit_before_failed_read);
    g_test_add_func ("/clipboard/forced_expiry/after_history_update", test_forced_expiry_after_history_update);
    g_test_add_func ("/clipboard/strip_during_read/growing_duplicate", test_strip_growing_duplicate);
    g_test_add_func ("/clipboard/timeout_edit/plain_selection", test_timeout_edit_plain_selection);
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
    g_test_add_data_func ("/clipboard/strip_during_read/trimmed", GUINT_TO_POINTER (5), test_strip_during_read);
    g_test_add_data_func ("/clipboard/strip_during_read/password", GUINT_TO_POINTER (6), test_strip_during_read);
    g_test_add_data_func ("/clipboard/strip_during_read/unknown_hint", GUINT_TO_POINTER (7), test_strip_during_read);
    g_test_add_data_func ("/clipboard/strip_during_read/retried_after_unknown_hint", GUINT_TO_POINTER (0), test_strip_retried_after_unknown_hint);
    g_test_add_data_func ("/clipboard/strip_during_read/dispose_refused", GUINT_TO_POINTER (1), test_strip_retried_after_unknown_hint);
    g_test_add_data_func ("/clipboard/strip_during_read/unanswered_retry", GUINT_TO_POINTER (3), test_strip_retried_after_unknown_hint);
    g_test_add_data_func ("/clipboard/strip_during_read/provider_refused", GUINT_TO_POINTER (2), test_strip_retried_after_unknown_hint);
    g_test_add_func ("/clipboard/strip_during_read/refused_publication", test_strip_survives_refused_publication);
    g_test_add_data_func ("/clipboard/image_cache", GINT_TO_POINTER (CLIPBOARD_CONTENT_IMAGE), test_typed_cache_commit);
    g_test_add_data_func ("/clipboard/color_cache", GINT_TO_POINTER (CLIPBOARD_CONTENT_COLOR), test_typed_cache_commit);
    g_test_add_data_func ("/clipboard/file_list_cache", GINT_TO_POINTER (CLIPBOARD_CONTENT_FILE_LIST), test_typed_cache_commit);
    for (guint variant = 0; variant < 3; ++variant)
    {
        g_autofree gchar *path = g_strdup_printf ("/clipboard/password_expiry_during_read/%u", variant);

        g_test_add_data_func (path, GUINT_TO_POINTER (variant), test_password_expiry_during_read);
    }
    for (guint i = 0; i < G_N_ELEMENTS (timeout_edits); ++i)
    {
        const TimeoutEditCase *test = &timeout_edits[i];
        g_autofree gchar *path = g_strdup_printf ("/clipboard/timeout_edit/%s", test->name);

        g_test_add_data_func (path, test, test_timeout_edit_during_read);
    }
    g_test_add_data_func ("/clipboard/reselect_overdue/one", GINT_TO_POINTER (FALSE), test_reselect_overdue_password);
    g_test_add_data_func ("/clipboard/reselect_overdue/two", GINT_TO_POINTER (TRUE), test_reselect_overdue_password);
    for (guint i = 0; i < 5; ++i)
    {
        g_autofree gchar *path = g_strdup_printf ("/clipboard/forced_expiry/%u", i);

        g_test_add_data_func (path, GUINT_TO_POINTER (i), test_forced_expiry);
    }
    g_test_add_func ("/clipboard/forced_expiry/both_selections", test_forced_expiry_waits_for_both_selections);
    g_test_add_data_func ("/clipboard/timeout_edit/publish_pending_deadline", GINT_TO_POINTER (FALSE), test_publish_pending_deadline);
    g_test_add_data_func ("/clipboard/timeout_edit/publish_pending_twin_deadline", GINT_TO_POINTER (TRUE), test_publish_pending_deadline);
    g_test_add_func ("/clipboard/timeout_edit/reenable_pending_timeout", test_reenable_pending_timeout);
    g_test_add_func ("/clipboard/timeout_edit/rename_twin", test_rename_twin_during_read);
    g_test_add_data_func ("/clipboard/forced_expiry/no_countdown/select", GINT_TO_POINTER (FALSE), test_forced_expiry_without_countdown);
    g_test_add_data_func ("/clipboard/forced_expiry/no_countdown/edit", GINT_TO_POINTER (TRUE), test_forced_expiry_without_countdown);
    g_test_add_func ("/clipboard/trimmed_duplicate", test_trimmed_duplicate);
    g_test_add_func ("/clipboard/sensitive_size_policy", test_sensitive_size_policy);
    g_test_add_func ("/clipboard/late_mime_read", test_late_mime_read);
    const gchar *outcomes[] = { "too_short", "too_long", "failed", "timed_out", "duplicate_outside_policy", "duplicate" };

    for (guint i = 0; i < 12; ++i)
    {
        g_autofree gchar *path = g_strdup_printf ("/clipboard/selection_identity/%s/%s", i >= 6 ? "expiry" : "strip", outcomes[i % 6]);

        g_test_add_data_func (path, GUINT_TO_POINTER (i), test_unidentified_selection);
    }
    g_test_add_func ("/clipboard/restart_completion_window", test_restart_completion_window);
    g_test_add_func ("/clipboard/stale_strip_after_publication", test_stale_strip_after_publication);
    g_test_add_data_func ("/clipboard/sync/older_first", GUINT_TO_POINTER (0), test_sync_copy_order);
    g_test_add_data_func ("/clipboard/sync/newer_first", GUINT_TO_POINTER (1), test_sync_copy_order);
    g_test_add_data_func ("/clipboard/sync/trimmed_older_first", GUINT_TO_POINTER (2), test_sync_copy_order);
    g_test_add_data_func ("/clipboard/sync/trimmed_newer_first", GUINT_TO_POINTER (3), test_sync_copy_order);
    g_test_add_func ("/clipboard/expiry/head_before_flush", test_expiry_retires_head_before_flush);
    g_test_add_func ("/clipboard/expiry/head_with_other_reading", test_expiry_retires_head_with_other_reading);
    g_test_add_func ("/clipboard/expiry/rechecks_new_read", test_expiry_rechecks_new_read);
    g_test_add_func ("/clipboard/expiry/changing_owner_deadline", test_expiry_deadline_with_changing_owner);
    for (guint variant = 0; variant < 8; ++variant)
    {
        g_autofree gchar *path = g_strdup_printf ("/clipboard/secret_text_policy/%u", variant);

        g_test_add_data_func (path, GUINT_TO_POINTER (variant), test_secret_text_policy);
    }
    g_test_add_data_func ("/clipboard/hint_reply/served_empty", GINT_TO_POINTER (TRUE), test_hint_reply_without_value);
    g_test_add_data_func ("/clipboard/hint_reply/refused", GINT_TO_POINTER (FALSE), test_hint_reply_without_value);
    g_test_add_func ("/clipboard/emptied_under_password", test_emptied_under_password);
    g_test_add_data_func ("/clipboard/follow_release/released", GUINT_TO_POINTER (FOLLOW_RELEASE_RELEASED), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/empty_secret", GUINT_TO_POINTER (FOLLOW_RELEASE_EMPTY_SECRET), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/empty_text", GUINT_TO_POINTER (FOLLOW_RELEASE_EMPTY_TEXT), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/replaced", GUINT_TO_POINTER (FOLLOW_RELEASE_REPLACED), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/new_text", GUINT_TO_POINTER (FOLLOW_RELEASE_NEW_TEXT), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/user_selected", GUINT_TO_POINTER (FOLLOW_RELEASE_USER_SELECTED), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/selected_after_sync", GUINT_TO_POINTER (FOLLOW_RELEASE_SELECTED_AFTER_SYNC), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/edited_timeout", GUINT_TO_POINTER (FOLLOW_RELEASE_EDITED_TIMEOUT), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/new_password_unsynced", GUINT_TO_POINTER (FOLLOW_RELEASE_NEW_PASSWORD_UNSYNCED), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/found_by_identifying", GUINT_TO_POINTER (FOLLOW_RELEASE_FOUND_BY_IDENTIFYING), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/owner_recopied", GUINT_TO_POINTER (FOLLOW_RELEASE_OWNER_RECOPIED), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/owner_same_value", GUINT_TO_POINTER (FOLLOW_RELEASE_OWNER_SAME_VALUE), test_follow_release);
    g_test_add_data_func ("/clipboard/follow_release/twin_edited", GUINT_TO_POINTER (FOLLOW_RELEASE_TWIN_EDITED), test_follow_release);
    g_test_add_data_func ("/clipboard/empty_text_retires_record/hinted", GUINT_TO_POINTER (EMPTY_TEXT_HINTED), test_empty_text_retires_record);
    g_test_add_data_func ("/clipboard/empty_text_retires_record/plain", GUINT_TO_POINTER (EMPTY_TEXT_PLAIN), test_empty_text_retires_record);
    g_test_add_data_func ("/clipboard/empty_text_retires_record/unserved_hint", GUINT_TO_POINTER (EMPTY_TEXT_UNSERVED_HINT), test_empty_text_retires_record);
    g_test_add_data_func ("/clipboard/bootstrap_before_load/plain", GUINT_TO_POINTER (BOOTSTRAP_BEFORE_LOAD_PLAIN), test_bootstrap_before_load);
    g_test_add_data_func ("/clipboard/bootstrap_before_load/add_during_load", GUINT_TO_POINTER (BOOTSTRAP_BEFORE_LOAD_ADD_DURING_LOAD), test_bootstrap_before_load);
    g_test_add_data_func ("/clipboard/bootstrap_before_load/unconfirmed", GUINT_TO_POINTER (BOOTSTRAP_BEFORE_LOAD_UNCONFIRMED), test_bootstrap_before_load);
    g_test_add_data_func ("/clipboard/bootstrap_before_load/overdue", GUINT_TO_POINTER (BOOTSTRAP_BEFORE_LOAD_OVERDUE), test_bootstrap_before_load);
    g_test_add_func ("/clipboard/bootstrap_before_load/identify_during_load", test_bootstrap_during_load);
    g_test_add_data_func ("/clipboard/unconfirmed_password/sync", GUINT_TO_POINTER (0), test_unconfirmed_password);
    g_test_add_data_func ("/clipboard/unconfirmed_password/expiry", GUINT_TO_POINTER (1), test_unconfirmed_password);
    g_test_add_data_func ("/clipboard/unconfirmed_password/secret_without_text/sync", GUINT_TO_POINTER (2), test_unconfirmed_password);
    g_test_add_data_func ("/clipboard/unconfirmed_password/secret_without_text/expiry", GUINT_TO_POINTER (3), test_unconfirmed_password);
    g_test_add_data_func ("/clipboard/sync_pending_text", GUINT_TO_POINTER (0), test_sync_during_classification);
    g_test_add_data_func ("/clipboard/sync_pending_secret", GUINT_TO_POINTER (1), test_sync_during_classification);
    g_test_add_data_func ("/clipboard/sync_pending_unknown", GUINT_TO_POINTER (2), test_sync_during_classification);
    g_test_add_data_func ("/clipboard/sync_source_changed", GINT_TO_POINTER (TRUE), test_stale_sync_reply);
    g_test_add_data_func ("/clipboard/sync_destination_changed", GINT_TO_POINTER (FALSE), test_stale_sync_reply);
    g_test_add_func ("/clipboard/sync_reply_after_trimmed_reown", test_sync_reply_after_trimmed_reown);
    return g_paste_test_env_run ();
}
