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

    GPasteClipboardContent content;
    gboolean               is_clipboard;
    GPasteItem            *published;
    guint                  publications;
    GPasteClipboardUpdate *pending;
    gboolean               defer_reads;
    gboolean               reject_next;
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

    if (self->reject_next)
    {
        self->reject_next = FALSE;
        return FALSE;
    }

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
test_clipboard_iface_init (GPasteClipboardProviderInterface *iface)
{
    iface->is_clipboard = is_clipboard;
    iface->get_text = get_text;
    iface->get_image_checksum = get_image_checksum;
    iface->is_reading = is_reading;
    iface->is_empty = is_empty;
    iface->update = update;
    iface->select_item = select_item;
    iface->select_text = select_text;
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

        if (action == G_PASTE_CLIPBOARD_TEXT_UNCHANGED)
            pending->unchanged = TRUE;
        else if (action != G_PASTE_CLIPBOARD_TEXT_REJECT)
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
    g_autoptr (GPasteSettings) settings = make_settings ();
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
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (clipboard)), ==, "copied text");

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
    g_autoptr (GPasteSettings) settings = make_settings ();
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
    gboolean refreshed = variant != 0 && variant != 3 && variant != 4;

    if (variant == 5)
        g_paste_settings_set_trim_items (settings, TRUE);
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

static void
test_password_expiry_during_read (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (GPasteSettings) settings = make_settings ();
    g_autoptr (GPasteHistory) history = make_history (settings);
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
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
    fixture->clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
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
 * so such an assertion is skipped rather than failed once the deadline is gone. */
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
    g_autoptr (TestClipboard) primary = g_object_new (TEST_TYPE_CLIPBOARD, NULL);

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
    g_autoptr (TestClipboard) primary = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
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
test_publish_pending_deadline (void)
{
    g_autoptr (PasswordFixture) fixture = password_fixture_new (37);

    g_paste_password_item_set_timeout (G_PASTE_PASSWORD_ITEM (fixture->password), 1);
    g_paste_clipboards_manager_rearm_password (fixture->manager, fixture->password);
    pump_for (1100);

    GPasteClipboardUpdate *pending = fixture->clipboard->pending;

    g_paste_clipboards_manager_select (fixture->manager, fixture->password);
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

static void
test_trimmed_duplicate (void)
{
    g_autoptr (GPasteSettings) settings = make_settings ();
    GPasteClipboardContent content = { 0 };
    g_autofree gchar *value = NULL;

    g_paste_settings_set_trim_items (settings, TRUE);
    g_paste_clipboard_content_set_text (&content, " text ");
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, TRUE, " text ", &value), ==, G_PASTE_CLIPBOARD_TEXT_RESELECT);
    g_assert_cmpstr (value, ==, "text");
    g_clear_pointer (&value, g_free);

    g_paste_clipboard_content_set_text (&content, "text");
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, TRUE, " text ", &value), ==, G_PASTE_CLIPBOARD_TEXT_RESELECT);
    g_assert_cmpstr (value, ==, "text");
    g_clear_pointer (&value, g_free);

    /* The primary selection is never re-owned trimmed, so its padded text read
     * again is still the duplicate of the trimmed value the cache holds. */
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, " text ", &value), ==, G_PASTE_CLIPBOARD_TEXT_UNCHANGED);
    g_assert_null (value);

    g_paste_clipboard_content_set_text (&content, " text ");
    g_assert_cmpint (g_paste_clipboard_content_classify_text (&content, settings, FALSE, " text ", &value), ==, G_PASTE_CLIPBOARD_TEXT_UNCHANGED);
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
        /* An exact duplicate remains identifiable even outside the size policy. */
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
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
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
    g_autoptr (TestClipboard) primary = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);

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
     * completed copies are retained, but capture of every owner is not promised. */
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
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
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
    g_autoptr (TestClipboard) primary = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
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
    g_autoptr (TestClipboard) primary = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
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
capture_expiry (GObject      *source G_GNUC_UNUSED,
                GAsyncResult *result,
                gpointer      user_data)
{
    GAsyncResult **captured = user_data;

    *captured = g_object_ref (result);
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
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
    g_autoptr (TestClipboard) primary = g_object_new (TEST_TYPE_CLIPBOARD, NULL);

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
    g_autoptr (TestClipboard) clipboard = g_object_new (TEST_TYPE_CLIPBOARD, NULL);
    g_autoptr (TestClipboard) primary = g_object_new (TEST_TYPE_CLIPBOARD, NULL);

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
     * select writes both and supersedes that read, masking an incorrect serial. */
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

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add_func ("/clipboard/expiry/history_before_restart", test_restart_waits_for_history);
    g_test_add_func ("/clipboard/expiry/independent_deadlines", test_expiry_independent_deadlines);
    g_test_add_func ("/clipboard/expiry/pending_preserves_waiter", test_expiry_pending_preserves_other_waiter);
    g_test_add_func ("/clipboard/restore_rejected_head", test_restore_rejected_head);
    g_test_add_data_func ("/clipboard/sync/independent_item", GINT_TO_POINTER (TRUE), test_independent_publication_copy_order);
    g_test_add_data_func ("/clipboard/sync/independent_text", GINT_TO_POINTER (FALSE), test_independent_publication_copy_order);
    g_test_add_func ("/clipboard/sync/after_expiry_publication", test_sync_after_expiry_publication);
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
    g_test_add_func ("/clipboard/timeout_edit/publish_pending_deadline", test_publish_pending_deadline);
    g_test_add_func ("/clipboard/timeout_edit/reenable_pending_timeout", test_reenable_pending_timeout);
    g_test_add_func ("/clipboard/trimmed_duplicate", test_trimmed_duplicate);
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
    return g_paste_test_env_run ();
}
