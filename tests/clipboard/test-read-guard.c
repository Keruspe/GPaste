// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The read guard's deadline on a two-second allowance: silence and not elapsed
 * time, re-armed for what is left of it, and the cancel asked for only once the
 * batch is concluded. On a clock and a timer of the test's own, a deadline
 * firing at the very second it was armed for: what is checked is the
 * arithmetic, exactly, which real time would only bound -- and a loaded
 * builder could break. */

#include <gpaste-test-env.h>

/* The clock the guard reads, in microseconds. */
static gint64 now;

typedef struct
{
    guint           id;
    gint64          due;
    GSourceOnceFunc func;
    gpointer        data;
} Timer;

/* The deadlines out, by an id no source of the main context has. */
static GArray *timers;
static guint   last_timer_id = G_MAXUINT / 2;

static gint64
fake_get_monotonic_time (void)
{
    return now;
}

static guint
fake_timeout_add_seconds_once (guint           seconds,
                               GSourceOnceFunc func,
                               gpointer        data)
{
    Timer timer = { ++last_timer_id, now + seconds * G_USEC_PER_SEC, func, data };

    g_array_append_val (timers, timer);

    return timer.id;
}

/* A deadline of ours, or a source of the main context: the guard's idle. */
static gboolean
fake_source_remove (guint id)
{
    for (guint i = 0; i < timers->len; ++i)
    {
        if (g_array_index (timers, Timer, i).id == id)
        {
            g_array_remove_index (timers, i);
            return TRUE;
        }
    }

    return g_source_remove (id);
}

#define G_PASTE_CLIPBOARD_READ_TIMEOUT 2
#define g_get_monotonic_time fake_get_monotonic_time
#define g_timeout_add_seconds_once fake_timeout_add_seconds_once
#define g_source_remove fake_source_remove
#include <gpaste-daemon/gpaste-clipboard-content.c>
#undef g_source_remove
#undef g_timeout_add_seconds_once
#undef g_get_monotonic_time

/* Internal to the library, so the update code compiled in above needs one of
 * its own. Only a conclusion re-owning a selection reaches it, and no case here
 * makes one. */
GPasteClipboardPublication
g_paste_clipboard_provider_select_item_full (GPasteClipboardProvider *self G_GNUC_UNUSED,
                                             GPasteItem              *item G_GNUC_UNUSED,
                                             gboolean                 independent G_GNUC_UNUSED)
{
    g_assert_not_reached ();
}

typedef struct
{
    GPasteClipboardReadGuard guard;
    gint64                   expired_at;
    gboolean                 cancelled_on_expiry;
} Batch;

static void
on_expired (gpointer user_data)
{
    Batch *batch = user_data;

    batch->expired_at = now;
    batch->cancelled_on_expiry = g_cancellable_is_cancelled (batch->guard.cancellable);
}

/* Move the clock on to @until, every deadline due by then firing at the time it
 * is due, earliest first. */
static void
advance_to (gint64 until)
{
    for (;;)
    {
        guint next = G_MAXUINT;

        for (guint i = 0; i < timers->len; ++i)
        {
            gint64 due = g_array_index (timers, Timer, i).due;

            if (due <= until && (next == G_MAXUINT || due < g_array_index (timers, Timer, next).due))
                next = i;
        }

        if (next == G_MAXUINT)
            break;

        Timer timer = g_array_index (timers, Timer, next);

        g_array_remove_index (timers, next);
        now = timer.due;
        timer.func (timer.data);
    }

    now = until;
}

static void
setup (void)
{
    now = 0;
    g_clear_pointer (&timers, g_array_unref);
    timers = g_array_new (FALSE, FALSE, sizeof (Timer));
}

static void
test_silence (void)
{
    Batch batch = { 0 };

    setup ();
    g_paste_clipboard_read_guard_arm (&batch.guard, on_expired, &batch);
    advance_to (6 * G_USEC_PER_SEC);

    g_assert_cmpint (batch.expired_at, ==, G_PASTE_CLIPBOARD_READ_TIMEOUT * G_USEC_PER_SEC);
    g_assert_false (batch.cancelled_on_expiry);
    g_assert_true (g_cancellable_is_cancelled (batch.guard.cancellable));
    g_paste_clipboard_read_guard_clear (&batch.guard);
}

/* Reads landing every half second keep the batch going past its allowance, and
 * the allowance runs from the last of them. */
static void
test_moving_batch (void)
{
    Batch batch = { 0 };
    gint64 touched = 0;

    setup ();
    g_paste_clipboard_read_guard_arm (&batch.guard, on_expired, &batch);
    while (touched < 2 * G_PASTE_CLIPBOARD_READ_TIMEOUT * G_USEC_PER_SEC)
    {
        touched += G_USEC_PER_SEC / 2;
        advance_to (touched);
        g_assert_cmpint (batch.expired_at, ==, 0);
        g_paste_clipboard_read_guard_touch (&batch.guard);
    }
    advance_to (touched + 6 * G_USEC_PER_SEC);

    g_assert_cmpint (batch.expired_at, ==, touched + G_PASTE_CLIPBOARD_READ_TIMEOUT * G_USEC_PER_SEC);
    g_paste_clipboard_read_guard_clear (&batch.guard);
}

/* A read landing half a second in leaves a second and a half of silence when
 * the deadline fires: rounded down to the second, the batch is re-armed for the
 * one second left, and so goes after three seconds -- not after the four a
 * whole new allowance would give it. */
static void
test_rest_of_allowance (void)
{
    Batch batch = { 0 };

    setup ();
    g_paste_clipboard_read_guard_arm (&batch.guard, on_expired, &batch);
    advance_to (G_USEC_PER_SEC / 2);
    g_paste_clipboard_read_guard_touch (&batch.guard);
    advance_to (6 * G_USEC_PER_SEC);

    g_assert_cmpint (batch.expired_at, ==, 3 * G_USEC_PER_SEC);
    g_paste_clipboard_read_guard_clear (&batch.guard);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    g_test_add_func ("/read-guard/silence", test_silence);
    g_test_add_func ("/read-guard/moving-batch", test_moving_batch);
    g_test_add_func ("/read-guard/rest-of-allowance", test_rest_of_allowance);

    return g_paste_test_env_run ();
}
