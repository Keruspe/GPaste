// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-test-env.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>
#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-sensitive-mime.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-password-item.h>

#include "../../src/daemon/gpaste-clipboard-x11-hints.h"

/* Drive the backend's actual ownership and read callbacks without a display or
 * a live clipboard. GTK transport is the only part replaced by this fixture. */
static GdkContentFormats *formats;
static GTask *pending_text;
static guint publications;
/* What the hints workaround answers here: whether it can convert this display at
 * all, and what X says about the selection's owner. The two are independent --
 * the atoms the ownership query needs are interned ahead of the XFixes check
 * available () answers from, so a display with no XFixes still names owners. */
static gboolean hints_available;
static GPasteClipboardX11HintsOwner hints_owner;
/* The mimetype the last MIME read asked for, %NULL when none did. */
static gchar *requested_mime;

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

/* One MIME read, answered with the hint's own value: what a selection offering
 * the hint in its formats serves. The mimetype asked for is kept, that being what
 * says which list the read came from. */
static void
mock_read_mime (GdkClipboard        *clipboard G_GNUC_UNUSED,
                const char         **mime_types,
                int                  io_priority G_GNUC_UNUSED,
                GCancellable        *cancellable,
                GAsyncReadyCallback  callback,
                gpointer             user_data)
{
    g_autoptr (GTask) task = g_task_new (NULL, cancellable, callback, user_data);

    g_set_str (&requested_mime, mime_types[0]);
    g_task_return_pointer (task,
                           g_memory_input_stream_new_from_bytes (g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT)),
                           g_object_unref);
}

static GInputStream *
mock_read_mime_finish (GdkClipboard  *clipboard G_GNUC_UNUSED,
                       GAsyncResult  *result,
                       const char   **out_mime_type,
                       GError       **error)
{
    *out_mime_type = requested_mime;

    return g_task_propagate_pointer (G_TASK (result), error);
}

/* The hints workaround converts on the X display, which this fixture has none
 * of: no hint is ever offered, as a selection offering only text says. Whether it
 * could convert at all, and what it says about the owner, are the fixture's to
 * answer. */
static GPasteClipboardX11Hints *
mock_hints_new (GdkClipboard *clipboard G_GNUC_UNUSED,
                gboolean      is_clipboard G_GNUC_UNUSED)
{
    return NULL;
}

static void
mock_hints_read (GPasteClipboardX11Hints *self G_GNUC_UNUSED,
                 GPasteClipboardUpdate   *update G_GNUC_UNUSED)
{
}

static gboolean
mock_hints_available (GPasteClipboardX11Hints *self G_GNUC_UNUSED)
{
    return hints_available;
}

static GPasteClipboardX11HintsOwner
mock_hints_get_owner (GPasteClipboardX11Hints *self G_GNUC_UNUSED)
{
    return hints_owner;
}

#define gdk_clipboard_get_formats mock_formats
#define gdk_clipboard_is_local mock_local
#define gdk_clipboard_set_content mock_set_content
#define gdk_clipboard_read_text_async mock_read_text
#define gdk_clipboard_read_text_finish mock_read_text_finish
#define gdk_clipboard_read_async mock_read_mime
#define gdk_clipboard_read_finish mock_read_mime_finish
#define g_paste_clipboard_x11_hints_new mock_hints_new
#define g_paste_clipboard_x11_hints_read mock_hints_read
#define g_paste_clipboard_x11_hints_available mock_hints_available
#define g_paste_clipboard_x11_hints_get_owner mock_hints_get_owner
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

/* A text selection whose formats name the hint too, which is what GTK offers for
 * a password GPaste itself published: the one case the names it drops are there
 * to be read off the formats. */
