/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

G_BEGIN_DECLS

typedef enum {
    G_PASTE_UPDATE_ACTION_REPLACE,
    G_PASTE_UPDATE_ACTION_REMOVE,
    G_PASTE_UPDATE_ACTION_INVALID
} GPasteUpdateAction;

typedef enum {
    G_PASTE_UPDATE_TARGET_ALL,
    G_PASTE_UPDATE_TARGET_FIRST,
    G_PASTE_UPDATE_TARGET_POSITION,
    G_PASTE_UPDATE_TARGET_INVALID
} GPasteUpdateTarget;

static inline GPasteUpdateAction
g_paste_update_action_from_string (const gchar *action)
{
    if (!g_strcmp0 (action, "REPLACE"))
        return G_PASTE_UPDATE_ACTION_REPLACE;
    else if (!g_strcmp0 (action, "REMOVE"))
        return G_PASTE_UPDATE_ACTION_REMOVE;
    else
        return G_PASTE_UPDATE_ACTION_INVALID;
}

static inline GPasteUpdateTarget
g_paste_update_target_from_string (const gchar *target)
{
    if (!g_strcmp0 (target, "ALL"))
        return G_PASTE_UPDATE_TARGET_ALL;
    else if (!g_strcmp0 (target, "FIRST"))
        return G_PASTE_UPDATE_TARGET_FIRST;
    else if (!g_strcmp0 (target, "POSITION"))
        return G_PASTE_UPDATE_TARGET_POSITION;
    else
        return G_PASTE_UPDATE_TARGET_INVALID;
}

static const gchar *
g_paste_update_action_to_string (GPasteUpdateAction action)
{
    switch (action)
    {
    case G_PASTE_UPDATE_ACTION_REPLACE:
        return "REPLACE";
    case G_PASTE_UPDATE_ACTION_REMOVE:
        return "REMOVE";
    default:
        return NULL;
    }
}

static const gchar *
g_paste_update_target_to_string (GPasteUpdateTarget target)
{
    switch (target)
    {
    case G_PASTE_UPDATE_TARGET_ALL:
        return "ALL";
    case G_PASTE_UPDATE_TARGET_FIRST:
        return "FIRST";
    case G_PASTE_UPDATE_TARGET_POSITION:
        return "POSITION";
    default:
        return NULL;
    }
}

G_END_DECLS

#endif /*__G_PASTE_UPDATE_ENUMS_H__*/
