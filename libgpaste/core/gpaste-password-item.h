/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_PASSWORD_ITEM_H__
#define __G_PASTE_PASSWORD_ITEM_H__

#include <gpaste-text-item.h>

G_BEGIN_DECLS

/* GPaste PasswordItem */

#define G_PASTE_TYPE_PASSWORD_ITEM                (g_paste_password_item_get_type ())
#define G_PASTE_PASSWORD_ITEM(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_PASSWORD_ITEM, GPastePasswordItem))
#define G_PASTE_IS_PASSWORD_ITEM(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_PASSWORD_ITEM))
#define G_PASTE_PASSWORD_ITEM_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_PASSWORD_ITEM, GPastePasswordItemClass))
#define G_PASTE_IS_PASSWORD_ITEM_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_PASSWORD_ITEM))
#define G_PASTE_PASSWORD_ITEM_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_PASSWORD_ITEM, GPastePasswordItemClass))

typedef struct _GPastePasswordItem GPastePasswordItem;
typedef struct _GPastePasswordItemClass GPastePasswordItemClass;

G_PASTE_VISIBLE
GType g_paste_password_item_get_type (void);

const gchar *g_paste_password_item_get_name (const GPastePasswordItem *self);

void g_paste_password_item_set_name (GPastePasswordItem *self,
                                     const char         *name);

GPasteItem *g_paste_password_item_new (const gchar *name,
                                       const gchar *password);

G_END_DECLS

#endif /*__G_PASTE_PASSWORD_ITEM_H__*/
