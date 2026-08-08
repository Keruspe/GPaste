// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-macros.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_TEXT_CONTENT_PROVIDER (g_paste_text_content_provider_get_type ())

G_PASTE_FINAL_TYPE (TextContentProvider, text_content_provider, TEXT_CONTENT_PROVIDER, GdkContentProvider)

GdkContentProvider *g_paste_text_content_provider_new (const gchar *text);

G_END_DECLS
