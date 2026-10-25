// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The standalone daemon's host (src/daemon/gpaste-daemon.c), compiled in with
 * the storage concerns it starts stood in for: which of them it runs, and
 * when. */

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-daemon.h>
#include <gpaste-daemon/gpaste-storage-migration.h>

static guint  rekeys;
static guint  checks;
static GTask *held_check;

static void
fake_rekey_async (GPastePrompt       *prompt   G_GNUC_UNUSED,
                  GPasteSettings     *settings G_GNUC_UNUSED,
                  GAsyncReadyCallback callback,
                  gpointer            user_data)
{
    g_autoptr (GTask) task = g_task_new (NULL, NULL, callback, user_data);

    ++rekeys;
    g_task_return_boolean (task, TRUE);
}

static gboolean
fake_rekey_finish (GAsyncResult *result,
                   GError      **error)
{
    return g_task_propagate_boolean (G_TASK (result), error);
}

/* Held until the case answers it, as a check waiting on its prompt is. */
static void
fake_doctor_async (GPastePrompt       *prompt   G_GNUC_UNUSED,
                   GPasteSettings     *settings G_GNUC_UNUSED,
                   GAsyncReadyCallback callback,
                   gpointer            user_data)
{
    ++checks;
    held_check = g_task_new (NULL, NULL, callback, user_data);
}

static gboolean
fake_doctor_finish (GAsyncResult *result,
                    GStrv        *report,
                    gboolean     *healthy,
                    GError      **error)
{
    *report = g_new0 (gchar *, 1);
    *healthy = TRUE;

    return g_task_propagate_boolean (G_TASK (result), error);
}

/* What an exit asks of the daemon, in order: e for an expiry, r for a resume,
 * f for a flush. */
static GString *exit_calls;

/* An answer the history's flush landed, queued from an idle as the saver
 * queues a drained backup's. */
static void
fake_answer (gpointer user_data G_GNUC_UNUSED)
{
    if (exit_calls)
        g_string_append_c (exit_calls, 'a');
}

static void
fake_daemon_flush (GPasteDaemon *self G_GNUC_UNUSED)
{
    if (exit_calls)
    {
        g_string_append_c (exit_calls, 'f');
        g_idle_add_once (fake_answer, NULL);
    }
}

/* The bus a re-exec flushes its replies on, d for that flush, and x for an
 * exec, which fails. */
static gboolean
fake_connection_flush_sync (GDBusConnection *connection G_GNUC_UNUSED,
                            GCancellable    *cancellable G_GNUC_UNUSED,
                            GError         **error G_GNUC_UNUSED)
{
    if (exit_calls)
        g_string_append_c (exit_calls, 'd');
    return TRUE;
}

static GDBusConnection *
fake_get_dbus_connection (GApplication *application G_GNUC_UNUSED)
{
    return (GDBusConnection *) &exit_calls;
}

static int
fake_execl (const char *path G_GNUC_UNUSED,
            const char *arg G_GNUC_UNUSED,
            ...)
{
    if (exit_calls)
        g_string_append_c (exit_calls, 'x');
    errno = ENOENT;
    return -1;
}

static void
fake_daemon_expire_password (GPasteDaemon *self G_GNUC_UNUSED)
{
    if (exit_calls)
        g_string_append_c (exit_calls, 'e');
}

static void
fake_reload_storage (GPasteDaemon *self G_GNUC_UNUSED)
{
}

static void
fake_daemon_resume (GPasteDaemon *self G_GNUC_UNUSED)
{
    if (exit_calls)
        g_string_append_c (exit_calls, 'r');
}

static void
fake_application_quit (GApplication *application G_GNUC_UNUSED)
{
}

static void
fake_doctor_done (GPasteDaemon        *self    G_GNUC_UNUSED,
                  const gchar * const *report  G_GNUC_UNUSED,
                  gboolean             healthy G_GNUC_UNUSED)
{
}

/* A storage worker running for as long as the case says, its end never
 * coming otherwise; and the settles a name acquisition starts, counted. */
static gboolean storage_busy;
static guint    settles;
static gpointer idle_data;
static GDestroyNotify idle_destroy;

static gboolean
fake_storage_is_busy (void)
{
    return storage_busy;
}

static guint
fake_storage_when_idle (GSourceOnceFunc callback G_GNUC_UNUSED,
                        gpointer        user_data,
                        GDestroyNotify  destroy)
{
    idle_data = user_data;
    idle_destroy = destroy;

    return 1;
}

static gboolean
fake_migration_needed (GPasteSettings *settings G_GNUC_UNUSED)
{
    ++settles;

    return FALSE;
}

