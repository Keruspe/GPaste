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

G_END_DECLS
