// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <string.h>

#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-clipboard-provider-private.h>
#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

/**
 * g_paste_clipboard_content_clear:
 * @content: a #GPasteClipboardContent
 *
 * Release whatever @content currently holds and reset it to %CLIPBOARD_CONTENT_NONE.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_content_clear (GPasteClipboardContent *content)
{
    switch (content->kind)
    {
    case CLIPBOARD_CONTENT_TEXT:
    case CLIPBOARD_CONTENT_IMAGE:
        g_clear_pointer (&content->str, g_free);
        break;
    case CLIPBOARD_CONTENT_FILE_LIST:
        if (content->file_list)
            g_boxed_free (GDK_TYPE_FILE_LIST, g_steal_pointer (&content->file_list));
        break;
    case CLIPBOARD_CONTENT_COLOR:
    case CLIPBOARD_CONTENT_IGNORED:
    case CLIPBOARD_CONTENT_NONE:
        break;
    }
    content->kind = CLIPBOARD_CONTENT_NONE;
    /* Leave the union readable, not merely dead. The set_*_take() helpers assign
     * through g_set_str_take(), which *reads* @str to compare and free it before
     * storing — and after a colour that is four floats reinterpreted as a
     * pointer, which it would then strcmp() and g_free(). Clearing the kind is
     * not enough; the bytes have to go too. */
    content->str = NULL;
}

/**
 * g_paste_clipboard_content_is_empty:
 * @content: a #GPasteClipboardContent
 *
 * Returns: whether @content holds nothing
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_content_is_empty (const GPasteClipboardContent *content)
{
    return content->kind == CLIPBOARD_CONTENT_NONE;
}

/**
 * g_paste_clipboard_content_set_text:
 * @content: a #GPasteClipboardContent
 * @text: the text to hold (copied)
 *
 * Replace @content with @text.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_content_set_text (GPasteClipboardContent *content,
                                    const gchar            *text)
{
    g_paste_clipboard_content_set_text_take (content, g_strdup (text));
}

/**
 * g_paste_clipboard_content_set_text_take:
 * @content: a #GPasteClipboardContent
 * @text: (transfer full): the text to hold
 *
 * Replace @content with @text, consuming it.
 *
 * Every clipboard update goes through here with a string the caller just built
 * and immediately drops, so this avoids a copy on the hot path.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_content_set_text_take (GPasteClipboardContent *content,
                                         gchar                  *text)
{
    g_paste_clipboard_content_clear (content);
    content->kind = CLIPBOARD_CONTENT_TEXT;
    g_set_str_take (&content->str, text);
}

/**
 * g_paste_clipboard_content_set_image_checksum:
 * @content: a #GPasteClipboardContent
 * @checksum: the image checksum to hold (copied)
 *
 * Replace @content with an image identified by @checksum.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_content_set_image_checksum (GPasteClipboardContent *content,
                                              const gchar            *checksum)
{
    g_paste_clipboard_content_set_image_checksum_take (content, g_strdup (checksum));
}

/**
 * g_paste_clipboard_content_set_image_checksum_take:
 * @content: a #GPasteClipboardContent
 * @checksum: (transfer full): the image checksum to hold
 *
 * Replace @content with an image identified by @checksum, consuming it.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_content_set_image_checksum_take (GPasteClipboardContent *content,
                                                   gchar                  *checksum)
{
    g_paste_clipboard_content_clear (content);
    content->kind = CLIPBOARD_CONTENT_IMAGE;
    g_set_str_take (&content->str, checksum);
}

/**
 * g_paste_clipboard_content_set_color:
 * @content: a #GPasteClipboardContent
 * @rgba: the colour to hold (copied)
 *
 * Replace @content with the colour @rgba.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_content_set_color (GPasteClipboardContent *content,
                                     const GdkRGBA          *rgba)
{
    g_paste_clipboard_content_clear (content);
    content->kind = CLIPBOARD_CONTENT_COLOR;
    content->rgba = *rgba;
}

/**
 * g_paste_clipboard_content_set_file_list:
 * @content: a #GPasteClipboardContent
 * @file_list: (nullable): the file list to hold (copied)
 *
 * Replace @content with @file_list. A %NULL @file_list leaves @content empty.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_content_set_file_list (GPasteClipboardContent *content,
                                         GdkFileList            *file_list)
{
    g_paste_clipboard_content_clear (content);

    if (!file_list)
        return;

    content->kind = CLIPBOARD_CONTENT_FILE_LIST;
    content->file_list = g_boxed_copy (GDK_TYPE_FILE_LIST, file_list);
}

/**
 * g_paste_clipboard_content_get_text:
 * @content: a #GPasteClipboardContent
 *
 * Returns: (nullable): the held text, or %NULL unless @content holds text
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_content_get_text (const GPasteClipboardContent *content)
{
    return (content->kind == CLIPBOARD_CONTENT_TEXT) ? content->str : NULL;
}

/**
 * g_paste_clipboard_content_get_image_checksum:
 * @content: a #GPasteClipboardContent
 *
 * Returns: (nullable): the held image checksum, or %NULL unless @content holds an image
 */
