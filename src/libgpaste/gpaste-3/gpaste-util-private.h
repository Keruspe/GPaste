// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-item-enums.h>

G_BEGIN_DECLS

/* The uris a files item holds as its row shows them, without the "[Files]"
 * g_paste_util_display_string () puts in front: for the daemon's search,
 * which finds a file list by the paths a user reads. Exported for
 * libgpaste-daemon alone, and in no header a binding scans. */
gchar *g_paste_util_uris_display_string (const gchar *value);

/* @text as a one-line view shows it: folded onto a line, as
 * g_paste_util_one_line () does, and cut to its first @max_chars characters
 * (0 keeping them all), an ellipsis after them. A label lays the whole of its
 * text out to ellipsize it, which on one long line costs tens of milliseconds
 * a binding past a hundred kilobytes: the graphical tool's rows and the
 * overview's results cut at G_PASTE_UTIL_SHOWN_CHARS, more than any window is
 * wide enough to show, and GNOME Shell's menu at element-size. Cut before it is
 * folded, which changes no character's count, so the rest of a long text is
 * never walked. */
gchar *g_paste_util_shown_line (const gchar *text,
                                guint        max_chars);

/* g_paste_util_shown_line () of g_paste_util_display_string (), composed only
 * as far as the line shows: a files item's every path decoded and converted,
 * thousands of them for a directory's worth, to show the first few, would be
 * the most of what drawing its row costs. */
gchar *g_paste_util_display_line (const gchar   *value,
                                  GPasteItemKind kind,
                                  guint          max_chars);

/* g_paste_util_spawn () with the Wayland activation token a caller was handed
 * -- the daemon, by the portal with a shortcut -- rather than one its own
 * environment holds. Exported for libgpaste-daemon alone too. */
void g_paste_util_spawn_with_token (const gchar *app,
                                    const gchar *activation_token);

G_END_DECLS