static void
set_hinted_formats (void)
{
    const gchar *mimes[] = { g_paste_sensitive_mime_get (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT) };
    g_autoptr (GdkContentFormats) hint = gdk_content_formats_new (mimes, G_N_ELEMENTS (mimes));

    set_formats (TRUE);
    formats = gdk_content_formats_union (g_steal_pointer (&formats), hint);
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
    hints_available = TRUE;
    /* A copy someone has just made: the selection has an owner, which is what
     * makes a set of empty formats the ambiguity the long deadline is for. */
    hints_owner = G_PASTE_CLIPBOARD_X11_HINTS_OWNER_SOME;
    g_clear_pointer (&requested_mime, g_free);
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
    g_clear_pointer (&requested_mime, g_free);
}

/* The two changes GTK raises for one external copy, in order: the empty formats
 * a new owner is announced with, then its targets.
 *
 * Nothing may be published over the first and nothing may take the selection for
 * empty there -- doing so is what puts the history's head on top of the copy
 * being made, and holding that line is what lets on_real_changed () pass the
 * empty change on rather than filter it out, which is what keeps a release
 * visible at all (see the comment there).
 *
 * The record the selection was carrying waits for the read that follows: a value
 * of its own retires the password and is tracked, where the same value is that
 * password still sitting there, and the expiry it is owed publishes the
 * replacement over it. */
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

/* Fire the formats wait's deadline by hand rather than waiting it out. */
static void
time_out_formats_wait (Fixture *f)
{
    GPasteClipboardGdkFormatsWait *wait = f->clipboard->formats_wait;

    g_assert_nonnull (wait);
    g_source_remove (wait->source_id);
    g_paste_clipboard_gdk_formats_wait_timed_out (wait);
    pump (10);
}

typedef struct
{
    gboolean answered;
    gboolean superseded;
} IdentifyResult;

static void
on_identified (GPasteClipboardProvider *provider G_GNUC_UNUSED,
               GPasteItem              *item,
               gboolean                 superseded,
               GPasteClipboardSecret    secret G_GNUC_UNUSED,
               gpointer                 user_data)
{
    g_autoptr (GPasteItem) owned = item;
    IdentifyResult *result = user_data;

    result->answered = TRUE;
    result->superseded = superseded;
}

/* An owner slow enough that the wait its change asked for timed out while it
 * still held the selection is owed that copy: its targets arriving under an
 * identifying read asked for in the meantime are raised as the change they
 * are, and the copy is recorded, rather than continuing that read and
 * identifying the copy without ever recording it (see on_real_changed ()). */
static void
formats_owed_copy (Fixture      *f,
                   gconstpointer user_data G_GNUC_UNUSED)
{
    IdentifyResult identify = { 0 };

    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    time_out_formats_wait (f);
    g_assert_false (g_paste_clipboard_gdk_is_reading (f->clipboard));

    /* What a countdown, an expiry wait or a strip asks. */
    g_paste_clipboard_provider_update (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard), on_identified, &identify);
    g_assert_true (g_paste_clipboard_gdk_is_reading (f->clipboard));

    set_formats (TRUE);
    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    g_assert_nonnull (pending_text);
    g_task_return_pointer (pending_text, g_strdup ("the slow copy"), g_free);
    g_clear_object (&pending_text);
    pump (10);

    g_assert_true (identify.answered);
    g_assert_true (identify.superseded);
    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 2);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (f->history, 0)), ==, "the slow copy");
}

/* The mirror of formats_owed_copy (): the bootstrap read's own wait times out
 * with its owner still there, and the targets arrive afterwards with no read out
 * at all. They answer the identification that wait gave up on, and are not taken
 * for a copy: what was already on the selection stays out of the history (see
 * on_real_changed ()). */
static void
formats_bootstrap_late (Fixture      *f,
                        gconstpointer user_data G_GNUC_UNUSED)
{
    time_out_formats_wait (f);
    g_assert_false (g_paste_clipboard_gdk_is_reading (f->clipboard));

    set_formats (TRUE);
    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    g_assert_nonnull (pending_text);
    g_task_return_pointer (pending_text, g_strdup ("already there"), g_free);
    g_clear_object (&pending_text);
    pump (10);

    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)), ==, "already there");
    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 1);
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

    /* Nothing goes back on while the wait is out: the formats may still arrive,
     * and the copy being negotiated is not ours to overwrite. */
    g_assert_true (g_paste_clipboard_gdk_is_reading (f->clipboard));
    g_assert_cmpuint (publications, ==, before);

    /* The owner that was there when the change was announced is gone by the time
     * the deadline is reached, which is the release the long wait finds out
     * about rather than the one it is spared (/gdk/formats/release). */
    hints_owner = G_PASTE_CLIPBOARD_X11_HINTS_OWNER_NONE;
    time_out_formats_wait (f);
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