G_PASTE_VISIBLE const gchar *
g_paste_clipboard_content_get_image_checksum (const GPasteClipboardContent *content)
{
    return (content->kind == CLIPBOARD_CONTENT_IMAGE) ? content->str : NULL;
}

/**
 * g_paste_clipboard_content_get_file_list:
 * @content: a #GPasteClipboardContent
 *
 * Returns: (nullable) (transfer none): the held file list, or %NULL unless @content holds one
 */
G_PASTE_VISIBLE GdkFileList *
g_paste_clipboard_content_get_file_list (const GPasteClipboardContent *content)
{
    return (content->kind == CLIPBOARD_CONTENT_FILE_LIST) ? content->file_list : NULL;
}

/**
 * g_paste_clipboard_content_classify_text:
 * @content: the currently held content (for dedup against the new text)
 * @settings: a #GPasteSettings instance (trim and min/max size policy)
 * @is_clipboard: whether the caller drives the clipboard (vs the primary selection)
 * @text: the candidate text just read from the selection
 * @out_value: (out) (transfer full) (nullable): the text to act on, or %NULL when rejected
 *
 * Apply the shared trim/size/dedup policy to a candidate clipboard text, so both
 * backends accept and normalise text identically.
 *
 * Returns: the action the backend should take for @text
 */
G_PASTE_VISIBLE GPasteClipboardTextAction
g_paste_clipboard_content_classify_text (const GPasteClipboardContent *content,
                                         GPasteSettings               *settings,
                                         gboolean                      is_clipboard,
                                         const gchar                  *text,
                                         gchar                       **out_value)
{
    gboolean trim_items = g_paste_settings_get_trim_items (settings);
    g_autofree gchar *stripped = trim_items ? g_strstrip (g_strdup (text)) : NULL;
    const gchar *to_add = trim_items ? stripped : text;
    guint64 length = strlen (to_add);

    *out_value = NULL;

    gboolean unchanged = content->kind == CLIPBOARD_CONTENT_TEXT && g_paste_str_equal (content->str, text);

    if (length < g_paste_settings_get_min_text_item_size (settings) ||
        length > g_paste_settings_get_max_text_item_size (settings))
        return unchanged ? G_PASTE_CLIPBOARD_TEXT_UNCHANGED : G_PASTE_CLIPBOARD_TEXT_REJECT;

    /* Trimming changed the clipboard's own text: re-own it with the stripped
     * form, duplicate or not -- the padded form is what is on the selection. */
    if (trim_items && is_clipboard && !g_paste_str_equal (text, stripped))
    {
        *out_value = g_steal_pointer (&stripped);
        return G_PASTE_CLIPBOARD_TEXT_RESELECT;
    }

    /* A duplicate is the raw text the cache holds, or its trimmed form: the
     * primary selection is never re-owned with the stripped text, so an owner
     * re-asserting it hands back the padded text while the cache holds the
     * trimmed item's value. Taking that for new content would re-add it over
     * whatever was copied since and, with synchronization on, publish it over
     * the clipboard. */
    if (content->kind == CLIPBOARD_CONTENT_TEXT && (unchanged || g_paste_str_equal (content->str, to_add)))
        return G_PASTE_CLIPBOARD_TEXT_UNCHANGED;

    /* When trimming, to_add aliases the owned stripped buffer — hand it off rather
     * than copying; otherwise to_add borrows text and must be duplicated. */
    *out_value = trim_items ? g_steal_pointer (&stripped) : g_strdup (text);
    return G_PASTE_CLIPBOARD_TEXT_SET;
}

/**
 * g_paste_clipboard_file_list_equal:
 * @a: (nullable): a #GdkFileList
 * @b: (nullable): another #GdkFileList
 *
 * Returns: whether @a and @b list the same files in the same order
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_file_list_equal (GdkFileList *a,
                                   GdkFileList *b)
{
    if (a == b)
        return TRUE;
    if (!a || !b)
        return FALSE;

    /* gdk_file_list_get_files is (transfer container): it hands back a fresh
     * GSList whose GFiles stay owned by the list, hence g_autoptr (GSList). */
    g_autoptr (GSList) files_a = gdk_file_list_get_files (a);
    g_autoptr (GSList) files_b = gdk_file_list_get_files (b);
    const GSList *fa = files_a;
    const GSList *fb = files_b;

    for (; fa && fb; fa = fa->next, fb = fb->next)
    {
        if (!g_file_equal (G_FILE (fa->data), G_FILE (fb->data)))
            return FALSE;
    }

    return !fa && !fb;
}

