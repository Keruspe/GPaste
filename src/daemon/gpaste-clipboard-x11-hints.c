// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* Reading the sensitive hints off an X11 selection, around GDK.
 *
 * A workaround, kept apart so it can be deleted whole. It exists because GTK
 * cannot be asked either half of the question: gdk_x11_clipboard_formats_from_atoms ()
 * (gdk/x11/gdkclipboard-x11.c) drops "x-kde-passwordManagerHint" from a
 * selection's formats, keeping only the TARGETS atoms that name a mimetype or one
 * of its text targets, and GDK's own reads cannot be trusted with TARGETS or a
 * hint (see convert ()). Once GTK keeps those atoms (GTK MR !10321's second patch)
 * and its selection input streams match replies by requestor or property, the GDK
 * backend can go back to g_paste_clipboard_update_read_mimes () against its
 * formats, and this file, its tests and the xfixes dependency can go. */

#include "gpaste-clipboard-x11-hints.h"

#include <gdk/x11/gdkx.h>

#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>

struct _GPasteClipboardX11Hints
{
    GObject parent_instance;

    GdkDisplay *display;

    /* What converting the selection takes: the conversions still waiting for
     * their reply, the windows retired from ones that may still get one, the
     * atoms every conversion names, and the handler their replies are picked out
     * of GDK's events by. @xevent_id is 0 where none of this could be set up,
     * which is what g_paste_clipboard_x11_hints_available () answers: every
     * conversion would then answer nothing, and a backend reads the hints GDK
     * does offer instead. */
    GSList     *conversions;
    /* GPasteClipboardX11HintsRetiredWindow, each on its own deadline. */
    GSList     *retired_windows;
    Atom        xselection;
    Atom        property_atom;
    Atom        targets_atom;
    Atom        incr_atom;
    /* The sensitive mimes as atoms, interned once: see new (). */
    Atom        sensitive_atoms[G_PASTE_SENSITIVE_MIME_LAST];
    gulong      xevent_id;
    /* When the current owner took this selection, which every conversion of ours
     * names: ICCCM has an owner refuse a conversion timestamped before it owned
     * anything. It comes past in the XFixes notification GDK already listens for
     * -- the one that makes a selection change reach the GDK backend at all.
     * GdkX11Clipboard tracks the same timestamp but keeps it private, hence
     * XFixes here.
     *
     * A notification only comes with a change, so the reads made before the
     * first one have none to name -- and those are the daemon's bootstrap reads,
     * the very ones a password already sitting on the selection is recognised
     * by. They name the server's clock as of startup instead (server_time ()),
     * which the current owner acquired the selection before and so accepts;
     * CurrentTime, which GDK's own reads use there, is what an owner applying
     * that ICCCM rule refuses outright. */
    gint        xfixes_event_base;
    Time        selection_timestamp;
};

G_PASTE_DEFINE_TYPE (ClipboardX11Hints, clipboard_x11_hints, G_TYPE_OBJECT)

/* @data: (nullable): what the owner wrote, %NULL when it wrote nothing usable.
 * @timestamp is the one the conversion was made with, for a follow-up that has
 * to reach the same owner. */
typedef void (*GPasteClipboardX11HintsCallback) (GPasteClipboardX11Hints *self,
                                                 Time                     timestamp,
                                                 Atom                     type,
                                                 gint                     format,
                                                 const guchar            *data,
                                                 gulong                   n_items,
                                                 gpointer                 user_data);

typedef struct
{
    GPasteClipboardX11Hints        *self;        /* ref'd for the conversion's life */
    GCancellable                   *cancellable; /* ref'd: what says it still matters */
    gulong                          cancelled_id;
    Window                          window;      /* the requestor: what names its reply */
    Time                            timestamp;
    GPasteClipboardX11HintsCallback callback;
    gpointer                        user_data;
} GPasteClipboardX11HintsConversion;

/* How long a requestor window whose conversion was given up on is kept before it
 * is destroyed anyway, in seconds.
 *
 * Twice the rope a stuck transfer is given, so an owner the update itself was
 * still waiting on has long since answered or stopped existing. It is a bound
 * and not a policy change: without one, every conversion nobody answers leaves a
 * window and its property on the server for the life of the daemon -- one per
 * copy made by an owner that publishes targets it will not serve -- and the
 * reply lookup in on_xevent (), which walks this list on every SelectionNotify,
 * grows with them. */
#define G_PASTE_CLIPBOARD_X11_HINTS_RETIRE_TIMEOUT (2 * G_PASTE_CLIPBOARD_READ_TIMEOUT)

/* How long server_time () waits for the server to timestamp its own property
 * change, in milliseconds.
 *
 * A bound on a round trip, and sized as one: an answer that is coming is here in
 * microseconds on a local server and in tens of milliseconds on a forwarded one,
 * so what is left past this is the server server_time () describes, which does
 * not deliver the event at all -- not something more patience finds out, while
 * every millisecond of it is startup the bus name, the storage concerns and the
 * migration prompt wait behind.
 *
 * Short is affordable because of what little rides on being wrong. The fallback
 * is CurrentTime, which is what GDK's own selection reads pass: an owner refusing
 * it would be refusing GTK too, so pasting from that application into any GTK app
 * would already be failing. And the difference decides one thing -- whether text
 * already sitting on a selection when the daemon starts is recognised as a
 * password before the next copy replaces it (@selection_timestamp) -- for the
 * reads made before the first ownership notification, every conversion after one
 * carrying a real timestamp instead. */
#define G_PASTE_CLIPBOARD_X11_HINTS_SERVER_TIME_TIMEOUT 250

/* A requestor window kept past its conversion: see release_window ().
 *
 * The hints object is held weakly, its own dispose () being what ends a
 * retirement early (see AGENTS.md on what an object tracks so that its own
 * dispose() can end it): a reference here would put that dispose () out of
 * reach, and a borrowed pointer would rest on the list being freed ahead of the
 * display -- true today, and nothing states it. Only the deadline opens it; the
 * reply and dispose () already hold the object (see end_retirement ()). */
typedef struct
{
    GWeakRef self;
    Window   window;
    guint    source_id;
} GPasteClipboardX11HintsRetiredWindow;

static void
g_paste_clipboard_x11_hints_destroy_window (GPasteClipboardX11Hints *self,
                                            Window                   window)
{
    GdkDisplay *display = self->display;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gdk_x11_display_error_trap_push (display);
    XDestroyWindow (gdk_x11_display_get_xdisplay (display), window);
    gdk_x11_display_error_trap_pop_ignored (display);
    G_GNUC_END_IGNORE_DEPRECATIONS
}

/* The record alone: the window is destroyed by end_retirement (), which every
 * caller ending a retirement goes through with the hints object in hand. */
static void
g_paste_clipboard_x11_hints_retired_window_free (GPasteClipboardX11HintsRetiredWindow *retired)
{
    g_clear_handle_id (&retired->source_id, g_source_remove);
    g_weak_ref_clear (&retired->self);
    g_free (retired);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPasteClipboardX11HintsRetiredWindow, g_paste_clipboard_x11_hints_retired_window_free)

/* End a retirement, taking the window with it: called by whichever of the reply,
 * the deadline and dispose () gets there first.
 *
 * @self is passed in rather than opened from the record's weak reference, and
 * has to be: dispose () is one of the three, and when it runs
 * from the last g_object_unref () GLib has already cleared every weak reference
 * to the object -- g_weak_ref_get () answers %NULL there, so a window destroyed
 * only through the record's own reference would be left on the server until the
 * connection closes (/x11-hints/dispose-retired). */
static void
g_paste_clipboard_x11_hints_end_retirement (GPasteClipboardX11Hints              *self,
                                            GPasteClipboardX11HintsRetiredWindow *retired)
{
    g_autoptr (GPasteClipboardX11HintsRetiredWindow) owned = retired;

    self->retired_windows = g_slist_remove (self->retired_windows, retired);
    g_paste_clipboard_x11_hints_destroy_window (self, retired->window);
}

static void
g_paste_clipboard_x11_hints_retired_window_timed_out (gpointer user_data)
{
    GPasteClipboardX11HintsRetiredWindow *retired = user_data;
    g_autoptr (GPasteClipboardX11Hints) self = g_weak_ref_get (&retired->self);

    /* This is the source firing, so it is spent: dropped before the free below
     * can reach for g_source_remove () on it. */
    retired->source_id = 0;

    g_debug ("clipboard: destroying a requestor window nobody came back to");
    /* dispose () removes this source along with the record, so a hints object
     * gone by now is one whose teardown is still on the stack: there is nothing
     * left to destroy the window with. */
    if (self)
        g_paste_clipboard_x11_hints_end_retirement (self, retired);
    else
        g_paste_clipboard_x11_hints_retired_window_free (retired);
}

/* Let go of a conversion's requestor window. Destroyed once nothing can be
 * written to it any more -- answered or refused -- and otherwise retired until
 * whatever is owed arrives (on_xevent ()), its deadline passes, or dispose: an
 * owner answering a window that is gone gets BadWindow, which an Xlib client with
 * no error handler of its own dies of. That is why the window outlives the
 * conversion at all, and why the deadline is generous rather than tight. */
static void
g_paste_clipboard_x11_hints_release_window (GPasteClipboardX11Hints *self,
                                            Window                   window,
                                            gboolean                 done)
{
    if (done)
    {
        g_paste_clipboard_x11_hints_destroy_window (self, window);
        return;
    }

    GPasteClipboardX11HintsRetiredWindow *retired = g_new0 (GPasteClipboardX11HintsRetiredWindow, 1);

    g_weak_ref_init (&retired->self, self);
    retired->window = window;
    retired->source_id = g_timeout_add_seconds_once (G_PASTE_CLIPBOARD_X11_HINTS_RETIRE_TIMEOUT,
                                                     g_paste_clipboard_x11_hints_retired_window_timed_out,
                                                     retired);
    g_source_set_name_by_id (retired->source_id, "[GPaste] retired selection requestor");
    self->retired_windows = g_slist_prepend (self->retired_windows, retired);
}

/* Report one conversion and free it. Every path ends here: it counts into an
 * update, which cannot conclude while it is out. */
static void
g_paste_clipboard_x11_hints_conversion_finish (GPasteClipboardX11HintsConversion *conversion,
                                               gboolean                           done,
                                               Atom                               type,
                                               gint                               format,
                                               const guchar                      *data,
                                               gulong                             n_items)
{
    g_autofree GPasteClipboardX11HintsConversion *owned = conversion;
    g_autoptr (GPasteClipboardX11Hints) self = conversion->self;
    g_autoptr (GCancellable) cancellable = conversion->cancellable;

    self->conversions = g_slist_remove (self->conversions, conversion);

    if (conversion->cancelled_id)
        g_cancellable_disconnect (cancellable, conversion->cancelled_id);

    g_paste_clipboard_x11_hints_release_window (self, conversion->window, done);
    conversion->callback (self, conversion->timestamp, type, format, data, n_items, conversion->user_data);
}

static void
g_paste_clipboard_x11_hints_on_conversion_cancelled (GCancellable *cancellable G_GNUC_UNUSED,
                                                     gpointer      user_data)
{
    GPasteClipboardX11HintsConversion *conversion = user_data;

    /* Disconnecting from inside the handler is not allowed, and not needed. */
    conversion->cancelled_id = 0;

    /* Unlike GDK's reads, this one can be given up on; its owner may still
     * answer, so its window is retired rather than destroyed. */
    g_debug ("clipboard: giving up on a conversion that never came back");
    g_paste_clipboard_x11_hints_conversion_finish (conversion, FALSE, None, 0, NULL, 0);
}

/* A reply to one of our requestor windows, or the XFixes notification carrying
 * the owner's acquisition time (see @selection_timestamp). */
static gboolean
g_paste_clipboard_x11_hints_on_xevent (GdkDisplay              *display,
                                       gpointer                 xev,
                                       GPasteClipboardX11Hints *self)
{
    const XEvent *xevent = xev;

    /* The event base is set: this handler is connected for an XFixes display and
     * for no other (see new ()). */
    if (xevent->type - self->xfixes_event_base == XFixesSelectionNotify)
    {
        const XFixesSelectionNotifyEvent *notify = xev;

        if (notify->selection == self->xselection)
            self->selection_timestamp = notify->selection_timestamp;

        return FALSE;
    }

    if (xevent->type != SelectionNotify)
        return FALSE;

    Window window = xevent->xselection.requestor;
    GPasteClipboardX11HintsConversion *conversion = NULL;
    GPasteClipboardX11HintsRetiredWindow *retired = NULL;

    for (GSList *l = self->conversions; l && !conversion; l = l->next)
    {
        if (((GPasteClipboardX11HintsConversion *) l->data)->window == window)
            conversion = l->data;
    }

    for (GSList *l = self->retired_windows; l && !conversion && !retired; l = l->next)
    {
        if (((GPasteClipboardX11HintsRetiredWindow *) l->data)->window == window)
            retired = l->data;
    }

    if (!conversion && !retired)
        return FALSE;

    Atom property = xevent->xselection.property;
    Atom type = None;
    gint format = 0;
    gulong n_items = 0;
    gulong remaining = 0;
    guchar *data = NULL;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *xdisplay = gdk_x11_display_get_xdisplay (display);

    gdk_x11_display_error_trap_push (display);
    if (property != None)
    {
        XGetWindowProperty (xdisplay, window, property, 0, 0x1FFFFFFF, False, AnyPropertyType,
                            &type, &format, &n_items, &remaining, &data);
    }
    gdk_x11_display_error_trap_pop_ignored (display);
    G_GNUC_END_IGNORE_DEPRECATIONS

    /* An INCR reply is not read: deleting its property is what would have the
     * owner start writing chunks, so the property stays and the window with it.
     * Every other reply leaves nothing more to come, and the window goes -- its
     * property along with it. */
    gboolean done = (type != self->incr_atom);
    gboolean usable = (done && data && !remaining);

    if (conversion)
        g_paste_clipboard_x11_hints_conversion_finish (conversion, done, type, format, (usable) ? data : NULL, (usable) ? n_items : 0);
    else if (done)
    {
        g_debug ("clipboard: a reply came back for a conversion that is over");
        g_paste_clipboard_x11_hints_end_retirement (self, retired);
    }

    if (data)
        XFree (data);

    /* Ours either way: it is addressed to a window GDK knows nothing about. */
    return TRUE;
}

/* Convert this selection to @target and hand back what the owner wrote.
 *
 * GDK cannot be asked this reliably. gdk_clipboard_read_async () -- which since
 * GTK 4.24 no longer crashes on "TARGETS" -- goes through a selection input
 * stream that takes the first SelectionNotify naming its selection and target
 * (gdk/x11/gdkselectioninputstream-x11.c), whatever property or requestor it
 * names: every read of one target waits in one slot, and for TARGETS that slot is
 * shared with the request GTK makes itself on every owner change. One reply that
 * never comes -- an owner that serves the text and exits -- has every later reply
 * land on the request before it for the rest of the session: copies lost, hints
 * read for the wrong copy, passwords stored as text.
 *
 * So each conversion is made on an InputOnly requestor window of its own, which
 * every SelectionNotify names, refusals included: a reply is always attributable,
 * conversions run side by side, and one given up on cannot answer another. A
 * window costs no round trip to make and, unlike an atom, is freed when
 * destroyed. The reply is picked out of the events GDK is pumping anyway. INCR is
 * not supported: what this reads -- a target list, a hint -- is a handful of
 * bytes, and an INCR reply answers nothing.
 *
 * One of these per text copy, on the primary selection as much as on the
 * clipboard, is affordable because none of it waits on anything: creating the
 * window, asking for the selection and destroying it again are requests with no
 * reply, and the trap around them is popped without a sync, so what a copy costs
 * is bytes on the wire and a window the server holds until the reply lands. A
 * refusal is a reply, so an owner offering targets it will not serve gets its
 * window back at once; only a conversion nobody answers at all leaves one
 * standing, and then for a bounded time (release_window ()).
 *
 * Hence no pool of windows to hand on to the next conversion: it would save two
 * no-reply requests per copy and cost this file a lifetime rule it does not
 * otherwise need. And no sharing one window across conversions either -- the
 * requestor is the whole of the attribution, a refusal naming property None and
 * carrying nothing else to tell one request from another.
 *
 * @timestamp should be the owner's acquisition time as of the copy being
 * classified, so a newer owner refuses the conversion rather than answering for
 * a copy it never made. @callback is required and reports on every path. */
static void
g_paste_clipboard_x11_hints_convert (GPasteClipboardX11Hints        *self,
                                     Atom                            target,
                                     Time                            timestamp,
                                     GCancellable                   *cancellable,
                                     GPasteClipboardX11HintsCallback callback,
                                     gpointer                        user_data)
{
    /* @cancellable is required, and refused here rather than asserted on: it is
     * the only thing that ends a conversion nobody answers -- there is no
     * deadline of our own on a live one -- so starting one without it would hold
     * a requestor window indefinitely if its owner never replied.
     * Every caller passes the update's guard, which read_guard_arm () always
     * creates; this is what keeps that a requirement rather than a habit. */
    if (!self->xevent_id || target == None || !cancellable || g_cancellable_is_cancelled (cancellable))
    {
        callback (self, timestamp, None, 0, NULL, 0, user_data);
        return;
    }

    GdkDisplay *display = self->display;
    GPasteClipboardX11HintsConversion *conversion = g_new0 (GPasteClipboardX11HintsConversion, 1);

    /* Completion belongs to the update's guard, not to hints disposal. Its
     * deadline cancels this operation even if the owner stays silent; the strong
     * ref keeps the display and event dispatcher usable until that completion.
     * Unlike formats waits, conversions do not depend on dispose to end them --
     * which is why a backend standing down supersedes the update that owns the
     * guard (g_paste_clipboard_gdk_dispose ()) rather than only its formats wait:
     * that brings the cancellation forward to the next main-loop turn, where
     * leaving the deadline to run would hold this display and this window for the
     * whole of it, long after the provider let go. */
    conversion->self = g_object_ref (self);
    conversion->cancellable = g_object_ref (cancellable);
    conversion->timestamp = timestamp;
    conversion->callback = callback;
    conversion->user_data = user_data;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *xdisplay = gdk_x11_display_get_xdisplay (display);

    /* Trapped: an error on a request of ours reaches GDK's default handler
     * otherwise, which ends the process. One that could not be made or sent is
     * never answered, and its cancellable ends it like any other. */
    gdk_x11_display_error_trap_push (display);
    conversion->window = XCreateWindow (xdisplay, DefaultRootWindow (xdisplay), -100, -100, 1, 1, 0,
                                        CopyFromParent, InputOnly, CopyFromParent, 0, NULL);
    XConvertSelection (xdisplay, self->xselection, target, self->property_atom, conversion->window, timestamp);
    gdk_x11_display_error_trap_pop_ignored (display);
    G_GNUC_END_IGNORE_DEPRECATIONS

    self->conversions = g_slist_prepend (self->conversions, conversion);

    /* Connected once listed, since the handler finishes the conversion.
     *
     * The id is written back only where the handler did not run: a cancellable
     * already cancelled has g_cancellable_connect () dispatch inline and answer
     * 0, and that handler finishes the conversion, which frees it -- so an
     * unconditional assignment here is a write to memory the connect released.
     * The check at the top of this function is what makes that unreachable
     * today, two X requests earlier and on the caller's word that nothing
     * cancels in between; this costs one branch and does not rely on either. */
    gulong cancelled_id = g_cancellable_connect (cancellable,
                                                 G_CALLBACK (g_paste_clipboard_x11_hints_on_conversion_cancelled),
                                                 conversion,
                                                 NULL);

    if (cancelled_id)
        conversion->cancelled_id = cancelled_id;
}

static Atom
g_paste_clipboard_x11_hints_sensitive_atom (GPasteClipboardX11Hints *self,
                                            const gchar             *mimetype)
{
    for (GPasteSensitiveMime mime = G_PASTE_SENSITIVE_MIME_FIRST; mime < G_PASTE_SENSITIVE_MIME_LAST; ++mime)
    {
        if (g_paste_str_equal (mimetype, g_paste_sensitive_mime_get (mime)))
            return self->sensitive_atoms[mime];
    }

    return None;
}

/* What a selection offers, for g_paste_clipboard_update_read_sensitive_mimes ():
 * the atoms its owner listed in TARGETS, and the timestamp that list was asked
 * with, which the hint reads have to name too. */
typedef struct
{
    GPasteClipboardX11Hints *self;
    Time                     timestamp;
    const Atom              *targets;
    gsize                    n_targets;
} GPasteClipboardX11HintsTargets;

static gboolean
g_paste_clipboard_x11_hints_offers_target (gconstpointer offer,
                                           const gchar  *mimetype)
{
    const GPasteClipboardX11HintsTargets *targets = offer;
    Atom atom = g_paste_clipboard_x11_hints_sensitive_atom (targets->self, mimetype);

    for (gsize i = 0; atom != None && i < targets->n_targets; ++i)
    {
        if (targets->targets[i] == atom)
            return TRUE;
    }

    return FALSE;
}

static void
g_paste_clipboard_x11_hints_on_hint_converted (GPasteClipboardX11Hints *self G_GNUC_UNUSED,
                                               Time                     timestamp G_GNUC_UNUSED,
                                               Atom                     type G_GNUC_UNUSED,
                                               gint                     format,
                                               const guchar            *data,
                                               gulong                   n_items,
                                               gpointer                 user_data)
{
    g_autoptr (GBytes) bytes = (data && format == 8) ? g_bytes_new (data, n_items) : NULL;

    g_paste_clipboard_update_on_mime_read (user_data, bytes);
}

/* A hint read: converted ourselves rather than through GDK, for the reason
 * convert () gives. @backend is the #GPasteClipboardX11HintsTargets the hint
 * was found in. */
static void
g_paste_clipboard_x11_hints_read_hint (gpointer                backend,
                                       const gchar            *mimetype,
                                       GCancellable           *cancellable,
                                       GPasteClipboardMimeCtx *ctx)
{
    const GPasteClipboardX11HintsTargets *targets = backend;

    g_paste_clipboard_x11_hints_convert (targets->self, g_paste_clipboard_x11_hints_sensitive_atom (targets->self, mimetype),
                                         targets->timestamp, cancellable, g_paste_clipboard_x11_hints_on_hint_converted, ctx);
}

/* The owner's TARGETS have come back: read each hint it offers. How this read
 * counts into the update is g_paste_clipboard_update_add_sensitive_offer_read ()'s
 * to say. */
static void
g_paste_clipboard_x11_hints_on_targets_converted (GPasteClipboardX11Hints *self,
                                                  Time                     timestamp,
                                                  Atom                     type,
                                                  gint                     format,
                                                  const guchar            *data,
                                                  gulong                   n_items,
                                                  gpointer                 user_data)
{
    GPasteClipboardUpdate *update = user_data;
    gboolean answered = (data && format == 32 && (type == XA_ATOM || type == self->targets_atom));

    /* A concluded or superseded update has nothing left for a read fired here
     * to report to. */
    if (answered && !g_paste_clipboard_update_is_expired (update))
    {
        /* Format 32 property data is handed back as longs, which is what Atom is. */
        const GPasteClipboardX11HintsTargets offer = {
            .self = self,
            .timestamp = timestamp,
            .targets = (const Atom *) data,
            .n_targets = n_items,
        };

        g_paste_clipboard_update_read_sensitive_mimes (update, &offer, g_paste_clipboard_x11_hints_offers_target,
                                                       (gpointer) &offer, g_paste_clipboard_x11_hints_read_hint);
    }

    g_paste_clipboard_update_on_sensitive_offer_read (update, answered);
}

/**
 * g_paste_clipboard_x11_hints_available:
 * @self: a #GPasteClipboardX11Hints
 *
 * Whether this can convert the selection at all
 *
 * %FALSE where the workaround could not be set up -- no XFixes, an atom that
 * could not be interned, a display that is not X11 -- in which case every
 * conversion answers nothing, which a text read would take for a hint it could
 * not confirm. A caller asks first and reads the hints out of what GDK does
 * offer instead: see the fallback in g_paste_clipboard_gdk_update ().
 *
 * Returns: whether g_paste_clipboard_x11_hints_read () can answer
 */
gboolean
g_paste_clipboard_x11_hints_available (GPasteClipboardX11Hints *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_X11_HINTS (self), FALSE);

    return self->xevent_id != 0;
}

