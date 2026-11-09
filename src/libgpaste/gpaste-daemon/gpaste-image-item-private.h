// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-image-item.h>

G_BEGIN_DECLS

/* Have @self, in no history yet, stand for the image @checksum names in place
 * of the one its pixels gave: the file backend's, for an image whose pixels do
 * not come back to what it was stored under, rather than decode it a second
 * time to build it under @checksum (_g_paste_file_backend_named_image ()). */
void g_paste_image_item_set_stored_checksum (GPasteImageItem *self,
                                             const gchar     *checksum);

G_END_DECLS
