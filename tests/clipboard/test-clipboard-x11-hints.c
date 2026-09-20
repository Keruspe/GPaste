// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The X11 hints workaround itself (src/daemon/gpaste-clipboard-x11-hints.c),
 * against a real server and a selection owner of our own.
 *
 * What GTK mocks cannot reach: the conversions, the requestor window each reply
 * is attributed by, the property read behind it, the INCR reply that answers
 * nothing, the window a conversion given up on leaves standing -- and the
 * timestamps, which ICCCM has an owner judge a conversion by and which are the
 * whole reason the workaround asks the server for its clock before the first
 * ownership notification.
 *
 * The owner is an X client like any other, on its own connection in this
 * process, driven from the same pump as the main loop: every answer it gives is
 * under test control, and every exchange is a real round trip through the server
 * the daemon would be talking to. */

#include "gpaste-test-clipboard-provider.h"

#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-sensitive-mime.h>

#include "../../src/daemon/gpaste-clipboard-x11-hints.h"

#include <gdk/x11/gdkx.h>
#include <X11/Xatom.h>

#include <string.h>

#define HINT_VALUE "secret"

/* How the owner answers the conversions it is asked for. */
typedef enum
{
    /* TARGETS names the hint, and the hint is served. */
    OWNER_SERVES_HINT,
    /* TARGETS comes back without the hint: a selection saying its text is
     * ordinary. */
    OWNER_NO_HINT,
    /* TARGETS itself is refused: the selection said nothing at all. */
    OWNER_REFUSES_TARGETS,
    /* The hint is offered and then refused. */
    OWNER_REFUSES_HINT,
    /* The hint is answered with an INCR reply, which carries no value. */
    OWNER_HINT_AS_INCR,
    /* Nothing is answered, which is what a hung owner looks like. */
    OWNER_SILENT,
} OwnerBehaviour;

typedef struct
{
    Display       *dpy;
    Window         window;
    Atom           selection;
    Atom           targets_atom;
    Atom           hint_atom;
    Atom           incr_atom;
    Atom           timestamp_prop;
    Time           acquired;
    OwnerBehaviour behaviour;
    /* Refuse a conversion timestamped before this owner acquired the selection,
     * which is the rule the workaround's timestamps exist for. */
    gboolean       strict;
    /* The conversions this owner was asked for, and what the last one naming the
     * hint was timestamped with. Counted on the hint alone: GDK asks the same
     * owner for TARGETS on every ownership change, from a window of its own and
     * with CurrentTime, so nothing keyed on TARGETS here would be about the
     * workaround. Nothing but the workaround ever asks for the hint -- GDK drops
     * that name before it reaches a format. */
    Time           last_hint_time;
    guint          hint_requests;
    /* The requests a silent owner was asked and has not answered, every one of
     * them: GDK converts TARGETS on this same owner for its own formats, so a
     * single slot would be as likely to hold its request as the workaround's. */
    GArray        *deferred;
} Owner;

/* The server's clock, the way a client with no event to hand takes it: a
 * zero-length append to a property of its own, whose PropertyNotify carries the
 * time. The workaround's server_time () does the same; here it is what lets the
 * owner claim the selection at a time it knows, and so judge what it is asked
 * with. */
static Time
owner_time (Owner *owner)
{
    XEvent event;

    XChangeProperty (owner->dpy, owner->window, owner->timestamp_prop, XA_STRING, 8, PropModeAppend,
                     (const guchar *) "", 0);

    do
        XWindowEvent (owner->dpy, owner->window, PropertyChangeMask, &event);
    while (event.type != PropertyNotify);

    return event.xproperty.time;
}