/**
 * g_paste_clipboard_x11_hints_get_owner:
 * @self: a #GPasteClipboardX11Hints
 *
 * What the server says about this selection's owner right now
 *
 * The one question GDK cannot be asked either: a selection whose formats came
 * back empty has been released or has an owner that has yet to publish its
 * targets, and nothing in those formats tells the two apart. X answers it in
 * one round trip, the server tracking the owner itself -- and it is worth the
 * round trip because the answers are not symmetric: treating a release as an
 * owner leaves a selection empty until the next copy, where treating an owner
 * that is merely slow as a release puts the history's head over the copy the
 * user has just made.
 *
 * That asymmetry is also why there are three answers and not two. A display
 * this cannot be asked of -- one that is not X11, or one that refused us the
 * atoms -- answers %G_PASTE_CLIPBOARD_X11_HINTS_OWNER_UNKNOWN, which its caller
 * holds exactly as it holds a slow owner: folding it into "nobody owns this"
 * would hand the destructive half of the question to every display the
 * workaround could not be set up on, and those -- nested, proxying, forwarded
 * servers -- are the same ones whose owners are slowest to publish targets, so
 * the two conditions arrive together rather than independently.
 *
 * Not conditional on g_paste_clipboard_x11_hints_available (): the atoms are
 * interned before the XFixes check new () makes, so a display with no XFixes,
 * which answers %FALSE there, still gets a real answer here.
 *
 * Returns: whether the selection has an owner, or that the server could not say
 */
