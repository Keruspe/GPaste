// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-macros.h>
#include <gpaste-3/gpaste-update-enums.h>

G_PASTE_VISIBLE G_DEFINE_ENUM_TYPE (GPasteUpdateAction, g_paste_update_action,
                                    G_DEFINE_ENUM_VALUE (G_PASTE_UPDATE_ACTION_REPLACE, "REPLACE"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_UPDATE_ACTION_REMOVE,  "REMOVE"))

G_PASTE_VISIBLE G_DEFINE_ENUM_TYPE (GPasteUpdateTarget, g_paste_update_target,
                                    G_DEFINE_ENUM_VALUE (G_PASTE_UPDATE_TARGET_ALL,  "ALL"),
                                    G_DEFINE_ENUM_VALUE (G_PASTE_UPDATE_TARGET_ITEM, "ITEM"))
