// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-item.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_IMAGE_ITEM (g_paste_image_item_get_type ())

G_PASTE_FINAL_TYPE (ImageItem, image_item, IMAGE_ITEM, GPasteItem)

const gchar     *g_paste_image_item_get_checksum  (GPasteImageItem *self);
const GDateTime *g_paste_image_item_get_date      (GPasteImageItem *self);
GdkTexture      *g_paste_image_item_get_image     (GPasteImageItem *self);
GBytes          *g_paste_image_item_get_png_bytes (GPasteImageItem *self);

gchar           *g_paste_image_item_get_path_for_history (GPasteImageItem *self,
                                                          const gchar     *history_name);
void             g_paste_image_item_set_history           (GPasteImageItem *self,
                                                           const gchar     *history_name);

gchar           *g_paste_image_item_get_images_dir     (const gchar *history_name);
gchar           *g_paste_image_item_get_encrypted_path (const gchar *path);
void             g_paste_image_item_delete_files       (const gchar *path);

GPasteItem      *g_paste_image_item_new                    (GdkTexture  *texture);
GPasteItem      *g_paste_image_item_new_from_file          (const gchar *path,
                                                            GDateTime   *date,
                                                            const gchar *checksum);
GPasteItem      *g_paste_image_item_new_from_bytes         (const gchar *history_name,
                                                            GBytes      *png,
                                                            GDateTime   *date,
                                                            const gchar *checksum);
GPasteItem      *g_paste_image_item_new_from_bytes_at_path (const gchar *path,
                                                            GBytes      *png,
                                                            GDateTime   *date,
                                                            const gchar *checksum);

/* The checksum an image is identified by, everywhere: dedup, the cache file
 * name, and the clipboard backends deciding whether what they just read is what
 * they already hold. Pure GDK — no widget, no libadwaita — which is why it
 * lives here and not in libgpaste-gtk4: this library must not pull the widget
 * stack into gnome-shell, which cannot initialise it. */
gchar           *g_paste_image_item_compute_checksum       (GdkTexture  *image);

G_END_DECLS
