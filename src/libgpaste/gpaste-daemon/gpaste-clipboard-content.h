// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gdk/gdk.h>

#include <gpaste-3/gpaste-settings.h>

#include <gpaste-daemon/gpaste-clipboard-provider.h>
#include <gpaste-daemon/gpaste-item.h>
#include <gpaste-daemon/gpaste-sensitive-mime.h>
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
void         g_paste_clipboard_content_set_ignored        (GPasteClipboardContent       *content);
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
 * g_paste_clipboard_content_classify_text() from the trim/size/dedup policy.
 *
 * UNCHANGED and DROP both mean there is no item to build, and they are not the
 * same answer about the selection: one is standing still, holding the very text
 * the cache names, and the other has moved to a text nothing here keeps. Only
 * the first leaves the cache alone. */
typedef enum
{
    G_PASTE_CLIPBOARD_TEXT_UNCHANGED, /* the selection is standing still: leave the cache alone */
    G_PASTE_CLIPBOARD_TEXT_DROP,      /* the size policy turns it down: no @out_value to cache */
    G_PASTE_CLIPBOARD_TEXT_SET,       /* cache @out_value, or the text itself when %NULL */
    G_PASTE_CLIPBOARD_TEXT_RESELECT,  /* re-own the selection with the stripped @out_value */
} GPasteClipboardTextAction;

GPasteClipboardTextAction g_paste_clipboard_content_classify_text (const GPasteClipboardContent *content,
                                                                   GPasteSettings               *settings,
                                                                   gboolean                      may_reselect,
                                                                   const gchar                  *text,
                                                                   gboolean                      sensitive,
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
 * Silence and not elapsed time, which is what
 * g_paste_clipboard_read_guard_touch() is for: how long a batch takes is the
 * owner's to decide -- a large image or a long file list over a forwarded
 * display services its INCR chunks at whatever pace it manages -- and cutting
 * off a transfer that is demonstrably still arriving loses a copy that was
 * working. A read coming in says the batch is moving and hands the rest of it
 * the deadline again.
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
 * @cancellable is for failing a read that stalls partway, and is not what says
 * the deadline is past: g_paste_clipboard_update_is_expired () answers that, off
 * the update itself, for the window a conclusion leaves between taking the
 * provider over and asking for the cancel. Cancelling cannot fail the conversion
 * request an X11 read starts with (see the backends), but the transfer through
 * the stream that request returns is another matter:
 * gdk_content_deserialize_async () and the g_output_stream_splice_async () a
 * mime read ends in both honour it, so a transfer that stops halfway is failed
 * by the cancel and counts itself out rather than holding the update for the
 * session. Those take a #GCancellable and nothing else, which is why this is one
 * rather than a flag.
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

void g_paste_clipboard_read_guard_arm    (GPasteClipboardReadGuard *guard,
                                          GSourceOnceFunc           expired,
                                          gpointer                  user_data);
void g_paste_clipboard_read_guard_touch  (GPasteClipboardReadGuard *guard);
void g_paste_clipboard_read_guard_disarm (GPasteClipboardReadGuard *guard);
void g_paste_clipboard_read_guard_clear  (GPasteClipboardReadGuard *guard);

/* What a sync read has to keep alive: the selection its text is going to, ref'd
 * because the read spans main-loop iterations. Guarded like an update's reads,
 * and for the same reason -- an owner that stops answering leaves the read
 * pending for the rest of the session, and this one is reachable straight from
 * D-Bus. Concluding a sync is letting go of that target rather than publishing
 * anything, there being no text to publish; the data itself is freed only if the
 * read ever lands, cancelling being unable to fail it (see either backend) --
 * @user_data excepted, which the conclusion releases too, that one being a ref
 * on everything the caller put behind it rather than a struct. The two backends
 * then differ only in the read they fire. */
typedef struct
{
    GPasteClipboardProvider    *other;
    GPasteClipboardReadGuard    guard;
    GPasteClipboardSyncCallback callback;
    gpointer                    user_data;
    GDestroyNotify              destroy;
} GPasteClipboardSyncData;

GPasteClipboardSyncData *g_paste_clipboard_sync_data_new        (GPasteClipboardProvider    *other,
                                                                 GPasteClipboardSyncCallback callback,
                                                                 gpointer                    user_data,
                                                                 GDestroyNotify              destroy);
gboolean                 g_paste_clipboard_sync_data_wants_text (const GPasteClipboardSyncData *data);
void                     g_paste_clipboard_sync_data_deliver    (GPasteClipboardSyncData *data,
                                                                 const gchar             *text);
void                     g_paste_clipboard_sync_data_free       (GPasteClipboardSyncData *data);

/* Where the mime reads an update fires put their answers. Both lists are read
 * the same way and differ only in what their bytes then mean -- a representation
 * of the content to keep, or a fact about it -- so what tells them apart is a
 * policy neither backend owns.
 *
 * @sensitive is a hint that came back and matched, and nothing else: an update
 * concluded on its deadline with that read still out leaves it %FALSE and the
 * text lands as text. A secret is the exception among the things that get
 * copied, so calling one on a read that never answered would mask ordinary text
 * -- every copy of it, for as long as its owner keeps not answering -- on no
 * evidence whatever. Only proof that a text is a secret makes it one, and the
 * price of that is the reverse case: an owner that serves the text and then
 * stops answering has its marked password land in the clear, for the one copy
 * whose hint the guard outlasted.
 *
 * @sensitive_unknown is that same silence read the other way, for the one reader
 * that needs it: an answer that never came is not a selection saying its content
 * is ordinary, and a record already kept for that selection has to survive it.
 * Set where nothing could be learned -- an offer-list read failed, or a hint
 * the selection offers whose read failed, or was still out when the deadline
 * ran -- and turned into the #GPasteClipboardSecret a provider's callers are
 * handed, proof still being what makes a password. A hint served empty is not
 * that: the owner answered, with a value that does not match. */
typedef struct
{
    gboolean          sensitive;
    gboolean          sensitive_unknown;
    GPasteBinaryData *special_mime[G_PASTE_SPECIAL_MIME_LAST];
} GPasteClipboardMimeResults;

void g_paste_clipboard_mime_results_clear (GPasteClipboardMimeResults *results);

/* What one mime read is for: the update it counts into, and which entry of which
 * list it was fired for. Neither is a backend's to interpret -- see the results
 * above -- so the two travel as one, whichever of them is doing the reading, and
 * what they are is this file's alone: a backend carries the pointer from the
 * call that fires a read to the one that reports it and never looks inside. That
 * is also what keeps the two mime enums apart, each entry point below taking its
 * own -- there is no one integer both lists index. */
typedef struct _GPasteClipboardMimeCtx GPasteClipboardMimeCtx;

void g_paste_clipboard_mime_ctx_free (GPasteClipboardMimeCtx *ctx);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPasteClipboardMimeCtx, g_paste_clipboard_mime_ctx_free)

