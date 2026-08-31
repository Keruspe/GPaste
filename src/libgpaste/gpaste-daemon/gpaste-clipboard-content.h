// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gdk/gdk.h>

#include <gpaste-3/gpaste-settings.h>

#include <gpaste-daemon/gpaste-clipboard-provider.h>
#include <gpaste-daemon/gpaste-item.h>
#include <gpaste-daemon/gpaste-special-mime.h>

G_BEGIN_DECLS

/* The single piece of content a clipboard provider currently holds, shared by
 * the GDK and mutter backends so the kind enum and its tagged value live in one
 * place. Only the field matching @kind is live at any time: prefer the get_*
 * accessors below, which check @kind, and check it by hand for the fields that
 * have none — reading the wrong member reinterprets unrelated bytes (a string
 * pointer as a GdkFileList *, an RGBA's floats as a pointer, …). */
typedef enum
{
    CLIPBOARD_CONTENT_NONE,
    CLIPBOARD_CONTENT_TEXT,
    CLIPBOARD_CONTENT_IMAGE,
    CLIPBOARD_CONTENT_FILE_LIST,
    CLIPBOARD_CONTENT_COLOR,
    /* The selection has an owner but only offers types we don't handle
     * (e.g. an image while images-support is disabled): not tracked, but
     * must not be overridden by ensure_not_empty either. */
    CLIPBOARD_CONTENT_IGNORED,
} GPasteClipboardContentKind;

typedef struct
{
    GPasteClipboardContentKind kind;
    union {
        gchar       *str;       /* TEXT: the text; IMAGE: the image checksum */
        GdkFileList *file_list; /* FILE_LIST */
        GdkRGBA      rgba;      /* COLOR */
    };
} GPasteClipboardContent;

void         g_paste_clipboard_content_clear              (GPasteClipboardContent       *content);
gboolean     g_paste_clipboard_content_is_empty           (const GPasteClipboardContent *content);
const gchar *g_paste_clipboard_content_get_text           (const GPasteClipboardContent *content);
const gchar *g_paste_clipboard_content_get_image_checksum (const GPasteClipboardContent *content);
GdkFileList *g_paste_clipboard_content_get_file_list      (const GPasteClipboardContent *content);

void g_paste_clipboard_content_set_text                (GPasteClipboardContent *content,
                                                        const gchar            *text);
void g_paste_clipboard_content_set_text_take           (GPasteClipboardContent *content,
                                                        gchar                  *text);
void g_paste_clipboard_content_set_image_checksum      (GPasteClipboardContent *content,
                                                        const gchar            *checksum);
void g_paste_clipboard_content_set_image_checksum_take (GPasteClipboardContent *content,
                                                        gchar                  *checksum);
void g_paste_clipboard_content_set_color               (GPasteClipboardContent *content,
                                                        const GdkRGBA          *rgba);
void g_paste_clipboard_content_set_file_list           (GPasteClipboardContent *content,
                                                        GdkFileList            *file_list);

/* What a backend should do with a candidate clipboard text, as decided by
 * g_paste_clipboard_content_classify_text() from the trim/size/dedup policy. */
typedef enum
{
    G_PASTE_CLIPBOARD_TEXT_REJECT,   /* too short/long, or unchanged: drop it */
    G_PASTE_CLIPBOARD_TEXT_SET,      /* cache @out_value as the new text */
    G_PASTE_CLIPBOARD_TEXT_RESELECT, /* re-own the selection with the stripped @out_value */
} GPasteClipboardTextAction;

GPasteClipboardTextAction g_paste_clipboard_content_classify_text (const GPasteClipboardContent *content,
                                                                   GPasteSettings               *settings,
                                                                   gboolean                      is_clipboard,
                                                                   const gchar                  *text,
                                                                   gchar                       **out_value);

gboolean     g_paste_clipboard_file_list_equal (GdkFileList *a,
                                                GdkFileList *b);

