// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste/gpaste-macros.h>

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum
{
    G_PASTE_SECRET_STREAM_ENCRYPT,
    G_PASTE_SECRET_STREAM_DECRYPT,
} GPasteSecretStreamDirection;

#define G_PASTE_TYPE_SECRET_STREAM_CONVERTER (g_paste_secret_stream_converter_get_type ())

G_PASTE_FINAL_TYPE (SecretStreamConverter, secret_stream_converter, SECRET_STREAM_CONVERTER, GObject)

GConverter *g_paste_secret_stream_converter_new (GPasteSecretStreamDirection  direction,
                                                 const gchar                 *passphrase);

G_END_DECLS
