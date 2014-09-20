/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UPDATE_ENUMS_H__
#define __G_PASTE_UPDATE_ENUMS_H__

#include <gpaste-macros.h>

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

#endif /*__G_PASTE_UPDATE_ENUMS_H__*/
