/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste-item.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_IMAGE_ITEM (g_paste_image_item_get_type ())

G_PASTE_FINAL_TYPE (ImageItem, image_item, IMAGE_ITEM, GPasteItem)

const gchar     *g_paste_image_item_get_checksum (const GPasteImageItem *self);
const GDateTime *g_paste_image_item_get_date     (const GPasteImageItem *self);
GdkTexture      *g_paste_image_item_get_image    (const GPasteImageItem *self);

GPasteItem      *g_paste_image_item_new           (GdkTexture  *texture);
GPasteItem      *g_paste_image_item_new_from_file (const gchar *path,
                                                   GDateTime   *date);

G_END_DECLS
