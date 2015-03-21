/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_CLIPBOARDS_MANAGER_H__
#define __G_PASTE_CLIPBOARDS_MANAGER_H__

#include <gpaste-clipboard.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARDS_MANAGER            (g_paste_clipboards_manager_get_type ())
#define G_PASTE_CLIPBOARDS_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_CLIPBOARDS_MANAGER, GPasteClipboardsManager))
#define G_PASTE_IS_CLIPBOARDS_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_CLIPBOARDS_MANAGER))
#define G_PASTE_CLIPBOARDS_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_CLIPBOARDS_MANAGER, GPasteClipboardsManagerClass))
#define G_PASTE_IS_CLIPBOARDS_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_CLIPBOARDS_MANAGER))
#define G_PASTE_CLIPBOARDS_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_CLIPBOARDS_MANAGER, GPasteClipboardsManagerClass))

typedef struct _GPasteClipboardsManager GPasteClipboardsManager;
typedef struct _GPasteClipboardsManagerClass GPasteClipboardsManagerClass;

G_PASTE_VISIBLE
GType g_paste_clipboards_manager_get_type (void);

void g_paste_clipboards_manager_add_clipboard (GPasteClipboardsManager *self,
                                               GPasteClipboard         *clipboard);
void g_paste_clipboards_manager_sync_from_to  (GPasteClipboardsManager *self,
                                               GdkAtom                  from,
                                               GdkAtom                  to);
void g_paste_clipboards_manager_activate      (GPasteClipboardsManager *self);
void g_paste_clipboards_manager_select        (GPasteClipboardsManager *self,
                                               const GPasteItem        *item);

GPasteClipboardsManager *g_paste_clipboards_manager_new (GPasteHistory  *history,
                                                         GPasteSettings *settings);

G_END_DECLS

#endif /*__G_PASTE_CLIPBOARDS_MANAGER_H__*/
