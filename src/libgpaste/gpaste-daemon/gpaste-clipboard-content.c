// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <string.h>

#include <gpaste-daemon/gpaste-clipboard-content.h>
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

    if (length < g_paste_settings_get_min_text_item_size (settings) ||
        length > g_paste_settings_get_max_text_item_size (settings))
        return G_PASTE_CLIPBOARD_TEXT_REJECT;

    if (content->kind == CLIPBOARD_CONTENT_TEXT && g_paste_str_equal (content->str, to_add))
        return G_PASTE_CLIPBOARD_TEXT_REJECT;

    /* Trimming changed the clipboard's own text: re-own it with the stripped form. */
    if (trim_items && is_clipboard && !g_paste_str_equal (text, stripped))
    {
        *out_value = g_steal_pointer (&stripped);
        return G_PASTE_CLIPBOARD_TEXT_RESELECT;
    }

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
 * g_paste_clipboard_read_guard_is_expired:
 * @guard: the #GPasteClipboardReadGuard a read went out under
 *
 * Returns: whether @guard's deadline has run out on the read asking
 *
 * What a read landing afterwards is told by. Nothing is waiting for it any more:
 * the batch it counted into was concluded, so what it brings back can only reach
 * the provider's cache -- where it would dedup, out of every later update, the
 * very content that never made it to the history.
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_read_guard_is_expired (const GPasteClipboardReadGuard *guard)
{
    g_return_val_if_fail (guard, TRUE);

    return g_cancellable_is_cancelled (guard->cancellable);
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
 *                 slot per #GPasteSpecialMime (%G_PASTE_SPECIAL_MIME_LAST of them)
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

    /* The text read asked for the selection to be re-owned with what it stripped
     * off. Done here rather than where that was decided, for two reasons: the
     * reads that say what this text *is* are still running against the owner we
     * would be replacing, and would come back empty; and what goes on the
     * selection is then the item, so a password keeps the hint that says it is a
     * secret -- which bare text cannot carry. Only a text read ever asks, and
     * select_item () takes every text item there is, so there is no refusal to
     * answer here: the one kind it can refuse is an image with no texture. */
    if (reselect && item)
        g_paste_clipboard_provider_select_item (provider, item);

    /* (transfer full) to the callback, and ours to release when there is none:
     * an update fired with no callback is part of the contract -- what a
     * backend's update () itself checks on every early return -- and what the
     * item holds can be a password's cleartext. */
    if (callback)
        callback (provider, g_steal_pointer (&item), user_data);
}

/* The guard ran out: conclude the update with what did arrive. Why it concludes
 * rather than failing the reads still out -- cancelling cannot fail them -- and
 * what that leaves behind is each backend's own answer, beside its update (). */
static void
g_paste_clipboard_update_timed_out (gpointer user_data)
{
    GPasteClipboardUpdate *update = user_data;

    g_debug ("%s: giving up on a clipboard read that never came back",
             g_paste_clipboard_provider_target_name (g_paste_clipboard_provider_is_clipboard (update->provider)));
    g_paste_clipboard_update_conclude (update);
}

/**
 * g_paste_clipboard_update_new:
 * @provider: the #GPasteClipboardProvider being read
 * @content_kind: the kind the content read is for
 * @callback: (scope async) (nullable): who to hand the item to
 * @user_data: what to hand it with
 *
 * Start an update, guard armed and one read outstanding
 *
 * The one read is the caller's own: a backend fires its reads and then counts
 * itself out with g_paste_clipboard_update_maybe_done(), so an update whose
 * reads all answered synchronously still concludes exactly once.
 *
 * Returns: (transfer full): the newly allocated #GPasteClipboardUpdate
 */
G_PASTE_VISIBLE GPasteClipboardUpdate *
g_paste_clipboard_update_new (GPasteClipboardProvider              *provider,
                              GPasteClipboardContentKind            content_kind,
                              GPasteClipboardProviderUpdateCallback callback,
                              gpointer                              user_data)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_PROVIDER (provider), NULL);

    GPasteClipboardUpdate *update = g_new0 (GPasteClipboardUpdate, 1);

    /* Ref'd for the whole update -- the content read plus every mime read --
     * released when it is: it spans main-loop iterations, and a provider being
     * disposed under one is what this is here to survive. */
    update->provider = g_object_ref (provider);
    update->callback = callback;
    update->user_data = user_data;
    update->pending = 1;
    update->content_kind = content_kind;

    g_paste_clipboard_read_guard_arm (&update->guard, g_paste_clipboard_update_timed_out, update);

    return update;
}

/**
 * g_paste_clipboard_update_is_expired:
 * @update: the #GPasteClipboardUpdate a read counted into
 *
 * Returns: whether @update has already moved on without the read asking
 *
 * What a read landing asks before it touches anything but the counter, the
 * guard alone answering it a moment too late: a conclusion disarms the guard
 * and takes the provider over, and only then cancels -- the cancellation being
 * what runs the handlers that could free the very update being concluded (see
 * g_paste_clipboard_read_guard_timed_out ()). A read landing between the two
 * finds a guard that has not expired and an update with no provider left.
 */
G_PASTE_VISIBLE gboolean
g_paste_clipboard_update_is_expired (const GPasteClipboardUpdate *update)
{
    g_return_val_if_fail (update, TRUE);

    return update->concluded || g_paste_clipboard_read_guard_is_expired (&update->guard);
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
 * @mime: which entry of which list it is being fired for
 * @sensitive: whether that list is the sensitive one
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