static void g_paste_clipboard_read_guard_set_deadline (GPasteClipboardReadGuard *guard, guint seconds);

static void
g_paste_clipboard_read_guard_timed_out (gpointer user_data)
{
    GPasteClipboardReadGuard *guard = user_data;

    /* This is the source firing, so it is spent: drop the id before anything can
     * reach for g_source_remove () on it. */
    guard->timeout_id = 0;

    /* A read landed while this was pending, so what the deadline is on has not
     * happened: the batch gets what is left of its silence allowance rather than
     * a new source per read (a text update fires up to eight of them). Rounded
     * down to the second the timer works in, which can only ever hand the batch
     * a little more rope. */
    gint64 silence = (g_get_monotonic_time () - guard->last_read) / G_USEC_PER_SEC;

    if (silence < guard->timeout)
    {
        g_paste_clipboard_read_guard_set_deadline (guard, guard->timeout - silence);
        return;
    }

    /* Held before the conclusion rather than read off @guard after it: what the
     * guard is embedded in is the very thing @expired concludes, and concluding
     * is free to free it -- the callback it ends in reaches the history, which
     * publishes, which lands back in an update that counts its last read out. */
    g_autoptr (GCancellable) cancellable = g_object_ref (guard->cancellable);

    guard->expired (guard->user_data);

    /* Asked for after the conclusion and not before it: g_cancellable_cancel ()
     * runs the handlers connected to it in this very thread, so a read that
     * failed from one of them would count itself out -- and free what is being
     * concluded -- under the call above. */
    g_cancellable_cancel (cancellable);
}

static void
g_paste_clipboard_read_guard_set_deadline (GPasteClipboardReadGuard *guard,
                                           guint                     seconds)
{
    guard->timeout_id = g_timeout_add_seconds_once (seconds,
                                                    g_paste_clipboard_read_guard_timed_out,
                                                    guard);
    g_source_set_name_by_id (guard->timeout_id, "[GPaste] clipboard read guard");
}

/**
 * g_paste_clipboard_read_guard_arm:
 * @guard: the #GPasteClipboardReadGuard to arm
 * @expired: what to conclude when the deadline runs out
 * @user_data: what to conclude it on
 *
 * Give a batch of clipboard reads its cancellable and its deadline.
 *
 * Armed before the first read goes out, so it covers every one of them, and
 * dropped when they are concluded, so it outlives none.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_read_guard_arm (GPasteClipboardReadGuard *guard,
                                  GSourceOnceFunc           expired,
                                  gpointer                  user_data)
{
    g_return_if_fail (guard);
    g_return_if_fail (expired);

    guard->cancellable = g_cancellable_new ();
    guard->last_read = g_get_monotonic_time ();
    guard->timeout = G_PASTE_CLIPBOARD_READ_TIMEOUT;
    guard->expired = expired;
    guard->user_data = user_data;
    g_paste_clipboard_read_guard_set_deadline (guard, guard->timeout);
}

/**
 * g_paste_clipboard_read_guard_touch:
 * @guard: the #GPasteClipboardReadGuard to hand its deadline back
 *
 * Start @guard's deadline over, one of the reads it covers having just landed
 *
 * A read coming in is the batch demonstrably moving, which is what that deadline
 * is a deadline on: see %G_PASTE_CLIPBOARD_READ_TIMEOUT.
 *
 * Written down rather than acted on: the source, when it fires, re-arms itself
 * for what is left of the allowance since the last read landed, so a batch that
 * keeps moving keeps the one source it was armed with. A guard with no deadline
 * left is one whose batch was already concluded, and a timestamp written on it
 * is one nothing is left to compare.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_read_guard_touch (GPasteClipboardReadGuard *guard)
{
    g_return_if_fail (guard);

    if (!guard->timeout_id)
        return;

    guard->last_read = g_get_monotonic_time ();
}

/**
 * g_paste_clipboard_read_guard_disarm:
 * @guard: the #GPasteClipboardReadGuard to disarm
 *
 * Drop the deadline, keeping the cancellable the reads went out on
 *
 * What a conclusion does: the reads it concluded without may still be out there
 * holding that cancellable, and the guard has nothing left to say to them.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_read_guard_disarm (GPasteClipboardReadGuard *guard)
{
    g_return_if_fail (guard);

    g_clear_handle_id (&guard->timeout_id, g_source_remove);
}

/* Bring @guard's deadline forward to the next turn of the main loop, the batch it
 * covers having stopped being worth waiting for. */
