// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-special-atom.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_BINARY_DATA (g_paste_binary_data_get_type ())

G_PASTE_FINAL_TYPE (BinaryData, binary_data, BINARY_DATA, GObject)

GPasteSpecialAtom  g_paste_binary_data_get_mime     (const GPasteBinaryData *self);
GBytes            *g_paste_binary_data_get_bytes    (const GPasteBinaryData *self);
gchar             *g_paste_binary_data_to_base64    (const GPasteBinaryData *self);

GPasteBinaryData *g_paste_binary_data_new (GPasteSpecialAtom  mime,
                                           GBytes            *bytes);

G_END_DECLS