/* Turn a finished clipboard read into the item it describes. Only the argument
 * matching @kind is looked at, and %NULL is answered for a kind that produced
 * nothing -- including NONE and IGNORED, so a backend that decided not to
 * produce anything just passes those.
 *
 * @sensitive says the owner marked its text a secret (see #GPasteSensitiveMime),
 * which is what makes it a password rather than a text item; @settings is where
 * the timeout such a password starts with comes from.
 *
 * @special_mimes is the G_PASTE_SPECIAL_MIME_LAST-long array of alternative
 * representations gathered alongside; the ones that end up on the item are
 * stolen from it, and the rest are left for the caller to release. */
GPasteItem *g_paste_clipboard_content_to_item (GPasteSettings            *settings,
                                               GPasteClipboardContentKind kind,
                                               const gchar               *text,
                                               GdkTexture                *texture,
                                               GdkFileList               *file_list,
                                               const GdkRGBA             *rgba,
                                               gboolean                   sensitive,
                                               GPasteBinaryData         **special_mimes);

/* One clipboard update in flight: the reads it fired, what they came back with,
 * and what concluding it means. Both backends fire the same reads for the same
 * content kinds and conclude them identically -- only the calls that fetch the
 * bytes differ -- so the whole of that lifecycle lives here, in the one place
 * both of them count their reads into.
 *
 * @provider and @settings are ref'd from the update being fired to it being
 * concluded: it spans main-loop iterations, and nothing else keeps either of
 * them up. Everything that concludes an update is reachable from those two, so
 * the guard concludes it without a backend hook. Only to the conclusion, and not
 * to the teardown, because a conclusion the reads outlive would otherwise hold
 * both of them for as long as those reads do, which for ones that never land is
 * the rest of the session.
 *
 * @pending counts the reads still out and @sensitive_pending how many of those
 * are hint or sensitive-offer reads, which is what tells a hint that answered
 * "no" from one that never answered at all; @concluded says the item was already
 * built, the guard having run out with some of them still going. @mime is the mimetype the
 * content is being read under, for a backend that reads by mimetype rather than
 * by type -- %NULL for one that does not.
 *
 * @superseded says the selection moved on while this update was reading it, so
 * what the reads still out would bring back describes content nothing holds any
 * more: the conclusion builds no item and publishes nothing. Kept apart from
 * @concluded rather than folded into it because the two say opposite things
 * about what is still owed -- a superseded update has yet to call its callback,
 * and that callback is what releases whatever the caller put behind it.
 *
 * @slot is where the backend keeps its pointer to the update in flight on this
 * selection, cleared at the conclusion: one goes out per
 * #GPasteClipboardProvider::changed and nothing in the reads orders two of them,
 * which conclude as their owners answer rather than as they were asked. This is
 * what supplies that order, and the only thing that can tell an update it has
 * been overtaken. g_paste_clipboard_update_new () supersedes what it finds
 * there, so an update that starts one never has to remember to; what a backend
 * does owe is the same call on the changes it answers *without* starting one.
 * @cache belongs to that provider and holds the last completed content for
 * deduplication. Reads fill this update alone; only its conclusion writes the
 * cache, so abandoning a read cannot suppress a subsequent copy of its value.
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
 * @unchanged confirms an exact match with the cache. A read producing no item
 * without this confirmation leaves the selection unidentified, not empty.
 * @reselect asks for the selection to be re-owned after all reads complete:
 * trimmed text needs its normalized value published, and GDK retains images. */