static void
g_paste_clipboard_read_guard_expire_soon (GPasteClipboardReadGuard *guard)
{
    g_return_if_fail (guard);

    /* No deadline left is a batch already concluded, as it is for
     * g_paste_clipboard_read_guard_touch (). */
    if (!guard->timeout_id)
        return;

    g_clear_handle_id (&guard->timeout_id, g_source_remove);

    /* Silence is what the deadline measures, and there is none left to wait out:
     * whatever answers now answers for content nothing holds. An idle rather than
     * a deadline of zero seconds, which the timer would round up to the next
     * second boundary -- the superseded update would hold its provider, and on
     * X11 the requestor window behind it, for that whole extra second. */
    guard->timeout = 0;
    guard->timeout_id = g_idle_add_once (g_paste_clipboard_read_guard_timed_out, guard);
    g_source_set_name_by_id (guard->timeout_id, "[GPaste] clipboard read guard");
}

/**
 * g_paste_clipboard_read_guard_clear:
 * @guard: the #GPasteClipboardReadGuard to release
 *
 * Release what @guard holds, once every read it covered is in
 */
G_PASTE_VISIBLE void
g_paste_clipboard_read_guard_clear (GPasteClipboardReadGuard *guard)
{
    g_return_if_fail (guard);

    g_paste_clipboard_read_guard_disarm (guard);
    g_clear_object (&guard->cancellable);
}

/* The deadline on a sync ran out: let go of the selection its text was going to,
 * which is all a sync has to conclude. The read landing afterwards then finds no
 * target and only frees itself. */
static void
g_paste_clipboard_sync_timed_out (gpointer user_data)
{
    GPasteClipboardSyncData *data = user_data;

    g_debug ("clipboard: giving up on a sync read that never came back");
    g_clear_object (&data->other);
}

/**
 * g_paste_clipboard_sync_data_new:
 * @other: the selection the text being read is going to
 *
 * What a sync read has to keep alive, guard included
 *
 * Returns: (transfer full): the newly allocated #GPasteClipboardSyncData
 */
G_PASTE_VISIBLE GPasteClipboardSyncData *
g_paste_clipboard_sync_data_new (GPasteClipboardProvider *other)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (other), NULL);

    GPasteClipboardSyncData *data = g_new0 (GPasteClipboardSyncData, 1);

    data->other = g_object_ref (other);
    g_paste_clipboard_read_guard_arm (&data->guard, g_paste_clipboard_sync_timed_out, data);

    return data;
}

/**
 * g_paste_clipboard_sync_data_free:
 * @data: the #GPasteClipboardSyncData to release
 *
 * Release what a sync read was keeping alive, whether or not it concluded
 */
G_PASTE_VISIBLE void
g_paste_clipboard_sync_data_free (GPasteClipboardSyncData *data)
{
    g_return_if_fail (data);

    g_paste_clipboard_read_guard_clear (&data->guard);
    g_clear_object (&data->other);
    g_free (data);
}

/**
 * g_paste_clipboard_mime_ctx_new:
 * @data: the update the read counts into
 * @mime: which entry it was fired for
 *
 * What one mime read has to carry, whichever backend fired it
 *
 * Returns: (transfer full): the newly allocated #GPasteClipboardMimeCtx
 */
G_PASTE_VISIBLE GPasteClipboardMimeCtx *
g_paste_clipboard_mime_ctx_new (gpointer          data,
                                GPasteSpecialMime mime)
{
    GPasteClipboardMimeCtx *ctx = g_new0 (GPasteClipboardMimeCtx, 1);

    ctx->data = data;
    ctx->mime = mime;

    return ctx;
}