/* A selection nobody owns is not the ambiguity the long deadline is for: X has
 * already said so when the empty formats arrive, so the only thing left to cover
 * is the gap a release and the claim after it pass through, and the head is back
 * on the selection within it -- no deadline waited out, and none of what
 * is_reading () holds held for its length.
 *
 * Waited out for real here, since the length is the whole point: a change with
 * an owner behind it is still reading a second later (/gdk/formats/new-copy),
 * where this one has concluded. What it concludes is unchanged, the grace ending
 * in the same question asked of the same server -- which is what keeps a claim
 * landing inside it an owner like any other. */
static void
formats_release (Fixture      *f,
                 gconstpointer user_data G_GNUC_UNUSED)
{
    g_paste_clipboards_manager_select (f->manager, f->password);
    guint before = publications;

    hints_owner = G_PASTE_CLIPBOARD_X11_HINTS_OWNER_NONE;
    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    g_paste_clipboards_manager_expire_password_async (f->manager, on_expiry_done, f);

    g_assert_true (g_paste_clipboard_gdk_is_reading (f->clipboard));
    g_assert_cmpuint (publications, ==, before);

    pump (2 * G_PASTE_CLIPBOARD_GDK_RELEASE_GRACE);

    g_assert_true (f->completed);
    g_assert_false (g_paste_clipboard_gdk_is_reading (f->clipboard));
    g_assert_cmpuint (publications, ==, before + 1);
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)), ==, "replacement");
    g_assert_false (g_paste_clipboard_provider_is_empty (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)));
}

static void
on_expiry_unresolved (GObject      *source G_GNUC_UNUSED,
                      GAsyncResult *result,
                      gpointer      user_data)
{
    g_autoptr (GError) error = NULL;

    Fixture *f = user_data;

    g_assert_false (g_paste_clipboards_manager_expire_password_finish (f->manager, result, &error));
    g_assert_error (error, G_PASTE_ERROR, G_PASTE_ERROR_FAILED);
    f->completed = TRUE;
}

/* An owner slow to serve TARGETS is not a release: X still names one, so the
 * deadline answers nothing rather than putting the history's head over the copy
 * that owner is publishing -- and the record the selection holds stands, an
 * unknown being no evidence that it moved on.
 *
 * Nothing is confirmed against the cache either. It was filled for the owner
 * this one replaced, so the selection is marked as carrying something with no
 * value here: what a reader of the unknown must not get is the previous owner's
 * text answering for the current owner's selection. Expiry then reports a
 * selection it could not identify -- and above all publishes nothing over it --
 * where a cache taken at face value has it find the password still there and put
 * a replacement over the copy that owner is publishing.
 *
 * A display that could not be asked at all takes this same path, @user_data
 * being the answer the two cases run with. That is what the third value is for:
 * a server saying nothing is not a server saying nobody owns this, and only the
 * latter may restore the head, restoration being the half that overwrites a copy
 * still being made. The displays the workaround cannot be set up on are the
 * nested and forwarded ones, whose owners are also the slowest to publish
 * targets, so the answer that goes missing goes missing exactly where the
 * deadline is reached. */