GPasteClipboardX11HintsOwner
g_paste_clipboard_x11_hints_get_owner (GPasteClipboardX11Hints *self)
{
    g_return_val_if_fail (G_PASTE_IS_CLIPBOARD_X11_HINTS (self), G_PASTE_CLIPBOARD_X11_HINTS_OWNER_UNKNOWN);

    /* The atom is interned for an X11 display and for no other, so it stands in
     * for the backend check new () makes. */
    if (!self->display || !self->xselection)
        return G_PASTE_CLIPBOARD_X11_HINTS_OWNER_UNKNOWN;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *xdisplay = gdk_x11_display_get_xdisplay (self->display);

    /* Trapped like every other request of ours: an error reaches GDK's default
     * handler otherwise, which ends the process. */
    gdk_x11_display_error_trap_push (self->display);
    Window owner = XGetSelectionOwner (xdisplay, self->xselection);
    gboolean failed = gdk_x11_display_error_trap_pop (self->display);
    G_GNUC_END_IGNORE_DEPRECATIONS

    /* A request that errored says nothing about the owner, which is not the same
     * as the server answering None. */
    if (failed)
        return G_PASTE_CLIPBOARD_X11_HINTS_OWNER_UNKNOWN;

    return (owner != None) ? G_PASTE_CLIPBOARD_X11_HINTS_OWNER_SOME : G_PASTE_CLIPBOARD_X11_HINTS_OWNER_NONE;
}