/**
 * g_paste_clipboard_mime_results_store:
 * @results: the #GPasteClipboardMimeResults the update is filling
 * @mime: which entry the read was fired for
 * @bytes: (nullable): what the read came back with
 *
 * Record what one finished mime read means.
 *
 * An owner that advertised a mimetype and then answered nothing under it has
 * said nothing, so an empty read is no read at all -- which is a policy neither
 * backend owns.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_mime_results_store (GPasteClipboardMimeResults *results,
                                      GPasteSpecialMime           mime,
                                      GBytes                     *bytes)
{
    if (bytes && g_bytes_get_size (bytes) > 0)
    {
        g_clear_object (&results->special_mime[mime]);
        results->special_mime[mime] = g_paste_binary_data_new (mime, g_bytes_ref (bytes));
    }
}

/**
 * g_paste_clipboard_mime_results_clear:
 * @results: the #GPasteClipboardMimeResults to release
 *
 * Release whatever representations are still in @results
 *
 * The ones g_paste_clipboard_content_to_item() put on an item are gone from it
 * already, so what is left is what nothing took.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_mime_results_clear (GPasteClipboardMimeResults *results)
{
    g_return_if_fail (results);

    for (GPasteSpecialMime mime = G_PASTE_SPECIAL_MIME_FIRST; mime < G_PASTE_SPECIAL_MIME_LAST; ++mime)
        g_clear_object (&results->special_mime[mime]);
}

/**
 * g_paste_clipboard_content_to_item:
 * @kind: what the read produced
 * @text: (nullable): the text, for %CLIPBOARD_CONTENT_TEXT
 * @texture: (nullable): the image, for %CLIPBOARD_CONTENT_IMAGE
 * @file_list: (nullable): the files, for %CLIPBOARD_CONTENT_FILE_LIST
 * @rgba: (nullable): the colour, for %CLIPBOARD_CONTENT_COLOR
 * @special_mimes: (array fixed-size=5): the alternative representations, one
 *                 slot per #GPasteSpecialMime (%G_PASTE_SPECIAL_MIME_LAST of
 *                 them)
 *
 * Build the item a finished clipboard read describes. Both backends end their
 * update here, which is what keeps them agreeing on what each kind produces and
 * on which kinds carry special values.
 *
 * Returns: (transfer full) (nullable): the item, or %NULL when there is none
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_clipboard_content_to_item (GPasteClipboardContentKind kind,
                                   const gchar               *text,
                                   GdkTexture                *texture,
                                   GdkFileList               *file_list,
                                   const GdkRGBA             *rgba,
                                   GPasteBinaryData         **special_mimes)
{
    GPasteItem *item = NULL;

    switch (kind)
    {
    case CLIPBOARD_CONTENT_FILE_LIST:
        if (file_list)
            item = G_PASTE_ITEM (g_paste_uris_item_new (file_list));
        break;
    case CLIPBOARD_CONTENT_COLOR:
        if (rgba)
            item = G_PASTE_ITEM (g_paste_color_item_new (rgba));
        break;
    case CLIPBOARD_CONTENT_TEXT:
        if (text)
            item = G_PASTE_ITEM (g_paste_text_item_new (text));
        break;
    case CLIPBOARD_CONTENT_IMAGE:
        if (texture)
            item = G_PASTE_ITEM (g_paste_image_item_new (texture));
        break;
    case CLIPBOARD_CONTENT_IGNORED:
    case CLIPBOARD_CONTENT_NONE:
        break;
    }

    /* Only these two ever come with alternative representations. */
    if (item && special_mimes &&
        (kind == CLIPBOARD_CONTENT_TEXT || kind == CLIPBOARD_CONTENT_FILE_LIST))
    {
        for (GPasteSpecialMime mime = G_PASTE_SPECIAL_MIME_FIRST; mime < G_PASTE_SPECIAL_MIME_LAST; ++mime)
        {
            if (special_mimes[mime])
                g_paste_item_add_special_value (item, g_steal_pointer (&special_mimes[mime]));
        }
    }

    return item;
}

/* Everything an update was holding for the item it has just built.
 *
 * At the conclusion and not at the teardown, which is the whole of what an
 * update outliving its own conclusion may be left holding: cancelling cannot
 * fail the reads still out (see the backends), so nothing bounds how long they
 * keep the struct alive, and what waits on them is a counter where this is a
 * whole texture and the provider's only ref -- and that ref is what keeps a
 * backend's requestor window and its interned properties on the X server for the
 * rest of the session.
 *
 * Called again from the teardown, for whatever a read landing afterwards stored
 * on its way in: every release here clears what it released, so the second pass
 * finds either that or nothing.
 *
 * The one thing an update holds that is not released here is the mime a read was
 * fired with, which the reads still out may be holding themselves: the teardown
 * is the only place that knows they are all in. */
static void
g_paste_clipboard_update_release_content (GPasteClipboardUpdate *update)
{
    g_paste_clipboard_mime_results_clear (&update->mimes);

    /* The kind says which member of the union is the live one, and it is the
     * only one there is to release: the update is fired for one kind and every
     * read that writes here is that kind's. */
    switch (update->content_kind)
    {
    case CLIPBOARD_CONTENT_TEXT:
        g_clear_pointer (&update->text, g_free);
        break;
    case CLIPBOARD_CONTENT_IMAGE:
        g_clear_object (&update->texture);
        break;
    case CLIPBOARD_CONTENT_FILE_LIST:
        if (update->file_list)
            g_boxed_free (GDK_TYPE_FILE_LIST, g_steal_pointer (&update->file_list));
        break;
    case CLIPBOARD_CONTENT_COLOR:
    case CLIPBOARD_CONTENT_IGNORED:
    case CLIPBOARD_CONTENT_NONE:
        break;
    }
}

/* Build what the update read and hand it to whoever asked for it.
 *
 * Separate from the teardown below because the guard concludes an update whose
 * reads will never come back, and those reads are what frees the struct: the
 * item is built, the caller is called back, everything either of them was built
 * from is released above, and what waits on reads that may never land is the
 * counter they report to. */