typedef struct _GPasteClipboardUpdate GPasteClipboardUpdate;

struct _GPasteClipboardUpdate
{
    GPasteClipboardProvider              *provider;
    GPasteSettings                       *settings;
    GPasteClipboardProviderUpdateCallback callback;
    gpointer                              user_data;

    GPasteClipboardReadGuard              guard;
    gint                                  pending;
    gint                                  sensitive_pending;
    gboolean                              concluded;
    gboolean                              superseded;
    GPasteClipboardUpdate               **slot;
    GPasteClipboardContent               *cache;
    gchar                                *mime;

    GPasteClipboardContentKind            content_kind;
    gboolean                              produced;
    gboolean                              unchanged;
    union {
        gchar       *text;
        GdkTexture  *texture;
        GdkFileList *file_list;
        GdkRGBA      rgba;
    };
    gboolean                              reselect;
    GPasteClipboardMimeResults            mimes;
};

/* What a backend answers about the selection it is reading: whether that
 * selection offers @mimetype, and how a read of one is fired. @offer is whatever
 * the backend has to consult to answer the first -- a GdkContentFormats, a list
 * of mimetype strings -- and it is opaque to everything but the two of them.
 *
 * Which mimetypes are worth asking about, and for which kind of content, is not
 * one of the questions: that is the same policy for either backend, and it lives
 * in g_paste_clipboard_update_read_special_mimes () and
 * g_paste_clipboard_update_read_sensitive_mimes () so that a mimetype gained, or
 * a condition put on one, is written once rather than once per backend. A
 * backend may answer the two against different @offer -- the GDK one answers
 * for the hints from the selection's TARGETS rather than from its formats -- but
 * every answer is still whether the selection offers @mimetype. */
typedef gboolean (*GPasteClipboardMimeOfferedFunc) (gconstpointer           offer,
                                                    const gchar            *mimetype);
typedef void     (*GPasteClipboardMimeReadFunc)    (gpointer                backend,
                                                    const gchar            *mimetype,
                                                    GCancellable           *cancellable,
                                                    GPasteClipboardMimeCtx *ctx);

void                    g_paste_clipboard_update_supersede                (GPasteClipboardUpdate               **slot);
GPasteClipboardUpdate  *g_paste_clipboard_update_new                      (GPasteClipboardProvider              *provider,
                                                                           GPasteSettings                       *settings,
                                                                           GPasteClipboardContentKind            content_kind,
                                                                           GPasteClipboardUpdate               **slot,
                                                                           GPasteClipboardContent               *cache,
                                                                           GPasteClipboardProviderUpdateCallback callback,
                                                                           gpointer                              user_data);
void                    g_paste_clipboard_update_add_read                 (GPasteClipboardUpdate                *update);
GPasteClipboardMimeCtx *g_paste_clipboard_update_add_special_mime_read    (GPasteClipboardUpdate                *update,
                                                                           GPasteSpecialMime                     mime);
GPasteClipboardMimeCtx *g_paste_clipboard_update_add_sensitive_mime_read  (GPasteClipboardUpdate                *update,
                                                                           GPasteSensitiveMime                   mime);
void                    g_paste_clipboard_update_add_sensitive_offer_read (GPasteClipboardUpdate                *update);
void                    g_paste_clipboard_update_on_sensitive_offer_read  (GPasteClipboardUpdate                *update,
                                                                           gboolean                              answered);
void                    g_paste_clipboard_update_read_special_mimes       (GPasteClipboardUpdate                *update,
                                                                           gconstpointer                         offer,
                                                                           GPasteClipboardMimeOfferedFunc        offered,
                                                                           gpointer                              backend,
                                                                           GPasteClipboardMimeReadFunc           read);
gboolean                g_paste_clipboard_update_wants_sensitive_mimes    (const GPasteClipboardUpdate          *update);
void                    g_paste_clipboard_update_read_sensitive_mimes     (GPasteClipboardUpdate                *update,
                                                                           gconstpointer                         offer,
                                                                           GPasteClipboardMimeOfferedFunc        offered,
                                                                           gpointer                              backend,
                                                                           GPasteClipboardMimeReadFunc           read);
void                    g_paste_clipboard_update_read_mimes               (GPasteClipboardUpdate                *update,
                                                                           gconstpointer                         offer,
                                                                           GPasteClipboardMimeOfferedFunc        offered,
                                                                           gpointer                              backend,
                                                                           GPasteClipboardMimeReadFunc           read);
void                    g_paste_clipboard_update_on_mime_read             (GPasteClipboardMimeCtx               *ctx,
                                                                           GBytes                               *bytes);
gboolean                g_paste_clipboard_update_is_expired               (const GPasteClipboardUpdate          *update);
void                    g_paste_clipboard_update_maybe_done               (GPasteClipboardUpdate                *update);

G_END_DECLS
