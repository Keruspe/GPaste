// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* The uris a files item holds as its row shows them, without the "[Files]"
 * g_paste_util_display_string () puts in front: for the daemon's search,
 * which finds a file list by the paths a user reads. Exported for
 * libgpaste-daemon alone, and in no header a binding scans. */
gchar *g_paste_util_uris_display_string (const gchar *value);

/* g_paste_util_spawn () with the Wayland activation token a caller was handed
 * -- the daemon, by the portal with a shortcut -- rather than one its own
 * environment holds. Exported for libgpaste-daemon alone too. */
void g_paste_util_spawn_with_token (const gchar *app,
                                    const gchar *activation_token);

G_END_DECLS