static Owner *
owner_new (gboolean clipboard)
{
    Owner *owner = g_new0 (Owner, 1);

    owner->dpy = XOpenDisplay (NULL);
    g_assert_nonnull (owner->dpy);

    owner->window = XCreateSimpleWindow (owner->dpy, DefaultRootWindow (owner->dpy), -100, -100, 1, 1, 0, 0, 0);
    XSelectInput (owner->dpy, owner->window, PropertyChangeMask);

    owner->selection = XInternAtom (owner->dpy, (clipboard) ? "CLIPBOARD" : "PRIMARY", False);
    owner->targets_atom = XInternAtom (owner->dpy, "TARGETS", False);
    owner->hint_atom = XInternAtom (owner->dpy, g_paste_sensitive_mime_get (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT), False);
    owner->incr_atom = XInternAtom (owner->dpy, "INCR", False);
    owner->timestamp_prop = XInternAtom (owner->dpy, "GPASTE_TEST_OWNER_TIME", False);
    owner->deferred = g_array_new (FALSE, FALSE, sizeof (XSelectionRequestEvent));

    return owner;
}

/* Take the selection, at a timestamp this owner knows. */
static void
owner_claim (Owner *owner)
{
    owner->acquired = owner_time (owner);
    XSetSelectionOwner (owner->dpy, owner->selection, owner->window, owner->acquired);
    XFlush (owner->dpy);
    g_assert_cmpuint (XGetSelectionOwner (owner->dpy, owner->selection), ==, owner->window);
}

/* Synced rather than flushed: the X server orders one client's requests, not two
 * clients' against each other, so a release only sent may not have been processed
 * yet when the workaround asks, on its own connection, who owns the selection
 * (/x11-hints/owner). owner_claim () needs no such care, its own
 * XGetSelectionOwner () being the round trip. */
static void
owner_release (Owner *owner)
{
    XSetSelectionOwner (owner->dpy, owner->selection, None, owner_time (owner));
    XSync (owner->dpy, False);
}

/* Answer one conversion, as an owner does: write the value on the requestor's
 * own window and say where it landed, or say None for a refusal. */
static void
owner_answer (Owner                  *owner,
              XSelectionRequestEvent *request)
{
    XSelectionEvent notify = {
        .type = SelectionNotify,
        .display = owner->dpy,
        .requestor = request->requestor,
        .selection = request->selection,
        .target = request->target,
        .property = None,
        .time = request->time,
    };
    gboolean stale = (owner->strict && (request->time == CurrentTime || request->time < owner->acquired));

    if (!stale && request->target == owner->targets_atom && owner->behaviour != OWNER_REFUSES_TARGETS)
    {
        Atom targets[2];
        gint n_targets = 0;

        targets[n_targets++] = XA_STRING;
        if (owner->behaviour != OWNER_NO_HINT)
            targets[n_targets++] = owner->hint_atom;

        XChangeProperty (owner->dpy, request->requestor, request->property, XA_ATOM, 32, PropModeReplace,
                         (const guchar *) targets, n_targets);
        notify.property = request->property;
    }
    else if (!stale && request->target == owner->hint_atom && owner->behaviour == OWNER_SERVES_HINT)
    {
        XChangeProperty (owner->dpy, request->requestor, request->property, request->target, 8, PropModeReplace,
                         (const guchar *) HINT_VALUE, strlen (HINT_VALUE));
        notify.property = request->property;
    }
    else if (!stale && request->target == owner->hint_atom && owner->behaviour == OWNER_HINT_AS_INCR)
    {
        /* The size the value would come in, which is all an INCR reply says.
         * Deleting the property is what would start the chunks; the workaround
         * does not, so nothing more is owed here. */
        gulong size = strlen (HINT_VALUE);

        XChangeProperty (owner->dpy, request->requestor, request->property, owner->incr_atom, 32, PropModeReplace,
                         (const guchar *) &size, 1);
        notify.property = request->property;
    }

    XSendEvent (owner->dpy, request->requestor, False, NoEventMask, (XEvent *) &notify);
    XFlush (owner->dpy);
}

