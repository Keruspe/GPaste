// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>
#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-password-item.h>

/* Drive the backend's actual ownership and read callbacks without a display or
 * a live clipboard. GTK transport is the only part replaced by this fixture. */
static GdkContentFormats *formats;
static GTask *pending_text;
static guint publications;

static GdkContentFormats *
mock_formats (GdkClipboard *clipboard G_GNUC_UNUSED)
{
    return formats;
}

static gboolean
mock_local (GdkClipboard *clipboard G_GNUC_UNUSED)
{
    return FALSE;
}

static gboolean
mock_set_content (GdkClipboard       *clipboard G_GNUC_UNUSED,
                  GdkContentProvider *content G_GNUC_UNUSED)
{
    ++publications;
    return TRUE;
}

static void
mock_read_text (GdkClipboard        *clipboard G_GNUC_UNUSED,
                GCancellable        *cancellable,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    g_assert_null (pending_text);
    pending_text = g_task_new (NULL, cancellable, callback, user_data);
}

static gchar *
mock_read_text_finish (GdkClipboard *clipboard G_GNUC_UNUSED,
                       GAsyncResult *result,
                       GError      **error)
{
    return g_task_propagate_pointer (G_TASK (result), error);
}

#define gdk_clipboard_get_formats mock_formats
#define gdk_clipboard_is_local mock_local
#define gdk_clipboard_set_content mock_set_content
#define gdk_clipboard_read_text_async mock_read_text
#define gdk_clipboard_read_text_finish mock_read_text_finish
#include "../../src/daemon/gpaste-clipboard-gdk.c"

typedef struct
{
    GPasteSettings          *settings;
    GPasteHistory           *history;
    GPasteClipboardsManager *manager;
    GPasteClipboardGdk      *clipboard;
    GPasteItem              *password;
    gboolean                 completed;
} Fixture;

static void
pump (guint milliseconds)
{
    gint64 end = g_get_monotonic_time () + milliseconds * G_TIME_SPAN_MILLISECOND;

    do
    {
        while (g_main_context_iteration (NULL, FALSE));
        g_usleep (1000);
    } while (g_get_monotonic_time () < end);
}

static void
set_formats (gboolean text)
{
    g_clear_pointer (&formats, gdk_content_formats_unref);
    formats = text ? gdk_content_formats_new_for_gtype (G_TYPE_STRING) : gdk_content_formats_new (NULL, 0);
}

static void
fixture_setup (Fixture      *f,
               gconstpointer user_data G_GNUC_UNUSED)
{
    f->settings = g_paste_settings_new ();
    g_paste_settings_set_storage_backend (f->settings, G_PASTE_STORAGE_NOOP);
    g_paste_settings_set_synchronize_clipboards (f->settings, FALSE);
    f->history = g_paste_history_new (f->settings);
    g_paste_history_load (f->history, "gdk-tests");
    g_paste_history_add (f->history, g_paste_text_item_new ("replacement"));
    f->manager = g_paste_clipboards_manager_new (f->history, f->settings);
    f->clipboard = g_object_new (G_PASTE_TYPE_CLIPBOARD_GDK, NULL);
    /* No real GdkClipboard or signal subscription; settings is borrowed here. */
    f->clipboard->settings = f->settings;
    f->clipboard->is_clipboard = TRUE;
    set_formats (FALSE);
    publications = 0;
    g_paste_clipboards_manager_add_clipboard (f->manager, G_PASTE_CLIPBOARD_PROVIDER (f->clipboard));
    g_paste_clipboards_manager_activate (f->manager);
    f->password = g_paste_password_item_new (NULL, "secret", 1);
}

static void
fixture_teardown (Fixture      *f,
                  gconstpointer user_data G_GNUC_UNUSED)
{
    g_paste_clipboards_manager_select (f->manager, g_paste_history_get (f->history, 0));
    if (pending_text)
    {
        g_task_return_pointer (pending_text, NULL, NULL);
        g_clear_object (&pending_text);
    }
    pump (10);
    g_clear_object (&f->manager);
    f->clipboard->settings = NULL;
    g_clear_object (&f->clipboard);
    g_clear_object (&f->password);
    g_clear_object (&f->history);
    g_clear_object (&f->settings);
    g_clear_pointer (&formats, gdk_content_formats_unref);
}

