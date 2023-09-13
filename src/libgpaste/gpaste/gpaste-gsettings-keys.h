/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define G_PASTE_SETTINGS_NAME       "org.gnome.GPaste"
#define G_PASTE_SETTINGS_PATH       "/org/gnome/GPaste/"
#define G_PASTE_SHELL_SETTINGS_NAME "org.gnome.shell"

#define G_PASTE_CLOSE_ON_SELECT_SETTING            "close-on-select"
#define G_PASTE_ELEMENT_SIZE_SETTING               "element-size"
#define G_PASTE_EMPTY_HISTORY_CONFIRMATION_SETTING "empty-history-confirmation"
#define G_PASTE_GROWING_LINES_SETTING              "growing-lines"
#define G_PASTE_HISTORY_NAME_SETTING               "history-name"
#define G_PASTE_IMAGES_SUPPORT_SETTING             "images-support"
#define G_PASTE_LAUNCH_UI_SETTING                  "launch-ui"
#define G_PASTE_MAKE_PASSWORD_SETTING              "make-password"
#define G_PASTE_MAX_DISPLAYED_HISTORY_SIZE_SETTING "max-displayed-history-size"
#define G_PASTE_MAX_HISTORY_SIZE_SETTING           "max-history-size"
#define G_PASTE_MAX_MEMORY_USAGE_SETTING           "max-memory-usage"
#define G_PASTE_MAX_TEXT_ITEM_SIZE_SETTING         "max-text-item-size"
#define G_PASTE_MIN_TEXT_ITEM_SIZE_SETTING         "min-text-item-size"
#define G_PASTE_OPEN_CENTERED_SETTING              "open-centered"
#define G_PASTE_POP_SETTING                        "pop"
#define G_PASTE_PRIMARY_TO_HISTORY_SETTING         "primary-to-history"
#define G_PASTE_RICH_TEXT_SUPPORT_SETTING          "rich-text-support"
#define G_PASTE_SAVE_HISTORY_SETTING               "save-history"
#define G_PASTE_SHOW_HISTORY_SETTING               "show-history"
#define G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING  "sync-clipboard-to-primary"
#define G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING  "sync-primary-to-clipboard"
#define G_PASTE_SYNCHRONIZE_CLIPBOARDS_SETTING     "synchronize-clipboards"
#define G_PASTE_TRACK_CHANGES_SETTING              "track-changes"
#define G_PASTE_TRACK_EXTENSION_STATE_SETTING      "track-extension-state"
#define G_PASTE_TRIM_ITEMS_SETTING                 "trim-items"
#define G_PASTE_UPLOAD_SETTING                     "upload"

#define G_PASTE_EXTENSION_ENABLED_SETTING          "extension-enabled"
#define G_PASTE_SHELL_ENABLED_EXTENSIONS_SETTING   "enabled-extensions"

G_END_DECLS
