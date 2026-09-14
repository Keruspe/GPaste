// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
    /* One private bus, serving as both session and system bus, for the whole binary. */
    G_PASTE_TEST_ENV_DEFAULT = 0,
    /* The test brings up its own GTestDBus per fixture; outside of one, no bus answers. */
    G_PASTE_TEST_ENV_OWN_BUS = 1 << 0,
    /* A private Xvfb, when one can be found. */
    G_PASTE_TEST_ENV_DISPLAY = 1 << 1,
} GPasteTestEnvFlags;

void     g_paste_test_env_setup       (GPasteTestEnvFlags flags);
gboolean g_paste_test_env_has_display (void);
int      g_paste_test_env_run         (void);

G_END_DECLS