static void
owner_dispatch (Owner *owner)
{
    while (XPending (owner->dpy))
    {
        XEvent event;

        XNextEvent (owner->dpy, &event);

        if (event.type != SelectionRequest)
            continue;

        if (event.xselectionrequest.target == owner->hint_atom)
        {
            ++owner->hint_requests;
            owner->last_hint_time = event.xselectionrequest.time;
        }

        if (owner->behaviour == OWNER_SILENT)
        {
            /* Kept rather than dropped: what a silent owner leaves is a
             * conversion that may still be answered, which is the whole of what
             * a retired requestor window is for. */
            g_array_append_val (owner->deferred, event.xselectionrequest);
        }
        else
            owner_answer (owner, &event.xselectionrequest);
    }
}

/* Answer what was left unanswered, as an owner that was merely slow would. */
static void
owner_answer_deferred (Owner *owner)
{
    g_assert_cmpuint (owner->deferred->len, >, 0);

    for (guint i = 0; i < owner->deferred->len; ++i)
        owner_answer (owner, &g_array_index (owner->deferred, XSelectionRequestEvent, i));

    g_array_set_size (owner->deferred, 0);
}

static void
owner_free (Owner *owner)
{
    g_array_unref (owner->deferred);
    XDestroyWindow (owner->dpy, owner->window);
    XCloseDisplay (owner->dpy);
    g_free (owner);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Owner, owner_free)

/* Both event loops at once: ours, which is what carries the replies back
 * through GDK, and the owner's. */
static void
pump (Owner *owner,
      guint  milliseconds)
{
    gint64 end = g_get_monotonic_time () + milliseconds * G_TIME_SPAN_MILLISECOND;

    do
    {
        while (g_main_context_iteration (NULL, FALSE));
        owner_dispatch (owner);
        g_usleep (1000);
    } while (g_get_monotonic_time () < end);
}

typedef struct
{
    GPasteItem           *item;
    GPasteClipboardSecret secret;
    gboolean              answered;
} ReadResult;

static void
capture_read (GPasteClipboardProvider *provider G_GNUC_UNUSED,
              GPasteItem              *item,
              gboolean                 superseded G_GNUC_UNUSED,
              GPasteClipboardSecret    secret,
              gpointer                 user_data)
{
    ReadResult *result = user_data;

    result->item = item;
    result->secret = secret;
    result->answered = TRUE;
}

static gboolean have_display;

/* The display gtk_init_check () opened on the private Xvfb, which is the one the
 * owner connects to as well. */
static GdkDisplay *
test_display (void)
{
    GdkDisplay *display = gdk_display_get_default ();

    g_assert_true (GDK_IS_X11_DISPLAY (display));

    return display;
}

static gboolean
skipped_without_display (void)
{
    if (have_display)
        return FALSE;

    g_test_skip ("A private Xvfb display is required");

    return TRUE;
}

static GPasteClipboardX11Hints *
make_hints (void)
{
    return g_paste_clipboard_x11_hints_new (gdk_display_get_clipboard (test_display ()), TRUE);
}

/* One text update, read the way the GDK backend reads one: the text reports, the
 * workaround is asked what the selection offers, and the update concludes on
 * whichever comes last. */
static void
read_text (GPasteClipboardX11Hints *hints,
           Owner                   *owner,
           ReadResult              *result)
{
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestClipboard) clipboard = test_clipboard_new ();

    g_paste_settings_set_min_text_item_size (settings, 1);
    g_paste_settings_set_max_text_item_size (settings, 1000);

    GPasteClipboardUpdate *pending = g_paste_clipboard_update_new (G_PASTE_CLIPBOARD_PROVIDER (clipboard), settings,
                                                                   CLIPBOARD_CONTENT_TEXT, &clipboard->pending,
                                                                   &clipboard->content, capture_read, result);

    g_paste_clipboard_update_add_read (pending);
    g_paste_clipboard_x11_hints_read (hints, pending);
    test_clipboard_text_ready (pending, HINT_VALUE);
    /* What update () ends with: the count an update is created holding, spent
     * once every read it is going to fire has been. */
    g_paste_clipboard_update_maybe_done (pending);

    for (guint i = 0; i < 50 && !result->answered; ++i)
        pump (owner, 20);

    /* Before the provider and the settings this update was created against go:
     * one that never concluded still holds both. */
    g_assert_true (result->answered);
}

