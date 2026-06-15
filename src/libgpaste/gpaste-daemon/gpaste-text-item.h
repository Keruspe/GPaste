// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-item.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_TEXT_ITEM (g_paste_text_item_get_type ())

G_PASTE_DERIVABLE_TYPE (TextItem, text_item, TEXT_ITEM, GPasteItem)

struct _GPasteTextItemClass
{
    GPasteItemClass parent_class;
};

GPasteItem *g_paste_text_item_new (const gchar *text);

G_END_DECLS
