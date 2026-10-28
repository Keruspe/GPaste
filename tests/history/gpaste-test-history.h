// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-daemon-methods.h>

/* A test that provokes a warning runs its body in a child: the refusals under
 * test warn, and with structured logging (G_LOG_USE_STRUCTURED)
 * g_test_expect_message cannot swallow them, so the child drops warning
 * fatality -- a critical still aborts it -- and the parent matches them on the
 * child's stderr instead. A child run only for directories of its own,
 * expecting no warning, leaves this out, so that one still fails it. */
#define G_PASTE_TEST_IN_SUBPROCESS  g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR)

#define G_PASTE_TEST_TRAP_SUBPROCESS(stderr_glob)                    \
    do {                                                             \
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT); \
        g_test_trap_assert_passed ();                                \
        g_test_trap_assert_stderr (stderr_glob);                     \
    } while (FALSE)

/* How long a storage operation run off the main loop -- a migration, a change
 * of passphrase, an unlock, the doctor's check -- is given before the case
 * waiting on it fails: a bound on one that never settles, not on how long one
 * takes. One reads, writes, syncs and verifies every history and image it goes
 * through, which a loaded runner, valgrind or an emulated machine slows down;
 * a minute is past what any of those take, and well inside each suite's
 * timeout. */
#define G_PASTE_TEST_STORAGE_WAIT_SECONDS 60

void      backup_history (const GPasteDaemonMethods *methods,
                          const gchar               *history,
                          const gchar               *backup,
                          GError                   **error);
void      empty_history  (const GPasteDaemonMethods *methods,
                          const gchar               *name,
                          GError                   **error);
GVariant *list_histories (const GPasteDaemonMethods *methods,
                          GError                   **error);
void      wait_load      (GPasteHistory             *history);

GBytes *read_bytes  (const gchar *path);
void    write_bytes (const gchar *path,
                     GBytes      *bytes);
