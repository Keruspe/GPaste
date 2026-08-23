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

/* A history is named by the file it is stored in and by the images directory it
 * owns, so a name carrying a path component names something else entirely. Every
 * path built from a name goes through the two builders below, which refuse one
 * this rejects; the bus checks it itself, so a caller gets an error rather than
 * a critical. */
gboolean g_paste_util_history_name_is_valid (const gchar *name);

gchar *g_paste_util_get_history_dir_path  (void);
GFile *g_paste_util_get_history_dir       (void);
gchar *g_paste_util_get_history_file_path (const gchar *name,
                                           const gchar *extension);
GFile *g_paste_util_get_history_file      (const gchar *name,
                                           const gchar *extension);

gboolean g_paste_util_ensure_history_dir_exists (void);

/* Wiping what was decrypted. A read hands back the very thing the passphrase was
 * protecting, and releasing it with g_free() leaves it in the heap for whatever
 * allocates that block next -- and in any core dump taken meanwhile. The re-key
 * path has always wiped what it decrypted; every other read path is what these
 * are for.
 *
 * They live here rather than beside the converter because the read paths holding
 * a cleartext buffer are compiled whether or not this build has encryption; in a
 * build without, nothing was ever encrypted and freeing is all there is to do.
 *
 * #GPasteCleartext is a NUL-terminated one, so a scope with several ways out can
 * declare it g_autoptr and not have to remember. Anything else -- an image's
 * bytes, a whole history document -- carries its length and wipes with
 * g_paste_util_wipe(). */
typedef gchar GPasteCleartext;

void g_paste_util_wipe           (gpointer         data,
                                  gsize            length);
void g_paste_util_free_cleartext (GPasteCleartext *cleartext);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPasteCleartext, g_paste_util_free_cleartext)

/* Every directory listing in this library goes through here. Whether a directory
 * that is not there counts as a failure, and whether an enumeration that stopped
 * half way does, are decided once here rather than at each of the four places
 * that enumerate one -- and getting the second one wrong is how a migration
 * imports half a history and its cleanup deletes the rest. */
GStrv    g_paste_util_list_directory            (GFile               *dir,
                                                 const gchar         *attribute,
                                                 GError             **error);

G_END_DECLS
