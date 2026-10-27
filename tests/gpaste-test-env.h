// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
    /* One private bus, serving as both session and system bus, for the whole
     * binary. */
    G_PASTE_TEST_ENV_DEFAULT = 0,
    /* The test brings up its own GTestDBus per fixture; outside of one, no bus
     * answers. */
    G_PASTE_TEST_ENV_OWN_BUS = 1 << 0,
    /* A private Xvfb, when one can be found. */
    G_PASTE_TEST_ENV_DISPLAY = 1 << 1,
} GPasteTestEnvFlags;

void     g_paste_test_env_setup       (GPasteTestEnvFlags flags);
gboolean g_paste_test_env_has_display (void);
int      g_paste_test_env_run         (void);

void     g_paste_test_env_keep_result (GObject       *source_object,
                                       GAsyncResult  *result,
                                       gpointer       user_data);
void     g_paste_test_env_wait_result (GAsyncResult **result);

/* Whether a library's own calls reach this binary, which a case counting them
 * through a definition of the binary's own needs, the case skipping itself
 * with @why when they do not. tests/meson.build defines
 * G_PASTE_TEST_LIBRARY_NOT_INTERPOSABLE where library_interposable is false,
 * for the binaries asking, and only an inline function reads the definitions
 * of the binary it is compiled into. */
static inline gboolean
g_paste_test_env_library_interposable (const gchar *why G_GNUC_UNUSED)
{
#ifdef G_PASTE_TEST_LIBRARY_NOT_INTERPOSABLE
    g_test_skip (why);
    return FALSE;
#else
    return TRUE;
#endif
}

G_END_DECLS