static gboolean
fake_decryption_needed (GPasteSettings *settings G_GNUC_UNUSED)
{
    return FALSE;
}

#define main gpaste_daemon_main
#define g_paste_storage_is_busy fake_storage_is_busy
#define g_paste_storage_when_idle fake_storage_when_idle
#define g_paste_storage_migration_needed fake_migration_needed
#define g_paste_storage_decryption_needed fake_decryption_needed
#define g_paste_storage_rekey_async fake_rekey_async
#define g_paste_storage_rekey_finish fake_rekey_finish
#define g_paste_storage_doctor_async fake_doctor_async
#define g_paste_storage_doctor_finish fake_doctor_finish
#define g_paste_daemon_flush fake_daemon_flush
#define g_paste_daemon_resume fake_daemon_resume
#define g_paste_daemon_expire_password fake_daemon_expire_password
#define g_application_quit fake_application_quit
#define g_application_get_dbus_connection fake_get_dbus_connection
#define g_dbus_connection_flush_sync fake_connection_flush_sync
#define execl fake_execl
#define g_paste_daemon_reload_storage fake_reload_storage
#define g_paste_daemon_doctor_done fake_doctor_done
#include "gpaste-daemon.c"
#undef g_paste_storage_decryption_needed
#undef g_paste_storage_migration_needed
#undef g_paste_storage_when_idle
#undef g_paste_storage_is_busy
#undef g_paste_daemon_doctor_done
#undef g_paste_daemon_reload_storage
#undef execl
#undef g_dbus_connection_flush_sync
#undef g_application_get_dbus_connection
#undef g_application_quit
#undef g_paste_daemon_expire_password
#undef g_paste_daemon_resume
#undef g_paste_daemon_flush
#undef g_paste_storage_doctor_finish
#undef g_paste_storage_doctor_async
#undef g_paste_storage_rekey_finish
#undef g_paste_storage_rekey_async
#undef main

static gboolean
counted (gconstpointer count,
         gconstpointer arg)
{
    return *(const guint *) count >= GPOINTER_TO_UINT (arg);
}

/* A passphrase change asked for while a check of the store runs -- the check
 * re-keying histories too -- is not dropped, the call having been answered: it
 * runs once the check is done, as the gnome-shell host queues it. */
static void
test_change_during_check (void)
{
    /* Never dereferenced: what reaches the daemon is stood in for. */
    GPasteDaemon *daemon = (GPasteDaemon *) &rekeys;
    DaemonContext ctx = { .daemon = daemon };

    rekeys = checks = 0;
    check_store (daemon, &ctx);
    g_paste_test_bus_wait_until (counted, &checks, GUINT_TO_POINTER (1));
    g_assert_cmpuint (checks, ==, 1);

    change_passphrase (daemon, &ctx);
    g_paste_test_bus_pump (50);
    g_assert_cmpuint (rekeys, ==, 0);

    g_task_return_boolean (held_check, TRUE);
    g_clear_object (&held_check);
    g_paste_test_bus_wait_until (counted, &rekeys, GUINT_TO_POINTER (1));
    g_assert_cmpuint (rekeys, ==, 1);
    g_assert_false (ctx.checking);
}

/* A stop takes the passwords whose countdown runs off before the last flush,
 * as the in-shell host does: retired after it -- at the daemon's dispose -- a
 * password's retirement is recorded nowhere, and the next daemon loads it as
 * the head. While a change of passphrase or a check of the store is at its
 * prompt, the handover is undone before that flush too, which records what was
 * copied meanwhile: the reload ending it goes with the process. A takeover
 * under a worker rewriting the store leaves the handover alone. */
static void
test_stop_order (void)
{
    /* Never dereferenced: what reaches the daemon is stood in for. */
    GPasteDaemon *daemon = (GPasteDaemon *) &rekeys;
    DaemonContext ctx = { .daemon = daemon };
    g_autoptr (GString) calls = g_string_new (NULL);

    exit_calls = calls;
    quit (&ctx);
    g_assert_cmpstr (calls->str, ==, "ef");

    g_string_truncate (calls, 0);
    ctx.changing_passphrase = TRUE;
    quit (&ctx);
    g_assert_cmpstr (calls->str, ==, "erf");

    g_string_truncate (calls, 0);
    ctx.changing_passphrase = FALSE;
    ctx.checking = TRUE;
    quit (&ctx);
    g_assert_cmpstr (calls->str, ==, "erf");

    g_string_truncate (calls, 0);
    storage_busy = TRUE;
    flush_history (&ctx);
    g_assert_cmpstr (calls->str, ==, "ef");
    storage_busy = FALSE;
    exit_calls = NULL;
}