static void
g_paste_clipboard_update_conclude (GPasteClipboardUpdate *update)
{
    update->concluded = TRUE;

    /* @slot names the update in flight, and this one has stopped being it,
     * whichever of the two ways it concluded: an update left there is what the
     * next one to start marks and releases, which it does without asking whether
     * anything is left to conclude. */
    if (*update->slot == update)
        *update->slot = NULL;

    /* Overtaken while it was reading (see @superseded): nothing to build an item
     * from, and no selection to re-own. The callback is still owed all the same,
     * that being what releases whatever the caller put behind this update. */
    if (update->superseded)
    {
        g_paste_clipboard_update_release_content (update);
        g_paste_clipboard_read_guard_disarm (&update->guard);

        g_autoptr (GPasteClipboardProvider) provider = g_steal_pointer (&update->provider);

        if (update->callback)
            update->callback (provider, NULL, TRUE, update->user_data);

        return;
    }

    /* Nothing produced means nothing to build, whatever the kind said; and the
     * union means only the member matching the kind may be read, which is the
     * one the builder is handed. */
    GPasteClipboardContentKind kind = (update->produced) ? update->content_kind : CLIPBOARD_CONTENT_NONE;
    g_autoptr (GPasteItem) item = g_paste_clipboard_content_to_item (kind,
                                                                     (kind == CLIPBOARD_CONTENT_TEXT) ? update->text : NULL,
                                                                     (kind == CLIPBOARD_CONTENT_IMAGE) ? update->texture : NULL,
                                                                     (kind == CLIPBOARD_CONTENT_FILE_LIST) ? update->file_list : NULL,
                                                                     (kind == CLIPBOARD_CONTENT_COLOR) ? &update->rgba : NULL,
                                                                     update->mimes.special_mime);

    /* Deduplication only remembers content whose update survived. Publishing a
     * cache entry while MIME reads are pending would let a superseded update
     * suppress its successor without either producing a history item. */
    if (item)
    {
        switch (kind)
        {
        case CLIPBOARD_CONTENT_TEXT:
            /* The item's value is this very string, which the release below
             * would free: taken rather than copied. */
            g_paste_clipboard_content_set_text_take (update->cache, g_steal_pointer (&update->text));
            break;
        case CLIPBOARD_CONTENT_IMAGE:
            g_paste_clipboard_content_set_image_checksum (update->cache, g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item)));
            break;
        case CLIPBOARD_CONTENT_FILE_LIST:
            g_paste_clipboard_content_set_file_list (update->cache, update->file_list);
            break;
        case CLIPBOARD_CONTENT_COLOR:
            g_paste_clipboard_content_set_color (update->cache, &update->rgba);
            break;
        case CLIPBOARD_CONTENT_NONE:
        case CLIPBOARD_CONTENT_IGNORED:
            g_assert_not_reached ();
        }
    }
    else if (!update->unchanged)
    {
        /* Rejection and read failure establish no match with the previous
         * owner. Keep an unidentified selection non-empty so it cannot be
         * restored over. */
        g_paste_clipboard_content_clear (update->cache);
        update->cache->kind = CLIPBOARD_CONTENT_IGNORED;
    }

    /* Everything this conclusion has of its own is done with before either call
     * below, both of which can end up back here: publishing drops the previous
     * owner, which can fail a transfer this very update is still waiting on --
     * synchronously, on the mutter backend -- and the callback reaches the
     * history, which publishes too. A read failing counts itself out, and the
     * last one to do so frees the update, so anything read off it afterwards is
     * read off freed memory -- the provider included, whose only ref is the
     * update's, hence the one taken over below. */
    g_paste_clipboard_update_release_content (update);
    g_paste_clipboard_read_guard_disarm (&update->guard);

    /* Taken over rather than ref'd alongside: this is where the update stops
     * holding the provider up, and a conclusion the reads outlive would
     * otherwise hold it for as long as they do. */
    g_autoptr (GPasteClipboardProvider) provider = g_steal_pointer (&update->provider);
    GPasteClipboardProviderUpdateCallback callback = update->callback;
    gpointer user_data = update->user_data;
    gboolean reselect = update->reselect;

    /* Re-own trimmed text or a GDK image only after its reads are complete, so
     * replacing the owner cannot abort the remaining MIME transfers. Publishing
     * the item also preserves a password's sensitive hint and an image's
     * texture. */
    if (reselect && item)
        g_paste_clipboard_provider_select_item_full (provider, item, FALSE);

    /* (transfer full) to the callback, and ours to release when there is none:
     * an update fired with no callback is part of the contract -- what a
     * backend's update () itself checks on every early return -- and what the
     * item holds can be a password's cleartext. */
    if (callback)
        callback (provider, g_steal_pointer (&item), FALSE, user_data);
}

/* The guard ran out: conclude the update with what did arrive. Why it concludes
 * rather than failing the reads still out -- cancelling cannot fail them -- and
 * what that leaves behind is each backend's own answer, beside its update (). */
