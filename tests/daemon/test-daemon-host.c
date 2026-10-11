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

static void
fake_doctor_done (GPasteDaemon        *self    G_GNUC_UNUSED,
                  const gchar * const *report  G_GNUC_UNUSED,
                  gboolean             healthy G_GNUC_UNUSED)
{
}

#define main gpaste_daemon_main
#define g_paste_storage_rekey_async fake_rekey_async
#define g_paste_storage_rekey_finish fake_rekey_finish
#define g_paste_storage_doctor_async fake_doctor_async
#define g_paste_storage_doctor_finish fake_doctor_finish
#define g_paste_daemon_flush fake_daemon_flush
#define g_paste_daemon_reload_storage fake_reload_storage
#define g_paste_daemon_doctor_done fake_doctor_done
#include "gpaste-daemon.c"
#undef g_paste_daemon_doctor_done
#undef g_paste_daemon_reload_storage
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

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/daemon-host/change-during-check", test_change_during_check);

    return g_paste_test_env_run ();
}
