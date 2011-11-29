/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_ITEM_H__
#define __G_PASTE_ITEM_H__

#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixdata.h>

/* GPaste Item */

#define G_PASTE_TYPE_ITEM                (g_paste_item_get_type ())
#define G_PASTE_ITEM(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_ITEM, GPasteItem))
#define G_PASTE_IS_ITEM(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_ITEM))
#define G_PASTE_ITEM_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_ITEM, GPasteItemClass))
#define G_PASTE_IS_ITEM_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_ITEM))
#define G_PASTE_ITEM_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_ITEM, GPasteItemClass))

typedef struct _GPasteItem GPasteItem;
typedef struct _GPasteItemClass GPasteItemClass;
typedef struct _GPasteItemPrivate GPasteItemPrivate;

GType g_paste_item_get_type (void);
const gchar *g_paste_item_get_value (GPasteItem *self);
const gchar *g_paste_item_get_display_string (GPasteItem *self);
gboolean g_paste_item_equals (GPasteItem *self, GPasteItem *other);
gboolean g_paste_item_has_value (GPasteItem *self);
const gchar *g_paste_item_get_kind (GPasteItem *self);

/* GPaste TextItem */

#define G_PASTE_TYPE_TEXT_ITEM                (g_paste_text_item_get_type ())
#define G_PASTE_TEXT_ITEM(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_TEXT_ITEM, GPasteTextItem))
#define G_PASTE_IS_TEXT_ITEM(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_TEXT_ITEM))
#define G_PASTE_TEXT_ITEM_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_TEXT_ITEM, GPasteTextItemClass))
#define G_PASTE_IS_TEXT_ITEM_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_TEXT_ITEM))
#define G_PASTE_TEXT_ITEM_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_TEXT_ITEM, GPasteTextItemClass))

typedef struct _GPasteTextItem GPasteTextItem;
typedef struct _GPasteTextItemClass GPasteTextItemClass;

GType g_paste_text_item_get_type (void);
GPasteTextItem *g_paste_text_item_new (const gchar *text);

/* GPaste UrisItem */

#define G_PASTE_TYPE_URIS_ITEM                (g_paste_uris_item_get_type ())
#define G_PASTE_URIS_ITEM(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_URIS_ITEM, GPasteUrisItem))
#define G_PASTE_IS_URIS_ITEM(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_URIS_ITEM))
#define G_PASTE_URIS_ITEM_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_URIS_ITEM, GPasteUrisItemClass))
#define G_PASTE_IS_URIS_ITEM_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_URIS_ITEM))
#define G_PASTE_URIS_ITEM_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_URIS_ITEM, GPasteUrisItemClass))

typedef struct _GPasteUrisItem GPasteUrisItem;
typedef struct _GPasteUrisItemClass GPasteUrisItemClass;
typedef struct _GPasteUrisItemPrivate GPasteUrisItemPrivate;

GType g_paste_uris_item_get_type (void);
GPasteUrisItem *g_paste_uris_item_new (const gchar *uris);
const gchar const **g_paste_uris_item_get_uris (GPasteUrisItem *self);

/* GPaste ImageItem */

#define G_PASTE_TYPE_IMAGE_ITEM               (g_paste_image_item_get_type ())
#define G_PASTE_IMAGE_ITEM(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_IMAGE_ITEM, GPasteImageItem))
#define G_PASTE_IS_IMAGE_ITEM(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_IMAGE_ITEM))
#define G_PASTE_IMAGE_ITEM_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_IMAGE_ITEM, GPasteImageItemClass))
#define G_PASTE_IS_IMAGE_ITEM_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_IMAGE_ITEM))
#define G_PASTE_IMAGE_ITEM_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_IMAGE_ITEM, GPasteImageItemClass))

typedef struct _GPasteImageItem GPasteImageItem;
typedef struct _GPasteImageItemClass GPasteImageItemClass;
typedef struct _GPasteImageItemPrivate GPasteImageItemPrivate;

GType g_paste_image_item_get_type (void);
GPasteImageItem *g_paste_image_item_new (GdkPixbuf *img);
GPasteImageItem *g_paste_image_item_new_from_file (const gchar *path,
                                                   GDateTime   *date);
const gchar *g_paste_image_item_get_checksum (GPasteImageItem *self);
const GDateTime *g_paste_image_item_get_date (GPasteImageItem *self);
const GdkPixbuf *g_paste_image_item_get_image (GPasteImageItem *self);

#endif /*__G_PASTE_ITEM_H__*/
