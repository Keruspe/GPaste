// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-text-item.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_PASSWORD_ITEM (g_paste_password_item_get_type ())

G_PASTE_FINAL_TYPE (PasswordItem, password_item, PASSWORD_ITEM, GPasteTextItem)

const gchar *g_paste_password_item_get_name (const GPastePasswordItem *self);

void g_paste_password_item_set_name (GPastePasswordItem *self,
                                     const char         *name);

GPasteItem *g_paste_password_item_new (const gchar *name,
                                       const gchar *password);

G_END_DECLS