/* A display the workaround can convert on: Xvfb has XFixes, the atoms intern,
 * and everything below depends on this being answered. */
static void
available (void)
{
    if (skipped_without_display ())
        return;

    g_autoptr (GPasteClipboardX11Hints) hints = make_hints ();

    g_assert_true (g_paste_clipboard_x11_hints_available (hints));
}

/* The server's own answer, which is what tells a released selection from one
 * whose owner has yet to publish its targets. Asked of the server every time,
 * so it needs no notification to be right. */
static void
owner_query (void)
{
    if (skipped_without_display ())
        return;

    g_autoptr (Owner) owner = owner_new (TRUE);
    g_autoptr (GPasteClipboardX11Hints) hints = make_hints ();

    g_assert_cmpint (g_paste_clipboard_x11_hints_get_owner (hints), ==, G_PASTE_CLIPBOARD_X11_HINTS_OWNER_NONE);

    owner_claim (owner);
    g_assert_cmpint (g_paste_clipboard_x11_hints_get_owner (hints), ==, G_PASTE_CLIPBOARD_X11_HINTS_OWNER_SOME);

    owner_release (owner);
    g_assert_cmpint (g_paste_clipboard_x11_hints_get_owner (hints), ==, G_PASTE_CLIPBOARD_X11_HINTS_OWNER_NONE);
}

/* What the selection says about its text, through a whole conversion: the hint
 * named in TARGETS and served is a password, anything the owner does not answer
 * is an unknown -- never a no -- and TARGETS without the hint is the only no. */
static void
hint_read (gconstpointer user_data)
{
    OwnerBehaviour behaviour = GPOINTER_TO_INT (user_data);

    if (skipped_without_display ())
        return;

    g_autoptr (Owner) owner = owner_new (TRUE);
    ReadResult result = { 0 };

    owner->behaviour = behaviour;

    g_autoptr (GPasteClipboardX11Hints) hints = make_hints ();

    owner_claim (owner);
    pump (owner, 100);

    read_text (hints, owner, &result);

    g_autoptr (GPasteItem) item = result.item;

    g_assert_true (result.answered);
    g_assert_nonnull (item);
    g_assert_cmpstr (g_paste_item_get_real_value (item), ==, HINT_VALUE);

    switch (behaviour)
    {
    case OWNER_SERVES_HINT:
        g_assert_cmpint (result.secret, ==, CLIPBOARD_SECRET_YES);
        g_assert_true (G_PASTE_IS_PASSWORD_ITEM (item));
        break;
    case OWNER_NO_HINT:
        g_assert_cmpint (result.secret, ==, CLIPBOARD_SECRET_NO);
        g_assert_false (G_PASTE_IS_PASSWORD_ITEM (item));
        break;
    default:
        /* A refusal, an INCR reply and a silence are the same answer: the
         * selection was not identified, and nothing may read that as ordinary
         * text. */
        g_assert_cmpint (result.secret, ==, CLIPBOARD_SECRET_UNKNOWN);
        g_assert_false (G_PASTE_IS_PASSWORD_ITEM (item));
        break;
    }
}

/* The timestamp a conversion carries, against an owner applying ICCCM's rule to
 * it -- the one that refuses anything from before it acquired the selection.
 *
 * @user_data picks which timestamp is under test: an owner that claimed before
 * the workaround existed leaves it with no notification to name, which is the
 * daemon's bootstrap read and the whole reason the server's clock is asked for;
 * one that claimed after is named by the notification itself. Both are accepted,
 * where CurrentTime -- what GDK's own reads pass -- would not be. */