static void
expiry_during_formats (Fixture      *f,
                       gconstpointer user_data)
{
    gboolean same_password = GPOINTER_TO_INT (user_data);

    g_paste_clipboards_manager_select (f->manager, f->password);
    guint before = publications;

    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    pump (1100);
    g_assert_cmpuint (publications, ==, before);
    g_assert_true (g_paste_clipboard_gdk_is_reading (f->clipboard));
    g_assert_null (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)));
    g_assert_false (g_paste_clipboard_provider_is_empty (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)));

    set_formats (TRUE);
    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    g_assert_nonnull (pending_text);
    if (same_password)
        f->clipboard->update->mimes.sensitive_unknown = TRUE;
    g_task_return_pointer (pending_text, g_strdup (same_password ? "secret" : "new copy"), g_free);
    g_clear_object (&pending_text);
    pump (10);

    g_assert_false (g_paste_clipboard_gdk_is_reading (f->clipboard));
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)), ==, same_password ? "replacement" : "new copy");
    g_assert_cmpuint (publications, ==, before + (same_password ? 1 : 0));
}

/* The read the manager bootstraps a selection with finds a GdkClipboard's
 * formats still empty, GDK filling them in once its own TARGETS conversion
 * returns, and the change that raises is the answer to that read and not a new
 * owner: what was already on the selection is identified and nothing enters the
 * history (see on_real_changed ()).
 *
 * With @user_data, a new owner claims the selection before the targets arrive:
 * its empty formats come with a change, and the targets after them are a copy
 * like any other. */
static void
formats_bootstrap (Fixture      *f,
                   gconstpointer user_data)
{
    gboolean copy = GPOINTER_TO_INT (user_data);

    g_assert_true (g_paste_clipboard_gdk_is_reading (f->clipboard));

    if (copy)
        g_paste_clipboard_gdk_on_real_changed (f->clipboard);

    set_formats (TRUE);
    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    g_assert_nonnull (pending_text);
    g_task_return_pointer (pending_text, g_strdup ("already there"), g_free);
    g_clear_object (&pending_text);
    pump (10);

    g_assert_false (g_paste_clipboard_gdk_is_reading (f->clipboard));
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)), ==, "already there");
    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, (copy) ? 2 : 1);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (f->history, 0)), ==, (copy) ? "already there" : "replacement");
}

static void
on_expiry_done (GObject      *source G_GNUC_UNUSED,
                GAsyncResult *result,
                gpointer      user_data)
{
    g_autoptr (GError) error = NULL;

    Fixture *f = user_data;

    g_assert_true (g_paste_clipboards_manager_expire_password_finish (f->manager, result, &error));
    g_assert_no_error (error);
    f->completed = TRUE;
}

/* No formats by the deadline is a release, or an owner nothing can paste from:
 * the record is retired, so expiry -- and a re-exec waiting on it -- goes
 * through rather than being refused until the next copy.
 *
 * And the selection is empty from there on, so the history's head goes back on
 * it. That restoration is why closing the application you copied from does not
 * lose the copy, and it is what the mutter backend does on a release too: what
 * the wait buys is holding it back until empty formats are known not to be a new
 * owner still publishing its targets -- never dropping it. Leaving the cache
 * ignored here instead would leave the selection empty for the session. */