/* How long a batch of clipboard reads may go without a single one of them
 * landing. A selection transfer has no deadline of its own: an owner that dies
 * between advertising its targets and servicing the request, or an INCR transfer
 * that stops halfway, leaves the read pending for the rest of the session -- and
 * with it the item being built and the re-own a trimmed text is waiting for.
 * Here rather than in each backend, so a stuck owner is given the same rope
 * whichever one is reading it.
 *
 * Silence and not elapsed time, which is what g_paste_clipboard_read_guard_touch()
 * is for: how long a batch takes is the owner's to decide -- a large image or a
 * long file list over a forwarded display services its INCR chunks at whatever
 * pace it manages -- and cutting off a transfer that is demonstrably still
 * arriving loses a copy that was working. A read coming in says the batch is
 * moving and hands the rest of it the deadline again.
 *
 * Wide enough that one read which cannot report progress -- a single big
 * transfer, the only thing this batch is waiting on, so nothing lands to say it
 * is moving -- is not cut off either. The cost of waiting is a late item; the
 * cost of giving up early is no item at all. */
#define G_PASTE_CLIPBOARD_READ_TIMEOUT 30

/* The deadline a batch of clipboard reads runs under, and the #GCancellable
 * every one of them goes out on. Both backends embed one, so what running out
 * does -- and in which order -- is written down here rather than in each of
 * them: @expired concludes whatever the guard was armed for, and only then is
 * the cancel asked for.
 *
 * @cancellable is also what a read landing afterwards is told by
 * (g_paste_clipboard_read_guard_is_expired()): cancelling cannot fail these
 * reads (see the backends), but one that does come back can still ask whether
 * anything is left waiting for it. Both backends ask it, so it is asked here.
 *
 * @last_read is when the batch last moved and @timeout how much silence it is
 * allowed: what the timer measures is the gap between those two, which is why a
 * read landing writes a timestamp rather than replacing the source. The source
 * that finds the gap too short re-arms itself for what is left of it, so a batch
 * that keeps moving keeps one timer for its whole life however many reads it
 * fires. */
typedef struct
{
    GCancellable   *cancellable;
    guint           timeout_id;
    gint64          last_read;
    guint           timeout;
    GSourceOnceFunc expired;
    gpointer        user_data;
} GPasteClipboardReadGuard;

void     g_paste_clipboard_read_guard_arm        (GPasteClipboardReadGuard *guard,
                                                  GSourceOnceFunc           expired,
                                                  gpointer                  user_data);
void     g_paste_clipboard_read_guard_touch      (GPasteClipboardReadGuard *guard);
gboolean g_paste_clipboard_read_guard_is_expired (const GPasteClipboardReadGuard *guard);
void     g_paste_clipboard_read_guard_disarm     (GPasteClipboardReadGuard *guard);
void     g_paste_clipboard_read_guard_clear      (GPasteClipboardReadGuard *guard);

/* What a sync read has to keep alive: the selection its text is going to, ref'd
 * because the read spans main-loop iterations. Guarded like an update's reads,
 * and for the same reason -- an owner that stops answering leaves the read
 * pending for the rest of the session, and this one is reachable straight from
 * D-Bus. Concluding a sync is letting go of that target rather than publishing
 * anything, there being no text to publish; the data itself is freed only if the
 * read ever lands, cancelling being unable to fail it (see either backend). The
 * two backends then differ only in the read they fire. */
typedef struct
{
    GPasteClipboardProvider *other;
    GPasteClipboardReadGuard guard;
} GPasteClipboardSyncData;

GPasteClipboardSyncData *g_paste_clipboard_sync_data_new  (GPasteClipboardProvider *other);
void                     g_paste_clipboard_sync_data_free (GPasteClipboardSyncData *data);

/* Where the mime reads an update fires put their answers, and what one of them
 * having come back means. Both backends fire the same reads and want the same
 * thing done with the bytes, so what a read is worth keeping is written down once
 * here rather than in each of them. */
typedef struct
{
    GPasteBinaryData *special_mime[G_PASTE_SPECIAL_MIME_LAST];
} GPasteClipboardMimeResults;

void g_paste_clipboard_mime_results_store (GPasteClipboardMimeResults *results,
                                           GPasteSpecialMime           mime,
                                           GBytes                     *bytes);
void g_paste_clipboard_mime_results_clear (GPasteClipboardMimeResults *results);

/* What one mime read is for: the update it counts into and which entry it was
 * fired for. Neither is a backend's to interpret -- see the results above -- so
 * the pair travels as one, whichever of them is doing the reading. */
