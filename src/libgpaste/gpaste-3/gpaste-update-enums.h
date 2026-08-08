// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum {
    G_PASTE_UPDATE_ACTION_REPLACE = 1,
    G_PASTE_UPDATE_ACTION_REMOVE,
    G_PASTE_UPDATE_ACTION_INVALID = 0
} GPasteUpdateAction;

#define G_PASTE_TYPE_UPDATE_ACTION (g_paste_update_action_get_type ())
GType g_paste_update_action_get_type (void);

typedef enum {
    G_PASTE_UPDATE_TARGET_ALL = 1,
    G_PASTE_UPDATE_TARGET_POSITION,
    G_PASTE_UPDATE_TARGET_INVALID = 0
} GPasteUpdateTarget;

#define G_PASTE_TYPE_UPDATE_TARGET (g_paste_update_target_get_type ())
GType g_paste_update_target_get_type (void);

G_END_DECLS
