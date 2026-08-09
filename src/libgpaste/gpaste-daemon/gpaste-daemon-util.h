// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-macros.h>

#include <gio/gio.h>

G_BEGIN_DECLS

/* The half of the utility helpers only the daemon library has ever used: where
 * a history lives on disk, and the XML escaping the file backend writes it
 * with. They sit here rather than in libgpaste's gpaste-util.h because nothing
 * outside this library -- not the UI, not the command line, not the extension
 * -- calls them, and this header is not installed, so they stay off the
 * introspection data. */

gchar *g_paste_util_replace (const gchar *text,
                             const gchar *pattern,
                             const gchar *substitution);

gchar *g_paste_util_xml_decode (const gchar *text);
gchar *g_paste_util_xml_encode (const gchar *text);

gchar *g_paste_util_get_history_dir_path  (void);
GFile *g_paste_util_get_history_dir       (void);
gchar *g_paste_util_get_history_file_path (const gchar *name,
                                           const gchar *extension);
gchar *g_paste_util_get_history_name_from_file_path (const gchar *path);
GFile *g_paste_util_get_history_file      (const gchar *name,
                                           const gchar *extension);

gboolean g_paste_util_ensure_history_dir_exists (void);

G_END_DECLS
