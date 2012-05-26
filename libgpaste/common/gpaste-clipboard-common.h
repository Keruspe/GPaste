/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __G_PASTE_CLIPBOARD_INTERNAL_H__
#define __G_PASTE_CLIPBOARD_INTERNAL_H__

#include "gpaste-clipboard-private.h"

G_BEGIN_DECLS

#define g_paste_clipboard_copy_files_target gdk_atom_intern_static_string ("x-special/gnome-copied-files")


void g_paste_clipboard_get_clipboard_data (GtkClipboard     *clipboard,
                                           GtkSelectionData *selection_data,
                                           guint             info,
                                           gpointer          user_data_or_owner);
void g_paste_clipboard_clear_clipboard_data (GtkClipboard *clipboard,
                                             gpointer      user_data_or_owner);

G_END_DECLS

#endif /*__G_PASTE_CLIPBOARD_INTERNAL_H__*/
