// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-test-history.h"

#include <gpaste-test-bus.h>
#include <gpaste-test-env.h>

/* BackupHistory through the daemon's methods, waited for. */
void
backup_history (const GPasteDaemonMethods *methods,
                const gchar               *history,
                const gchar               *backup,
                GError                   **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_daemon_methods_backup_history_async (methods, history, backup, g_paste_test_env_keep_result, &result);
    g_paste_test_env_wait_result (&result);
    g_paste_daemon_methods_backup_history_finish (result, error);
}

/* EmptyHistory, waited for as its D-Bus answer is. */
void
empty_history (const GPasteDaemonMethods *methods,
               const gchar               *name,
               GError                   **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_daemon_methods_empty_history_async (methods, name, g_paste_test_env_keep_result, &result);
    g_paste_test_env_wait_result (&result);
    g_paste_daemon_methods_empty_history_finish (result, error);
}

/* ListHistories through the daemon's methods, waited for. */
GVariant *
list_histories (const GPasteDaemonMethods *methods,
                GError                   **error)
{
    g_autoptr (GAsyncResult) result = NULL;

    g_paste_daemon_methods_list_histories_async (methods, g_paste_test_env_keep_result, &result);
    g_paste_test_env_wait_result (&result);

    return g_paste_daemon_methods_list_histories_finish (result, error);
}

static gboolean
history_loaded (gconstpointer history,
                gconstpointer arg G_GNUC_UNUSED)
{
    return !g_paste_history_is_loading ((GPasteHistory *) history);
}

/* Until the load @history has out is over. */
void
wait_load (GPasteHistory *history)
{
    g_paste_test_bus_wait_until (history_loaded, history, NULL);
    g_assert_false (g_paste_history_is_loading (history));
}

/* Everything the file at @path holds. */
GBytes *
read_bytes (const gchar *path)
{
    gchar *contents = NULL;
    gsize length;

    g_assert_true (g_file_get_contents (path, &contents, &length, NULL));

    return g_bytes_new_take (contents, length);
}

/* The other way from read_bytes (). */
void
write_bytes (const gchar *path,
             GBytes      *bytes)
{
    g_assert_true (g_file_set_contents (path, g_bytes_get_data (bytes, NULL), (gssize) g_bytes_get_size (bytes), NULL));
}