/**
 * g_paste_clipboard_x11_hints_read:
 * @self: a #GPasteClipboardX11Hints
 * @update: the #GPasteClipboardUpdate the reads count into
 *
 * Ask the selection which hints it offers, and read those, all counted into
 * @update. Does nothing for an update that reads no hints.
 */
void
g_paste_clipboard_x11_hints_read (GPasteClipboardX11Hints *self,
                                  GPasteClipboardUpdate   *update)
{
    g_return_if_fail (G_PASTE_IS_CLIPBOARD_X11_HINTS (self));
    g_return_if_fail (update);

    if (!g_paste_clipboard_update_wants_sensitive_mimes (update))
        return;

    g_paste_clipboard_update_add_sensitive_offer_read (update);
    g_paste_clipboard_x11_hints_convert (self, self->targets_atom, self->selection_timestamp, update->guard.cancellable,
                                         g_paste_clipboard_x11_hints_on_targets_converted, update);
}

static void
g_paste_clipboard_x11_hints_dispose (GObject *object)
{
    GPasteClipboardX11Hints *self = G_PASTE_CLIPBOARD_X11_HINTS (object);

    if (self->display)
    {
        /* No conversion can be out -- each holds a ref on us -- but the windows
         * retired from ones given up on are still ours to destroy. */
        g_clear_signal_handler (&self->xevent_id, self->display);

        while (self->retired_windows)
            g_paste_clipboard_x11_hints_end_retirement (self, self->retired_windows->data);

        g_clear_object (&self->display);
    }

    G_OBJECT_CLASS (g_paste_clipboard_x11_hints_parent_class)->dispose (object);
}