static void
g_paste_clipboard_update_timed_out (gpointer user_data)
{
    GPasteClipboardUpdate *update = user_data;

    g_debug ("%s: %s",
             g_paste_clipboard_provider_target_name (g_paste_clipboard_provider_is_clipboard (update->provider)),
             (update->superseded) ? "concluding a clipboard read its selection has moved on from"
                                  : "giving up on a clipboard read that never came back");
    g_paste_clipboard_update_conclude (update);
}

/**
 * g_paste_clipboard_update_supersede:
 * @slot: where the backend keeps the update in flight on this selection
 *
 * Give up on the update @slot holds, the selection it was reading having changed
 * under it
 *
 * Every read it has left is now a read of content nothing holds, and letting it
 * conclude would add the *older* copy to the history as the newest entry and
 * hand the provider's cache a text its selection has replaced. Marked rather
 * than concluded here, a conclusion calling back into the clipboards manager
 * whose notify () this is running under: what is left of that update is a
 * counter and a guard, and the guard's deadline -- brought forward to the next
 * turn of the main loop -- answers for both there instead.
 *
 * Everything of any size goes now rather than at that update's own conclusion,
 * which a read that never lands leaves a whole %G_PASTE_CLIPBOARD_READ_TIMEOUT
 * away: a superseded texture is one nothing will ever be built from, and a
 * superseded text can be a password's cleartext.
 *
 * Called for its own sake by the paths that answer a change without starting an
 * update -- a selection released, an owner offering only types this build does
 * not handle -- since what makes the update in flight stale is the change, not
 * the one replacing it: those answer their caller and return, and the update
 * they leave behind would otherwise land as if its selection still stood.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_update_supersede (GPasteClipboardUpdate **slot)
{
    g_return_if_fail (slot);

    if (!*slot)
        return;

    GPasteClipboardUpdate *previous = g_steal_pointer (slot);

    previous->superseded = TRUE;
    g_paste_clipboard_update_release_content (previous);

    /* And concluded on the next turn of the main loop rather than at its own
     * deadline: the callers that supersede without starting a successor leave
     * nothing else to bound it, so a conclusion %G_PASTE_CLIPBOARD_READ_TIMEOUT
     * away is one reaching the clipboards manager with an update that says
     * nothing, against a cache that has moved on twice over -- while holding the
     * provider, and on X11 the requestor window behind it, for that whole while.
     * Soon and not here for the reason above: a conclusion calls back into the
     * notify () this is running under. */
    g_paste_clipboard_read_guard_expire_soon (&previous->guard);
}

/**
 * g_paste_clipboard_update_new:
 * @provider: the #GPasteClipboardProvider being read
 * @content_kind: the kind the content read is for
 * @slot: where the backend keeps the update in flight on this selection
 * @cache: the provider's committed content, written only when this update
 *         completes
 * @callback: (scope async) (nullable): who to hand the item to
 * @user_data: what to hand it with
 *
 * Start an update, guard armed and one read outstanding, giving up on the one it
 * overtakes
 *
 * The one read is the caller's own: a backend fires its reads and then counts
 * itself out with g_paste_clipboard_update_maybe_done(), so an update whose
 * reads all answered synchronously still concludes exactly once.
 *
 * Whatever @slot held is superseded (g_paste_clipboard_update_supersede ()),
 * this being one of the changes that leave it reading a selection nothing holds.
 *
 * Returns: (transfer full): the newly allocated #GPasteClipboardUpdate
 */
G_PASTE_VISIBLE GPasteClipboardUpdate *
g_paste_clipboard_update_new (GPasteClipboardProvider              *provider,
                              GPasteClipboardContentKind            content_kind,
                              GPasteClipboardUpdate               **slot,
                              GPasteClipboardContent               *cache,
                              GPasteClipboardProviderUpdateCallback callback,
                              gpointer                              user_data)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (provider), NULL);
    g_return_val_if_fail (slot, NULL);
    g_return_val_if_fail (cache, NULL);

    g_paste_clipboard_update_supersede (slot);

    GPasteClipboardUpdate *update = g_new0 (GPasteClipboardUpdate, 1);

    /* Ref'd for the whole update -- the content read plus every mime read --
     * released when it is: it spans main-loop iterations, and a provider being
     * disposed under one is what this is here to survive. */
    update->provider = g_object_ref (provider);
    update->callback = callback;
    update->user_data = user_data;
    update->pending = 1;
    update->content_kind = content_kind;
    update->slot = slot;
    update->cache = cache;

    *slot = update;

    g_paste_clipboard_read_guard_arm (&update->guard, g_paste_clipboard_update_timed_out, update);

    return update;
}

