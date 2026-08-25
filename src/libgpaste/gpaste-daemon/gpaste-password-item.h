// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-gsettings-keys.h>

#include <gpaste-daemon/gpaste-item.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_PASSWORD_ITEM (g_paste_password_item_get_type ())

/* What a password with no name of its own answers to, and what every password's
 * value reads as. Nameless passwords all read alike, so this is the one name a
 * history may hold several items under. */
#define G_PASTE_PASSWORD_ITEM_NO_NAME "******"

G_PASTE_FINAL_TYPE (PasswordItem, password_item, PASSWORD_ITEM, GPasteItem)

const gchar *g_paste_password_item_get_name    (GPastePasswordItem *self);
guint        g_paste_password_item_get_timeout (GPastePasswordItem *self);

void g_paste_password_item_set_name    (GPastePasswordItem *self,
                                        const char         *name);
void g_paste_password_item_set_timeout (GPastePasswordItem *self,
                                        guint               timeout);

GPasteItem *g_paste_password_item_new (const gchar *name,
                                       const gchar *password,
                                       guint        timeout);

G_END_DECLS
