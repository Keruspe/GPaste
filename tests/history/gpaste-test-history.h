// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <glib.h>

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
