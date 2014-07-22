/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __GPASTE_GSETTINGS_KEYS_H__
#define __GPASTE_GSETTINGS_KEYS_H__

#include <glib.h>

G_BEGIN_DECLS

#define G_PASTE_SETTINGS_NAME       "org.gnome.GPaste"
#define G_PASTE_SHELL_SETTINGS_NAME "org.gnome.shell"

#define G_PASTE_ELEMENT_SIZE_SETTING               "element-size"
#define G_PASTE_GROWING_LINES_SETTING              "growing-lines"
#define G_PASTE_HISTORY_NAME_SETTING               "history-name"
#define G_PASTE_IMAGES_SUPPORT_SETTING             "images-support"
#define G_PASTE_MAKE_PASSWORD_SETTING              "make-password"
#define G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING "max-displayed-history-size"
#define G_PASTE_MAX_HISTORY_SIZE_SETTING           "max-history-size"
#define G_PASTE_MAX_MEMORY_USAGE_SETTING           "max-memory-usage"
#define G_PASTE_MAX_TEXT_ITEM_SIZE_SETTING         "max-text-item-size"
#define G_PASTE_MIN_TEXT_ITEM_SIZE_SETTING         "min-text-item-size"
#define G_PASTE_POP_SETTING                        "pop"
#define G_PASTE_PRIMARY_TO_HISTORY_SETTING         "primary-to-history"
#define G_PASTE_SAVE_HISTORY_SETTING               "save-history"
#define G_PASTE_SHOW_HISTORY_SETTING               "show-history"
#define G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING  "sync-clipboard-to-primary"
#define G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING  "sync-primary-to-clipboard"
#define G_PASTE_SYNCHRONIZE_CLIPBOARDS_SETTING     "synchronize-clipboards"
#define G_PASTE_TRACK_CHANGES_SETTING              "track-changes"
#define G_PASTE_TRACK_EXTENSION_STATE_SETTING      "track-extension-state"
#define G_PASTE_TRIM_ITEMS_SETTING                 "trim-items"

#define G_PASTE_EXTENSION_ENABLED_SETTING          "extension-enabled"
#define G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING   "enabled-extensions"

G_END_DECLS

#endif /*__GPASTE_GSETTINGS_KEYS_H__*/
