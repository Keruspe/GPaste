// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-client.h>

G_BEGIN_DECLS

/* Whether @self follows the daemon (g_paste_client_follow_daemon ()): for a
 * wait following it for a while, which leaves it followed when its caller had
 * it so (g_paste_util_wait_for_daemon ()). */
gboolean g_paste_client_is_following (GPasteClient *self);

G_END_DECLS
