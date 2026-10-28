// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-password-item.h>

G_BEGIN_DECLS

/* Whether @self has no name of its own, going by %G_PASTE_PASSWORD_ITEM_NO_NAME
 * as every nameless password does: no name a search finds it by, nor one its
 * entry is told apart by -- such passwords match by value instead. */
gboolean g_paste_password_item_is_nameless (GPastePasswordItem *self);

G_END_DECLS