static void
formats_deadline (Fixture      *f,
                  gconstpointer user_data G_GNUC_UNUSED)
{
    /* Even bootstrap cannot infer an empty selection from missing formats. */
    g_assert_cmpuint (publications, ==, 0);
    g_paste_clipboards_manager_select (f->manager, f->password);
    guint before = publications;

    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    g_paste_clipboards_manager_expire_password_async (f->manager, on_expiry_done, f);
    GPasteClipboardGdkFormatsWait *wait = f->clipboard->formats_wait;

    /* Nothing goes back on while the wait is out: the formats may still arrive,
     * and the copy being negotiated is not ours to overwrite. */
    g_assert_true (g_paste_clipboard_gdk_is_reading (f->clipboard));
    g_assert_cmpuint (publications, ==, before);

    g_source_remove (wait->source_id);
    g_paste_clipboard_gdk_formats_wait_timed_out (wait);
    pump (10);
    g_assert_true (f->completed);
    g_assert_false (g_paste_clipboard_gdk_is_reading (f->clipboard));

    /* The head is back on the selection, which is why it is not empty. */
    g_assert_cmpuint (publications, ==, before + 1);
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)), ==, "replacement");
    g_assert_false (g_paste_clipboard_provider_is_empty (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)));

    /* And the next request finds nothing left to wait on. */
    f->completed = FALSE;
    g_paste_clipboards_manager_expire_password_async (f->manager, on_expiry_done, f);
    pump (10);
    g_assert_true (f->completed);
    g_assert_cmpuint (publications, ==, before + 1);
}

/* The wait holds its provider weakly: dropping the last reference disposes it
 * at once, and the superseded answer still releases the caller's state. */
static void
on_disposed_wait (GPasteClipboardProvider *provider,
                  GPasteItem              *item,
                  gboolean                 superseded,
                  GPasteClipboardSecret    secret G_GNUC_UNUSED,
                  gpointer                 user_data)
{
    g_assert_null (provider);
    g_assert_null (item);
    g_assert_true (superseded);
    *(gboolean *) user_data = TRUE;
}

static void
formats_wait_dispose (void)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    GPasteClipboardGdk *clipboard = g_object_new (G_PASTE_TYPE_CLIPBOARD_GDK, NULL);
    GWeakRef weak;
    gboolean answered = FALSE;

    set_formats (FALSE);
    clipboard->settings = settings;
    g_weak_ref_init (&weak, clipboard);
    g_paste_clipboard_gdk_update (clipboard, on_disposed_wait, &answered);
    g_assert_nonnull (clipboard->formats_wait);

    /* No real GdkClipboard to disconnect from: dispose () the way it runs, minus
     * the part that needs one. */
    g_paste_clipboard_gdk_supersede_formats_wait (clipboard);
    clipboard->settings = NULL;
    g_object_unref (clipboard);
    g_assert_null (g_weak_ref_get (&weak));

    pump (10);
    g_assert_true (answered);
    g_weak_ref_clear (&weak);
    g_clear_pointer (&formats, gdk_content_formats_unref);
}

static void
stale_read_and_publication (Fixture      *f,
                            gconstpointer user_data G_GNUC_UNUSED)
{
    set_formats (TRUE);
    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    set_formats (FALSE);
    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    g_task_return_pointer (pending_text, g_strdup ("stale copy"), g_free);
    g_clear_object (&pending_text);
    pump (10);
    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 1);
    g_assert_true (g_paste_clipboard_gdk_is_reading (f->clipboard));

    g_paste_clipboards_manager_select (f->manager, f->password);
    g_assert_false (g_paste_clipboard_gdk_is_reading (f->clipboard));
    pump (10);
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)), ==, "secret");
}

int
main (int argc, char **argv)
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, G_TEST_OPTION_ISOLATE_DIRS, NULL);
    g_test_add ("/gdk/formats/new-copy", Fixture, GINT_TO_POINTER (FALSE), fixture_setup, expiry_during_formats, fixture_teardown);
    g_test_add ("/gdk/formats/same-password", Fixture, GINT_TO_POINTER (TRUE), fixture_setup, expiry_during_formats, fixture_teardown);
    g_test_add ("/gdk/formats/deadline", Fixture, NULL, fixture_setup, formats_deadline, fixture_teardown);
    g_test_add ("/gdk/formats/stale-read-publication", Fixture, NULL, fixture_setup, stale_read_and_publication, fixture_teardown);
    g_test_add ("/gdk/formats/bootstrap", Fixture, GINT_TO_POINTER (FALSE), fixture_setup, formats_bootstrap, fixture_teardown);
    g_test_add ("/gdk/formats/bootstrap-then-copy", Fixture, GINT_TO_POINTER (TRUE), fixture_setup, formats_bootstrap, fixture_teardown);
    g_test_add_func ("/gdk/formats/dispose", formats_wait_dispose);
    return g_paste_test_env_run ();
}