/* A re-exec flushes the history first, then lets what that landed answer --
 * a backup the flush wrote, answered from an idle -- and only then flushes the
 * replies on the bus: flushed before, a backup's reply is queued after the
 * flush and the exec closes the socket under it. Both execs failing here, the
 * daemon resumes. */
static void
test_reexec_order (void)
{
    g_autoptr (GString) calls = g_string_new (NULL);
    GPasteDaemon *daemon = (GPasteDaemon *) &rekeys;
    DaemonContext ctx = { .daemon = daemon };
    GLogLevelFlags fatal = g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR);

    /* What the cases before left due, their flushes' answers among it. */
    while (g_main_context_iteration (NULL, FALSE))
        ;
    exit_calls = calls;
    g_assert_false (reexec (daemon, &ctx));
    exit_calls = NULL;
    g_log_set_always_fatal (fatal);
    g_assert_cmpstr (calls->str, ==, "fadxxr");
}

static gboolean
name_owned (gconstpointer data,
            gconstpointer arg)
{
    GDBusConnection *connection = (GDBusConnection *) data;
    g_autoptr (GVariant) reply = g_dbus_connection_call_sync (connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                              "org.freedesktop.DBus", "NameHasOwner",
                                                              g_variant_new ("(s)", G_PASTE_BUS_NAME), G_VARIANT_TYPE ("(b)"),
                                                              G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL);
    gboolean owned = FALSE;

    g_assert_nonnull (reply);
    g_variant_get (reply, "(b)", &owned);

    return owned == GPOINTER_TO_INT (arg);
}

/* A re-exec asked for while a stop waits on the store does nothing: the
 * process is ending, which an exec would outlive. */
static void
test_reexec_while_stopping (void)
{
    g_autoptr (GString) calls = g_string_new (NULL);
    GPasteDaemon *daemon = (GPasteDaemon *) &rekeys;
    DaemonContext ctx = { .daemon = daemon, .stopping = TRUE };

    exit_calls = calls;
    g_assert_false (reexec (daemon, &ctx));
    exit_calls = NULL;
    g_assert_cmpstr (calls->str, ==, "");
}

/* A stop waiting on a storage worker waits once: a second stop signal meanwhile
 * queues no second quit behind the first. */
static void
test_stop_once (void)
{
    DaemonContext ctx = { 0 };

    storage_busy = TRUE;
    stop_when_storage_idle (&ctx);
    g_assert_true (ctx.stopping);
    g_assert_nonnull (idle_data);

    gpointer first = g_steal_pointer (&idle_data);
    GDestroyNotify destroy = idle_destroy;

    stop_when_storage_idle (&ctx);
    g_assert_null (idle_data);

    storage_busy = FALSE;
    destroy (first);
}

/* A takeover waiting for a storage worker lets go of the name: still queued
 * for it, the process would have the bus hand it back once the successor let
 * go, and build a second daemon on its way out -- whose search provider, the
 * first one's still exported, would fail to export and exit under the worker
 * the stop waits for. A re-acquisition then settles nothing either. */
static void
test_takeover_lets_go (void)
{
    g_autoptr (GPasteBus) bus = g_paste_bus_new ();
    g_autoptr (GDBusConnection) connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
    DaemonContext ctx = { .bus = bus };

    g_paste_bus_own_name_full (bus, FALSE);
    g_paste_test_bus_wait_until (name_owned, connection, GINT_TO_POINTER (TRUE));
    g_assert_true (name_owned (connection, GINT_TO_POINTER (TRUE)));

    storage_busy = TRUE;
    settles = 0;
    on_name_lost (bus, TRUE, &ctx);
    g_assert_true (ctx.stopping);
    g_paste_test_bus_wait_until (name_owned, connection, GINT_TO_POINTER (FALSE));
    g_assert_true (name_owned (connection, GINT_TO_POINTER (FALSE)));

    on_name_acquired (bus, &ctx);
    g_assert_cmpuint (settles, ==, 0);
    g_assert_false (ctx.settling);

    storage_busy = FALSE;
    g_clear_pointer (&idle_data, idle_destroy);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/daemon-host/change-during-check", test_change_during_check);
    g_test_add_func ("/daemon-host/takeover-lets-go", test_takeover_lets_go);
    g_test_add_func ("/daemon-host/stop-order", test_stop_order);
    g_test_add_func ("/daemon-host/reexec-order", test_reexec_order);
    g_test_add_func ("/daemon-host/reexec-while-stopping", test_reexec_while_stopping);
    g_test_add_func ("/daemon-host/stop-once", test_stop_once);

    return g_paste_test_env_run ();
}