typedef struct
{
    gpointer          data;
    GPasteSpecialMime mime;
} GPasteClipboardMimeCtx;

GPasteClipboardMimeCtx *g_paste_clipboard_mime_ctx_new (gpointer          data,
                                                        GPasteSpecialMime mime);

/* Turn a finished clipboard read into the item it describes. Only the argument
 * matching @kind is looked at, and %NULL is answered for a kind that produced
 * nothing -- including NONE and IGNORED, so a backend that decided not to
 * produce anything just passes those.
 *
 * @special_mimes is the G_PASTE_SPECIAL_MIME_LAST-long array of alternative
 * representations gathered alongside; the ones that end up on the item are
 * stolen from it, and the rest are left for the caller to release. */
GPasteItem *g_paste_clipboard_content_to_item (GPasteClipboardContentKind kind,
                                               const gchar               *text,
                                               GdkTexture                *texture,
                                               GdkFileList               *file_list,
                                               const GdkRGBA             *rgba,
                                               GPasteBinaryData         **special_mimes);

/* One clipboard update in flight: the reads it fired, what they came back with,
 * and what concluding it means. Both backends fire the same reads for the same
 * content kinds and conclude them identically -- only the calls that fetch the
 * bytes differ -- so the whole of that lifecycle lives here, in the one place
 * both of them count their reads into.
 *
 * @provider is ref'd from the update being fired to it being concluded: it spans
 * main-loop iterations, and nothing else keeps it up. Everything that concludes
 * an update is reachable from it, so the guard concludes one without a backend
 * hook. Only to the conclusion, and not to the teardown, because a conclusion
 * the reads outlive would otherwise hold the provider for as long as they do,
 * which for one that never lands is the rest of the session.
 *
 * @pending counts the reads still out; @concluded says the item was already
 * built, the guard having run out with some of them still going. @mime is the
 * mimetype the content is being read under, for a backend that reads by
 * mimetype rather than by type -- %NULL for one that does not.
 *
 * @content_kind, @produced and the union are what the content read came back
 * with, owned rather than borrowed from the provider's cache: the reads still
 * running leave every route that publishes content free to overwrite that cache
 * -- a password countdown expiring, a Select, a SyncClipboardToPrimary -- each of
 * which frees what it was holding, and an update outlives its content callback
 * whenever anything else is still being read. A kind's read fills its member,
 * the builder is handed that member and no other, and the teardown releases that
 * one -- reading or freeing another reinterprets unrelated bytes (a string
 * pointer as a GdkFileList *, an RGBA's floats as a pointer, ...). @produced says
 * the read landed with something, a colour having no value that stands for none.
 * @reselect is a text read asking for the selection to be re-taken with what it
 * stripped off. */
typedef struct
{
    GPasteClipboardProvider              *provider;
    GPasteClipboardProviderUpdateCallback callback;
    gpointer                              user_data;

    GPasteClipboardReadGuard              guard;
    gint                                  pending;
    gboolean                              concluded;
    gchar                                *mime;

    GPasteClipboardContentKind            content_kind;
    gboolean                              produced;
    union {
        gchar       *text;
        GdkTexture  *texture;
        GdkFileList *file_list;
        GdkRGBA      rgba;
    };
    gboolean                              reselect;
    GPasteClipboardMimeResults            mimes;
} GPasteClipboardUpdate;

GPasteClipboardUpdate  *g_paste_clipboard_update_new           (GPasteClipboardProvider              *provider,
                                                                GPasteClipboardContentKind            content_kind,
                                                                GPasteClipboardProviderUpdateCallback callback,
                                                                gpointer                              user_data);
void                    g_paste_clipboard_update_add_read      (GPasteClipboardUpdate                *update);
GPasteClipboardMimeCtx *g_paste_clipboard_update_add_mime_read (GPasteClipboardUpdate                *update,
                                                                GPasteSpecialMime                     mime);
void                    g_paste_clipboard_update_on_mime_read  (GPasteClipboardMimeCtx               *ctx,
                                                                GBytes                               *bytes);
gboolean                g_paste_clipboard_update_is_expired    (const GPasteClipboardUpdate          *update);
void                    g_paste_clipboard_update_maybe_done    (GPasteClipboardUpdate                *update);

G_END_DECLS