/**
 * g_paste_clipboard_update_is_expired:
 * @update: the #GPasteClipboardUpdate a read counted into
 *
 * Returns: whether @update has already moved on without the read asking
 *
 * What a read landing asks before it touches anything but the counter. Nothing
 * is waiting for it either way, and what it brings back can only reach the
 * provider's cache -- where it would name content that selection no longer
 * holds, and dedup that very content out of every later update.
 *
 * Asked of the update and not of the guard's cancellable, which answers a moment
 * too late for one half of this and never for the other: a conclusion disarms
 * the guard and takes the provider over, and only then cancels -- the
 * cancellation being what runs the handlers that could free the very update
 * being concluded (see g_paste_clipboard_read_guard_timed_out ()) -- while an
 * update overtaken by the next one is not cancelled at all, its reads being what
 * still has to count it out.
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_update_is_expired (const GPasteClipboardUpdate *update)
{
    g_return_val_if_fail (update, TRUE);

    return update->concluded || update->superseded;
}

/**
 * g_paste_clipboard_update_add_read:
 * @update: the #GPasteClipboardUpdate about to fire one more read
 *
 * Count one read into @update
 *
 * Written beside the call that fires the read and never before the choice of
 * whether to fire one: a read counted in that never goes out leaves @update
 * unable to conclude -- and, its guard having only concluded it, never freed.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_update_add_read (GPasteClipboardUpdate *update)
{
    g_return_if_fail (update);

    ++update->pending;
}

/**
 * g_paste_clipboard_update_add_mime_read:
 * @update: the #GPasteClipboardUpdate the read counts into
 * @mime: which entry it is being fired for
 *
 * Count one mime read into @update and build what it has to carry
 *
 * The two halves of firing one, which is why they are one call: the count and
 * the context are the same read, and a backend that wrote only one of them
 * would either wedge @update (see add_read ()) or lose what its answer means.
 * All that is left to a backend is the call that fetches the bytes.
 *
 * Returns: (transfer full): the #GPasteClipboardMimeCtx to fire the read with
 */
G_PASTE_VISIBLE GPasteClipboardMimeCtx *
g_paste_clipboard_update_add_mime_read (GPasteClipboardUpdate *update,
                                        GPasteSpecialMime      mime)
{
    g_return_val_if_fail (update, NULL);

    g_paste_clipboard_update_add_read (update);

    return g_paste_clipboard_mime_ctx_new (update, mime);
}

/**
 * g_paste_clipboard_update_on_mime_read:
 * @ctx: (transfer full): what the read was fired with
 * @bytes: (nullable): what it came back with
 *
 * Record what one finished mime read means and count it out of its update
 *
 * The whole of what a backend does with a mime read that has landed: what the
 * bytes mean is #GPasteClipboardMimeResults' answer, and what a read reporting
 * does to the update is this file's, so neither backend has anything to add
 * around it.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_update_on_mime_read (GPasteClipboardMimeCtx *ctx,
                                       GBytes                 *bytes)
{
    g_return_if_fail (ctx);

    g_autofree GPasteClipboardMimeCtx *owned = ctx;
    GPasteClipboardUpdate *update = ctx->data;

    /* As for the content reads (g_paste_clipboard_update_is_expired ()): an
     * update that has moved on stores nothing more, or a sibling read that never
     * reports would keep these bytes for the rest of the session. */
    if (!g_paste_clipboard_update_is_expired (update))
        g_paste_clipboard_mime_results_store (&update->mimes, ctx->mime, bytes);

    g_paste_clipboard_update_maybe_done (update);
}

/**
 * g_paste_clipboard_update_maybe_done:
 * @update: the #GPasteClipboardUpdate one of whose reads has just reported
 *
 * Count one read out of @update, and conclude it once none is left
 *
 * Every read an update fires ends here, whatever it came back with and whether
 * or not anything is still waiting for it: this is what says the batch is still
 * moving (the guard's deadline measures silence) and what eventually releases
 * what the update was built from.
 */
G_PASTE_VISIBLE void
g_paste_clipboard_update_maybe_done (GPasteClipboardUpdate *update)
{
    g_return_if_fail (update);

    /* A read reporting is this batch demonstrably alive, so the rest of it gets
     * its deadline back. A no-op once the update is concluded, the guard having
     * been disarmed by the conclusion. */
    g_paste_clipboard_read_guard_touch (&update->guard);

    if (--update->pending > 0)
        return;

    if (!update->concluded)
        g_paste_clipboard_update_conclude (update);

    /* Anything a read that outlived the guard stored on its way in; the
     * conclusion released the rest of it where it stood. */
    g_paste_clipboard_update_release_content (update);
    g_paste_clipboard_read_guard_clear (&update->guard);

    /* Here and not at the conclusion, with everything the item was built from:
     * this is what a read was fired *with*, and a backend is free to have handed
     * the very pointer to its transfer -- the mutter one does, and that transfer
     * reads the string after the call returns. A guard concluding the update
     * leaves those reads running, so the string they are holding may only go
     * once every one of them has reported, which is here. */
    g_clear_pointer (&update->mime, g_free);

    /* Already gone once the update was concluded, which every path here has
     * been through. */
    g_clear_object (&update->provider);
    g_free (update);
}