static void
timestamped_conversion (gconstpointer user_data)
{
    gboolean bootstrap = GPOINTER_TO_INT (user_data);

    if (skipped_without_display ())
        return;

    g_autoptr (Owner) owner = owner_new (TRUE);
    ReadResult result = { 0 };

    owner->behaviour = OWNER_SERVES_HINT;
    owner->strict = TRUE;

    if (bootstrap)
        owner_claim (owner);

    /* A connection of this test's own, for the clock alone: the server's time is
     * asked once per display and kept there, so the shared one carries an answer
     * from before this owner claimed anything -- which is a stale conversion by
     * the very rule under test here. */
    GdkDisplay *display = gdk_display_open (g_getenv ("DISPLAY"));

    g_assert_true (GDK_IS_X11_DISPLAY (display));

    GPasteClipboardX11Hints *hints = g_paste_clipboard_x11_hints_new (gdk_display_get_clipboard (display), TRUE);

    if (!bootstrap)
    {
        owner_claim (owner);
        pump (owner, 100);
    }

    read_text (hints, owner, &result);

    g_autoptr (GPasteItem) item = result.item;

    g_assert_true (result.answered);
    g_assert_cmpuint (owner->hint_requests, ==, 1);
    g_assert_cmpuint (owner->last_hint_time, !=, CurrentTime);
    g_assert_cmpuint (owner->last_hint_time, >=, owner->acquired);
    g_assert_cmpint (result.secret, ==, CLIPBOARD_SECRET_YES);
    g_assert_true (G_PASTE_IS_PASSWORD_ITEM (item));

    g_clear_object (&hints);
    gdk_display_close (display);
}

/* A conversion given up on leaves its requestor window standing: the owner it
 * was asked of may still answer, and an answer to a window that is gone is a
 * BadWindow the owner dies of, this process being where both of them live.
 *
 * So the late reply lands, is recognised as one nothing is waiting for, and
 * takes the window with it. The update is answered by its cancellation, not by
 * that reply. */
static void
late_reply (void)
{
    if (skipped_without_display ())
        return;

    g_autoptr (Owner) owner = owner_new (TRUE);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestClipboard) clipboard = test_clipboard_new ();
    ReadResult result = { 0 };

    owner->behaviour = OWNER_SILENT;
    g_paste_settings_set_min_text_item_size (settings, 1);
    g_paste_settings_set_max_text_item_size (settings, 1000);

    g_autoptr (GPasteClipboardX11Hints) hints = make_hints ();

    owner_claim (owner);
    pump (owner, 100);

    /* GDK asked this owner for its own TARGETS on the claim above, so what is
     * already deferred is not ours: the conversion under test is the one that
     * comes on top of it. */
    guint before = owner->deferred->len;

    GPasteClipboardUpdate *pending = g_paste_clipboard_update_new (G_PASTE_CLIPBOARD_PROVIDER (clipboard), settings,
                                                                   CLIPBOARD_CONTENT_TEXT, &clipboard->pending,
                                                                   &clipboard->content, capture_read, &result);

    g_paste_clipboard_update_add_read (pending);
    g_paste_clipboard_x11_hints_read (hints, pending);
    test_clipboard_text_ready (pending, HINT_VALUE);
    g_paste_clipboard_update_maybe_done (pending);

    pump (owner, 50);
    g_assert_false (result.answered);
    g_assert_cmpuint (owner->deferred->len, >, before);

    /* What the update's own deadline would do to it, without waiting the
     * deadline out. */
    g_cancellable_cancel (pending->guard.cancellable);
    pump (owner, 50);

    g_autoptr (GPasteItem) item = result.item;

    g_assert_true (result.answered);
    g_assert_cmpint (result.secret, ==, CLIPBOARD_SECRET_UNKNOWN);

    /* And now the owner answers the conversion nobody is waiting for: the window
     * it names has to be standing for that answer to reach anything at all. */
    owner->behaviour = OWNER_SERVES_HINT;
    owner_answer_deferred (owner);
    pump (owner, 100);
}

