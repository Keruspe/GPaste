/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_CLIPBOARD_H__
#define __G_PASTE_CLIPBOARD_H__

#include <gpaste-history.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARD            (g_paste_clipboard_get_type ())
#define G_PASTE_CLIPBOARD(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_CLIPBOARD, GPasteClipboard))
#define G_PASTE_IS_CLIPBOARD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_CLIPBOARD))
#define G_PASTE_CLIPBOARD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_CLIPBOARD, GPasteClipboardClass))
#define G_PASTE_IS_CLIPBOARD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_CLIPBOARD))
#define G_PASTE_CLIPBOARD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_CLIPBOARD, GPasteClipboardClass))

#define g_paste_clipboard_copy_files_target gdk_atom_intern_static_string ("x-special/gnome-copied-files")

typedef struct _GPasteClipboard GPasteClipboard;
typedef struct _GPasteClipboardClass GPasteClipboardClass;

typedef void (*GPasteClipboardTextCallback) (GPasteClipboard *self,
                                             const gchar     *text,
                                             gpointer         user_data);

typedef void (*GPasteClipboardImageCallback) (GPasteClipboard *self,
                                              GdkPixbuf       *image,
                                              gpointer         user_data);

G_PASTE_VISIBLE
GType g_paste_clipboard_get_type (void);

void          g_paste_clipboard_bootstrap   (GPasteClipboard *self,
                                             GPasteHistory   *history);
GdkAtom       g_paste_clipboard_get_target  (const GPasteClipboard *self);
GtkClipboard *g_paste_clipboard_get_real    (const GPasteClipboard *self);
const gchar  *g_paste_clipboard_get_text    (const GPasteClipboard *self);
void          g_paste_clipboard_set_text    (GPasteClipboard            *self,
                                             GPasteClipboardTextCallback callback,
                                             gpointer                    user_data);
void          g_paste_clipboard_select_text (GPasteClipboard *self,
                                             const gchar     *text);
const gchar  *g_paste_clipboard_get_image_checksum (const GPasteClipboard *self);
void          g_paste_clipboard_set_image          (GPasteClipboard             *self,
                                                    GPasteClipboardImageCallback callback,
                                                    gpointer                     user_data);
void          g_paste_clipboard_select_item        (GPasteClipboard  *self,
                                                    const GPasteItem *item);

GPasteClipboard *g_paste_clipboard_new (GdkAtom         target,
                                        GPasteSettings *settings);

G_END_DECLS

#endif /*__G_PASTE_CLIPBOARD_H__*/