static void
g_paste_clipboard_x11_hints_class_init (GPasteClipboardX11HintsClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = g_paste_clipboard_x11_hints_dispose;
}

static void
g_paste_clipboard_x11_hints_init (GPasteClipboardX11Hints *self G_GNUC_UNUSED)
{
}

static Bool
g_paste_clipboard_x11_hints_is_property_notify (Display *xdisplay G_GNUC_UNUSED,
                                                XEvent  *event,
                                                XPointer arg)
{
    return event->type == PropertyNotify && event->xproperty.window == *(Window *) arg;
}

/* The server's clock now, for the conversions made before any owner's
 * acquisition time is known (see @selection_timestamp).
 *
 * Taken the way a client with no event to hand takes one: a zero-length append
 * to a property of a window of its own, whose PropertyNotify carries the time it
 * happened at. Now is inside the current owner's ownership period whoever that
 * is -- it acquired the selection in the past and still holds it -- which is the
 * whole of what an owner comparing a conversion's timestamp asks.
 *
 * Blocks on the reply, beside the XInternAtom ()s that already block in new ():
 * the standalone daemon builds its providers in on_storage_ready () and hands
 * them to g_paste_daemon_new (), whose bootstrap reads are fired in that same
 * call, before control goes back to the main loop -- so an answer that came
 * back later would answer nothing. Everything is flushed and the trap read
 * before waiting, so a request that could not be made answers CurrentTime rather
 * than waiting for an event that is never coming.
 *
 * The wait is bounded, where XIfEvent () would not be: on_storage_ready () runs
 * from a callback inside g_application_run (), so this blocks a running main
 * loop -- D-Bus, the prompts -- and a PropertyNotify that never arrives -- a
 * nested or proxying server that does not deliver PropertyChangeMask on an
 * InputOnly window -- would hang it for good, with nothing said. A deadline
 * reached answers CurrentTime, exactly as a request that could not be made does,
 * and costs the loop that bound once per display at startup.
 *
 * Asked once per display and not once per selection: the GDK backend builds one
 * of these for each of them, one right after the other, and the clock they
 * are asking about is the one server's -- so the second would spend another
 * round trip, and another deadline, on an answer already in hand. The cache
 * lives on the display, which is what the answer is about and what outlives both
 * providers; CurrentTime is cached like any other answer, a server that did not
 * timestamp one property change being in no hurry to timestamp the next. */