static gboolean
window_exists (Display *dpy,
               Window   window)
{
    Window root;
    Window parent;
    Window *children = NULL;
    guint n_children = 0;
    gboolean found = FALSE;

    XQueryTree (dpy, DefaultRootWindow (dpy), &root, &parent, &children, &n_children);
    for (guint i = 0; i < n_children && !found; ++i)
        found = (children[i] == window);
    if (children)
        XFree (children);

    return found;
}

/* A requestor window retired from a conversion given up on is still destroyed
 * when the hints object goes with its last reference. dispose () runs after GLib
 * has cleared the object's weak references then, so a teardown reaching the
 * object through the retirement's own weak reference would find nothing and
 * leave the window on the server (see end_retirement ()). */
static void
dispose_retired (void)
{
    if (skipped_without_display ())
        return;

    g_autoptr (Owner) owner = owner_new (TRUE);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestClipboard) clipboard = test_clipboard_new ();
    ReadResult result = { 0 };

    owner->behaviour = OWNER_SILENT;
    g_paste_settings_set_min_text_item_size (settings, 1);
    g_paste_settings_set_max_text_item_size (settings, 1000);

    GPasteClipboardX11Hints *hints = make_hints ();

    owner_claim (owner);
    pump (owner, 100);

    /* As in late_reply (): what GDK deferred on the claim is not ours. */
    guint before = owner->deferred->len;

    GPasteClipboardUpdate *pending = g_paste_clipboard_update_new (G_PASTE_CLIPBOARD_PROVIDER (clipboard), settings,
                                                                   CLIPBOARD_CONTENT_TEXT, &clipboard->pending,
                                                                   &clipboard->content, capture_read, &result);

    g_paste_clipboard_update_add_read (pending);
    g_paste_clipboard_x11_hints_read (hints, pending);
    test_clipboard_text_ready (pending, HINT_VALUE);
    g_paste_clipboard_update_maybe_done (pending);

    pump (owner, 50);
    g_assert_cmpuint (owner->deferred->len, >, before);

    Window requestor = g_array_index (owner->deferred, XSelectionRequestEvent, before).requestor;

    g_cancellable_cancel (pending->guard.cancellable);
    pump (owner, 50);

    g_autoptr (GPasteItem) item = result.item;

    g_assert_true (result.answered);
    g_assert_true (window_exists (owner->dpy, requestor));

    /* The conversion given up on released its reference, so this one is the
     * last. */
    g_object_unref (hints);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    XSync (gdk_x11_display_get_xdisplay (test_display ()), False);
    G_GNUC_END_IGNORE_DEPRECATIONS

    g_assert_false (window_exists (owner->dpy, requestor));
}

int
main (int   argc,
      char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DISPLAY);
    have_display = g_paste_test_env_has_display () && gtk_init_check ();
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/x11-hints/available", available);
    g_test_add_func ("/x11-hints/owner", owner_query);
    g_test_add_data_func ("/x11-hints/hint/served", GINT_TO_POINTER (OWNER_SERVES_HINT), hint_read);
    g_test_add_data_func ("/x11-hints/hint/absent", GINT_TO_POINTER (OWNER_NO_HINT), hint_read);
    g_test_add_data_func ("/x11-hints/hint/targets-refused", GINT_TO_POINTER (OWNER_REFUSES_TARGETS), hint_read);
    g_test_add_data_func ("/x11-hints/hint/refused", GINT_TO_POINTER (OWNER_REFUSES_HINT), hint_read);
    g_test_add_data_func ("/x11-hints/hint/incr", GINT_TO_POINTER (OWNER_HINT_AS_INCR), hint_read);
    g_test_add_data_func ("/x11-hints/timestamp/bootstrap", GINT_TO_POINTER (TRUE), timestamped_conversion);
    g_test_add_data_func ("/x11-hints/timestamp/notified", GINT_TO_POINTER (FALSE), timestamped_conversion);
    g_test_add_func ("/x11-hints/late-reply", late_reply);
    g_test_add_func ("/x11-hints/dispose-retired", dispose_retired);

    return g_paste_test_env_run ();
}
