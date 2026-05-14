/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2026, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#pragma once

#include <gpaste-special-atom.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_BINARY_DATA (g_paste_binary_data_get_type ())

G_PASTE_FINAL_TYPE (BinaryData, binary_data, BINARY_DATA, GObject)

GPasteSpecialAtom  g_paste_binary_data_get_mime     (const GPasteBinaryData *self);
const guchar      *g_paste_binary_data_get_data     (const GPasteBinaryData *self);
gsize              g_paste_binary_data_get_length   (const GPasteBinaryData *self);
const gchar       *g_paste_binary_data_get_checksum (const GPasteBinaryData *self);
gchar             *g_paste_binary_data_to_base64    (const GPasteBinaryData *self);

GPasteBinaryData *g_paste_binary_data_new (GPasteSpecialAtom  mime,
                                           const guchar      *data,
                                           gsize              length);

G_END_DECLS