static Time
g_paste_clipboard_x11_hints_server_time (GPasteClipboardX11Hints *self,
                                         Display                 *xdisplay)
{
    GQuark quark = g_quark_from_static_string ("gpaste-clipboard-x11-hints-server-time");
    const Time *cached = g_object_get_qdata (G_OBJECT (self->display), quark);

    if (cached)
        return *cached;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gdk_x11_display_error_trap_push (self->display);

    Window window = XCreateWindow (xdisplay, DefaultRootWindow (xdisplay), -100, -100, 1, 1, 0,
                                   CopyFromParent, InputOnly, CopyFromParent, 0, NULL);

    XSelectInput (xdisplay, window, PropertyChangeMask);
    /* An empty string rather than %NULL for the zero elements appended: Xlib
     * copies @data out of the request whatever @nelements says, and a null
     * source is undefined behaviour even for a length of nothing -- which is why
     * GDK's own server_time idiom (gdk_x11_get_server_time ()) hands it a dummy
     * buffer too. */
    XChangeProperty (xdisplay, window, self->property_atom, XA_STRING, 8, PropModeAppend, (const guchar *) "", 0);

    gboolean failed = gdk_x11_display_error_trap_pop (self->display);
    G_GNUC_END_IGNORE_DEPRECATIONS

    Time timestamp = CurrentTime;

    if (!failed)
    {
        XEvent event;
        GPollFD fd = { .fd = ConnectionNumber (xdisplay), .events = G_IO_IN };
        gint64 deadline = g_get_monotonic_time () + G_PASTE_CLIPBOARD_X11_HINTS_SERVER_TIME_TIMEOUT * G_TIME_SPAN_MILLISECOND;
        gboolean answered = FALSE;

        /* XCheckIfEvent () flushes the request out and takes in whatever has
         * arrived, so the waiting happens on the connection between two of its
         * calls rather than inside Xlib, where nothing could bound it. */
        while (!(answered = XCheckIfEvent (xdisplay, &event,
                                           g_paste_clipboard_x11_hints_is_property_notify, (XPointer) &window)))
        {
            gint64 remaining = deadline - g_get_monotonic_time ();

            if (remaining <= 0 || g_poll (&fd, 1, (gint) (remaining / G_TIME_SPAN_MILLISECOND) + 1) < 0)
                break;
        }

        if (answered)
            timestamp = event.xproperty.time;
        else
        {
            /* Debug, not a warning: a server that does not deliver the event is
             * this code running somewhere it was not written for, not a bug of
             * ours -- and a warning here is fatal under G_DEBUG=fatal-warnings,
             * which the tests and many CI setups run with. What it costs is the
             * bootstrap reads' timestamp, which only an owner applying ICCCM's
             * rule to CurrentTime refuses. */
            g_debug ("clipboard: the X server did not timestamp our own property change; conversions will carry no timestamp");
        }
    }

    g_paste_clipboard_x11_hints_destroy_window (self, window);

    g_object_set_qdata_full (G_OBJECT (self->display), quark, g_memdup2 (&timestamp, sizeof (timestamp)), g_free);

    return timestamp;
}