static void
formats_deadline_slow_owner (Fixture      *f,
                             gconstpointer user_data)
{
    g_paste_clipboards_manager_select (f->manager, f->password);
    guint before = publications;

    hints_owner = GPOINTER_TO_INT (user_data);
    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    time_out_formats_wait (f);

    g_assert_false (g_paste_clipboard_gdk_is_reading (f->clipboard));
    g_assert_cmpuint (publications, ==, before);
    g_assert_null (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)));
    /* The selection has an owner, so the head stays off it. */
    g_assert_false (g_paste_clipboard_provider_is_empty (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)));

    /* The waiter asks for a read of its own, which the same slow owner leaves
     * unanswered: the record is still one nothing could confirm. */
    g_paste_clipboards_manager_expire_password_async (f->manager, on_expiry_unresolved, f);
    g_assert_true (g_paste_clipboard_gdk_is_reading (f->clipboard));
    time_out_formats_wait (f);

    g_assert_true (f->completed);
    g_assert_cmpuint (publications, ==, before);
}

/* Where the workaround cannot convert the selection, the hints are read off the
 * formats GDK does answer: a hint named there is recognised, and one it drops --
 * every hint a foreign owner offers, as GTK stands -- reads as a selection saying
 * its text is ordinary. What matters is that it is an answer at all: asking the
 * workaround anyway would leave every text read unidentified for the session. */
static void
hints_unavailable (Fixture      *f,
                   gconstpointer user_data)
{
    gboolean hinted = GPOINTER_TO_INT (user_data);

    hints_available = FALSE;
    /* A copy, announced the way GTK does it: empty formats first, and the
     * targets next. The targets alone would be the answer to the bootstrap read
     * still waiting on them (/gdk/formats/bootstrap). */
    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    if (hinted)
        set_hinted_formats ();
    else
        set_formats (TRUE);

    g_paste_clipboard_gdk_on_real_changed (f->clipboard);
    g_assert_nonnull (pending_text);
    g_task_return_pointer (pending_text, g_strdup ("a copied secret"), g_free);
    g_clear_object (&pending_text);
    pump (10);

    g_assert_false (g_paste_clipboard_gdk_is_reading (f->clipboard));
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (G_PASTE_CLIPBOARD_PROVIDER (f->clipboard)), ==, "a copied secret");
    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 2);
    g_assert_cmpint (G_PASTE_IS_PASSWORD_ITEM (g_paste_history_get (f->history, 0)), ==, hinted);
    g_assert_cmpstr (requested_mime, ==, (hinted) ? g_paste_sensitive_mime_get (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT) : NULL);
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
    g_test_add ("/gdk/formats/deadline-slow-owner", Fixture, GINT_TO_POINTER (G_PASTE_CLIPBOARD_X11_HINTS_OWNER_SOME), fixture_setup, formats_deadline_slow_owner, fixture_teardown);
    g_test_add ("/gdk/formats/deadline-unknown-owner", Fixture, GINT_TO_POINTER (G_PASTE_CLIPBOARD_X11_HINTS_OWNER_UNKNOWN), fixture_setup, formats_deadline_slow_owner, fixture_teardown);
    g_test_add ("/gdk/formats/release", Fixture, NULL, fixture_setup, formats_release, fixture_teardown);
    g_test_add ("/gdk/hints/unavailable", Fixture, GINT_TO_POINTER (FALSE), fixture_setup, hints_unavailable, fixture_teardown);
    g_test_add ("/gdk/hints/unavailable-hinted", Fixture, GINT_TO_POINTER (TRUE), fixture_setup, hints_unavailable, fixture_teardown);
    g_test_add ("/gdk/formats/stale-read-publication", Fixture, NULL, fixture_setup, stale_read_and_publication, fixture_teardown);
    g_test_add ("/gdk/formats/owed-copy", Fixture, NULL, fixture_setup, formats_owed_copy, fixture_teardown);
    g_test_add ("/gdk/formats/bootstrap-late", Fixture, NULL, fixture_setup, formats_bootstrap_late, fixture_teardown);
    g_test_add ("/gdk/formats/bootstrap", Fixture, GINT_TO_POINTER (FALSE), fixture_setup, formats_bootstrap, fixture_teardown);
    g_test_add ("/gdk/formats/bootstrap-then-copy", Fixture, GINT_TO_POINTER (TRUE), fixture_setup, formats_bootstrap, fixture_teardown);
    g_test_add_func ("/gdk/formats/dispose", formats_wait_dispose);
    return g_paste_test_env_run ();
}
