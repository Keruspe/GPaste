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

static void
fake_daemon_flush (GPasteDaemon *self G_GNUC_UNUSED)
{
}

static void
fake_reload_storage (GPasteDaemon *self G_GNUC_UNUSED)
{
}

static guint resumes;

static void
fake_daemon_resume (GPasteDaemon *self G_GNUC_UNUSED)
{
    ++resumes;
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
#define g_application_quit fake_application_quit
#define g_paste_daemon_reload_storage fake_reload_storage
#define g_paste_daemon_doctor_done fake_doctor_done
#include "gpaste-daemon.c"
#undef g_paste_storage_decryption_needed
#undef g_paste_storage_migration_needed
#undef g_paste_storage_when_idle
#undef g_paste_storage_is_busy
#undef g_paste_daemon_doctor_done
#undef g_paste_daemon_reload_storage
#undef g_application_quit
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

/* A stop while a change of passphrase or a check of the store is at its
 * prompt undoes the handover before the last flush, which records what was
 * copied meanwhile: the reload ending it goes with the process. A takeover
 * under a worker rewriting the store leaves it alone. */
static void
test_stop_at_prompt (void)
{
    /* Never dereferenced: what reaches the daemon is stood in for. */
    GPasteDaemon *daemon = (GPasteDaemon *) &rekeys;
    DaemonContext ctx = { .daemon = daemon };

    resumes = 0;
    quit (&ctx);
    g_assert_cmpuint (resumes, ==, 0);

    ctx.changing_passphrase = TRUE;
    quit (&ctx);
    g_assert_cmpuint (resumes, ==, 1);

    ctx.changing_passphrase = FALSE;
    ctx.checking = TRUE;
    quit (&ctx);
    g_assert_cmpuint (resumes, ==, 2);

    storage_busy = TRUE;
    flush_history (&ctx);
    g_assert_cmpuint (resumes, ==, 2);
    storage_busy = FALSE;
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
    g_test_add_func ("/daemon-host/stop-at-prompt", test_stop_at_prompt);

    return g_paste_test_env_run ();
}