/**
 * g_paste_clipboard_x11_hints_new:
 * @clipboard: the #GdkClipboard whose selection to read
 * @is_clipboard: whether that is CLIPBOARD rather than PRIMARY
 *
 * Returns: (transfer full): a new #GPasteClipboardX11Hints
 */
GPasteClipboardX11Hints *
g_paste_clipboard_x11_hints_new (GdkClipboard *clipboard,
                                 gboolean      is_clipboard)
{
    g_return_val_if_fail (GDK_IS_CLIPBOARD (clipboard), NULL);

    GPasteClipboardX11Hints *self = g_object_new (G_PASTE_TYPE_CLIPBOARD_X11_HINTS, NULL);
    GdkDisplay *display = gdk_clipboard_get_display (clipboard);

    self->display = g_object_ref (display);

    /* X11 is the only backend the GDK provider runs on -- the daemon forces it
     * (gdk_set_allowed_backends () in main ()) -- so the guard is the raw-X calls
     * refusing to assume it: without it, every conversion answers nothing, which
     * a text update reads as a hint it could not confirm. */
    if (GDK_IS_X11_DISPLAY (display))
    {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        Display *xdisplay = gdk_x11_display_get_xdisplay (display);

        /* Trapped: an error here reaches GDK's default handler otherwise, which
         * ends the process, where this is meant to leave the daemon running. */
        gdk_x11_display_error_trap_push (display);

        self->xselection = XInternAtom (xdisplay, (is_clipboard) ? "CLIPBOARD" : "PRIMARY", False);
        self->property_atom = XInternAtom (xdisplay, "GPASTE_SELECTION", False);
        self->targets_atom = XInternAtom (xdisplay, "TARGETS", False);
        self->incr_atom = XInternAtom (xdisplay, "INCR", False);

        /* For the event base, and for whether any of this can run at all: see
         * @selection_timestamp and the branch below. */
        gint xfixes_error_base;
        gboolean has_xfixes = XFixesQueryExtension (xdisplay, &self->xfixes_event_base, &xfixes_error_base);

        /* Here rather than where TARGETS are compared against them: XInternAtom
         * blocks on a round trip, and Xlib caches only replies naming an atom,
         * so asking there for a name nothing has interned yet stalls the main
         * loop on every text copy. Interned outright, so the answer holds
         * however late an owner turns up; GDK interns these itself anyway the
         * first time it serves TARGETS for a password we published. */
        for (GPasteSensitiveMime mime = G_PASTE_SENSITIVE_MIME_FIRST; mime < G_PASTE_SENSITIVE_MIME_LAST; ++mime)
            self->sensitive_atoms[mime] = XInternAtom (xdisplay, g_paste_sensitive_mime_get (mime), False);

        gboolean failed = gdk_x11_display_error_trap_pop (display);
        G_GNUC_END_IGNORE_DEPRECATIONS

        /* Debug and not a warning, as server_time () says below for the same
         * reason: a server that refuses to intern an atom for us is this code
         * running somewhere it was not written for, not a failure of ours -- and
         * a warning is fatal under G_DEBUG=fatal-warnings, which the tests and
         * many CI setups run with, so it would end the daemon over the very
         * failure this branch exists to keep it running through. */
        if (failed)
            g_debug ("clipboard: could not set selection conversions up; the hints GDK leaves out will not be read");
        /* XFixes is not optional here, and not only to the handler that reads its
         * events: without it nothing ever learns when an owner took the
         * selection, so @selection_timestamp stands at the server's clock as of
         * startup for the whole session -- and ICCCM has an owner refuse a
         * conversion timestamped before it acquired anything. Every conversion
         * for a selection taken since startup would come back refused, which a
         * text read holds as a hint it could not confirm, on both selections and
         * for good. That is the silence available () answers %FALSE for: the
         * caller then reads the hints off the formats GDK does publish (see the
         * fallback in g_paste_clipboard_gdk_update ()). get_owner () asks the
         * server itself and keeps answering either way, the atoms it needs being
         * interned above this check.
         *
         * A message of its own and not the one above, nothing having been
         * attempted here; debug for the reason given there. */
        else if (!has_xfixes)
            g_debug ("clipboard: this display has no XFixes; the hints GDK leaves out will be read off the formats instead");
        else
        {
            /* After the atoms: it writes the property they name. */
            self->selection_timestamp = g_paste_clipboard_x11_hints_server_time (self, xdisplay);
            self->xevent_id = g_signal_connect (display,
                                                "xevent",
                                                G_CALLBACK (g_paste_clipboard_x11_hints_on_xevent),
                                                self);
        }
    }

    return self;
}
