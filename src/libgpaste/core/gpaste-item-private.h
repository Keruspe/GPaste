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

#ifndef __G_PASTE_ITEM_PRIVATE_H__
#define __G_PASTE_ITEM_PRIVATE_H__

#include <gpaste-item.h>

G_BEGIN_DECLS

void g_paste_item_set_display_string (GPasteItem  *self,
                                      const gchar *display_string);

void g_paste_item_set_size    (GPasteItem *self,
                               gsize       size);
void g_paste_item_add_size    (GPasteItem *self,
                               gsize       size);
void g_paste_item_remove_size (GPasteItem *self,
                               gsize       size);

GPasteItem *g_paste_item_new (GType        type,
                              const gchar *value);

G_END_DECLS

#endif /*__G_PASTE_ITEM_PRIVATE_H__*/
