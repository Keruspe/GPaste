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

#define G_PASTE_BUS_NAME "org.gnome.GPaste"

#define G_PASTE_DAEMON_OBJECT_PATH    "/org/gnome/GPaste"
#define G_PASTE_DAEMON_INTERFACE_NAME "org.gnome.GPaste2"

#define G_PASTE_DAEMON_ABOUT                      "About"
#define G_PASTE_DAEMON_ADD                        "Add"
#define G_PASTE_DAEMON_ADD_FILE                   "AddFile"
#define G_PASTE_DAEMON_ADD_PASSWORD               "AddPassword"
#define G_PASTE_DAEMON_BACKUP_HISTORY             "BackupHistory"
#define G_PASTE_DAEMON_DELETE                     "Delete"
#define G_PASTE_DAEMON_DELETE_HISTORY             "DeleteHistory"
#define G_PASTE_DAEMON_DELETE_PASSWORD            "DeletePassword"
#define G_PASTE_DAEMON_EMPTY_HISTORY              "EmptyHistory"
#define G_PASTE_DAEMON_GET_ELEMENT                "GetElement"
#define G_PASTE_DAEMON_GET_ELEMENT_AT_INDEX       "GetElementAtIndex"
#define G_PASTE_DAEMON_GET_ELEMENT_KIND           "GetElementKind"
#define G_PASTE_DAEMON_GET_ELEMENTS               "GetElements"
#define G_PASTE_DAEMON_GET_HISTORY                "GetHistory"
#define G_PASTE_DAEMON_GET_HISTORY_NAME           "GetHistoryName"
#define G_PASTE_DAEMON_GET_HISTORY_SIZE           "GetHistorySize"
#define G_PASTE_DAEMON_GET_RAW_ELEMENT            "GetRawElement"
#define G_PASTE_DAEMON_GET_RAW_HISTORY            "GetRawHistory"
#define G_PASTE_DAEMON_LIST_HISTORIES             "ListHistories"
#define G_PASTE_DAEMON_MERGE                      "Merge"
#define G_PASTE_DAEMON_ON_EXTENSION_STATE_CHANGED "OnExtensionStateChanged"
#define G_PASTE_DAEMON_REEXECUTE                  "Reexecute"
#define G_PASTE_DAEMON_RENAME_PASSWORD            "RenamePassword"
#define G_PASTE_DAEMON_REPLACE                    "Replace"
#define G_PASTE_DAEMON_SEARCH                     "Search"
#define G_PASTE_DAEMON_SELECT                     "Select"
#define G_PASTE_DAEMON_SET_PASSWORD               "SetPassword"
#define G_PASTE_DAEMON_SHOW_HISTORY               "ShowHistory"
#define G_PASTE_DAEMON_SWITCH_HISTORY             "SwitchHistory"
#define G_PASTE_DAEMON_TRACK                      "Track"
#define G_PASTE_DAEMON_UPLOAD                     "Upload"

#define G_PASTE_DAEMON_SIG_DELETE_HISTORY "DeleteHistory"
#define G_PASTE_DAEMON_SIG_EMPTY_HISTORY  "EmptyHistory"
#define G_PASTE_DAEMON_SIG_SHOW_HISTORY   "ShowHistory"
#define G_PASTE_DAEMON_SIG_SWITCH_HISTORY "SwitchHistory"
#define G_PASTE_DAEMON_SIG_UPDATE         "Update"

#define G_PASTE_DAEMON_PROP_ACTIVE  "Active"
#define G_PASTE_DAEMON_PROP_VERSION "Version"

#define G_PASTE_DAEMON_INTERFACE                                          \
        "<node>"                                                          \
        " <interface name='" G_PASTE_DAEMON_INTERFACE_NAME "'>"           \
        "  <method name='" G_PASTE_DAEMON_ABOUT "' />"                    \
        "  <method name='" G_PASTE_DAEMON_ADD "'>"                        \
        "   <arg type='s' direction='in' name='text' />"                  \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_ADD_FILE "'>"                   \
        "   <arg type='s' direction='in' name='file' />"                  \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_ADD_PASSWORD "'>"               \
        "   <arg type='s' direction='in' name='name'     />"              \
        "   <arg type='s' direction='in' name='password' />"              \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_BACKUP_HISTORY "'>"             \
        "   <arg type='s' direction='in' name='history' />"               \
        "   <arg type='s' direction='in' name='backup'  />"               \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_DELETE "'>"                     \
        "   <arg type='s' direction='in' name='uuid' />"                  \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_DELETE_HISTORY "'>"             \
        "   <arg type='s' direction='in' name='name' />"                  \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_DELETE_PASSWORD "'>"            \
        "   <arg type='s' direction='in' name='name' />"                  \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_EMPTY_HISTORY "'>"              \
        "   <arg type='s' direction='in' name='name' />"                  \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_GET_ELEMENT "'>"                \
        "   <arg type='s' direction='in' name='uuid'   />"                \
        "   <arg type='s' direction='out' name='value' />"                \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_GET_ELEMENT_AT_INDEX "'>"       \
        "   <arg type='t' direction='in'  name='index' />"                \
        "   <arg type='s' direction='out' name='uuid'  />"                \
        "   <arg type='s' direction='out' name='value' />"                \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_GET_ELEMENT_KIND "'>"           \
        "   <arg type='s' direction='in'  name='uuid' />"                 \
        "   <arg type='s' direction='out' name='kind' />"                 \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_GET_ELEMENTS "'>"               \
        "   <arg type='as' direction='in'  name='uuids' />"               \
        "   <arg type='a(ss)' direction='out' name='elements' />"         \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_GET_HISTORY "'>"                \
        "   <arg type='a(ss)' direction='out' name='history' />"          \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_GET_HISTORY_NAME "'>"           \
        "   <arg type='s' direction='out' name='name' />"                 \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_GET_HISTORY_SIZE "'>"           \
        "   <arg type='s' direction='in' name='name'  />"                 \
        "   <arg type='t' direction='out' name='size' />"                 \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_GET_RAW_ELEMENT "'>"            \
        "   <arg type='s' direction='in' name='uuid'   />"                \
        "   <arg type='s' direction='out' name='value' />"                \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_GET_RAW_HISTORY "'>"            \
        "   <arg type='a(ss)' direction='out' name='history' />"          \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_LIST_HISTORIES "'>"             \
        "   <arg type='as' direction='out' name='histories' />"           \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_MERGE "'>"                      \
        "   <arg type='s'  direction='in' name='decoration' />"           \
        "   <arg type='s'  direction='in' name='separator'  />"           \
        "   <arg type='as' direction='in' name='uuids'      />"           \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_ON_EXTENSION_STATE_CHANGED "'>" \
        "   <arg type='b' direction='in' name='extension-state' />"       \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_REEXECUTE "' />"                \
        "  <method name='" G_PASTE_DAEMON_RENAME_PASSWORD "'>"            \
        "   <arg type='s' direction='in' name='old-name' />"              \
        "   <arg type='s' direction='in' name='new-name' />"              \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_REPLACE "'>"                    \
        "   <arg type='s' direction='in' name='uuid' />"                  \
        "   <arg type='s' direction='in' name='contents' />"              \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_SEARCH "'>"                     \
        "   <arg type='s'  direction='in'  name='query'   />"             \
        "   <arg type='as' direction='out' name='results' />"             \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_SELECT "'>"                     \
        "   <arg type='s' direction='in' name='uuid' />"                  \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_SET_PASSWORD "'>"               \
        "   <arg type='s' direction='in' name='uuid' />"                  \
        "   <arg type='s' direction='in' name='name' />"                  \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_SHOW_HISTORY "' />"             \
        "  <method name='" G_PASTE_DAEMON_SWITCH_HISTORY "'>"             \
        "   <arg type='s' direction='in' name='name' />"                  \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_TRACK "'>"                      \
        "   <arg type='b' direction='in' name='tracking-state' />"        \
        "  </method>"                                                     \
        "  <method name='" G_PASTE_DAEMON_UPLOAD "'>"                     \
        "   <arg type='s' direction='in' name='uuid' />"                  \
        "  </method>"                                                     \
        "  <signal name='" G_PASTE_DAEMON_SIG_DELETE_HISTORY "'>"         \
        "   <arg type='s' direction='out' name='history' />"              \
        "  </signal>"                                                     \
        "  <signal name='" G_PASTE_DAEMON_SIG_EMPTY_HISTORY "'>"          \
        "   <arg type='s' direction='out' name='history' />"              \
        "  </signal>"                                                     \
        "  <signal name='" G_PASTE_DAEMON_SIG_SHOW_HISTORY "' />"         \
        "  <signal name='" G_PASTE_DAEMON_SIG_SWITCH_HISTORY "'>"         \
        "   <arg type='s' direction='out' name='history' />"              \
        "  </signal>"                                                     \
        "  <signal name='" G_PASTE_DAEMON_SIG_UPDATE "'>"                 \
        "   <arg type='s' direction='out' name='action' />"               \
        "   <arg type='s' direction='out' name='target' />"               \
        "   <arg type='t' direction='out' name='index'  />"               \
        "  </signal>"                                                     \
        "  <property name='" G_PASTE_DAEMON_PROP_ACTIVE "'"               \
        "            type='b' access='read' />"                           \
        "  <property name='" G_PASTE_DAEMON_PROP_VERSION "'"              \
        "            type='s' access='read' />"                           \
        " </interface>"                                                   \
        "</node>"

#define G_PASTE_SEARCH_PROVIDER_OBJECT_PATH    "/org/gnome/GPaste/SearchProvider"
#define G_PASTE_SEARCH_PROVIDER_INTERFACE_NAME "org.gnome.Shell.SearchProvider2"

#define G_PASTE_SEARCH_PROVIDER_GET_INITIAL_RESULT_SET   "GetInitialResultSet"
#define G_PASTE_SEARCH_PROVIDER_GET_SUBSEARCH_RESULT_SET "GetSubsearchResultSet"
#define G_PASTE_SEARCH_PROVIDER_GET_RESULT_METAS         "GetResultMetas"
#define G_PASTE_SEARCH_PROVIDER_ACTIVATE_RESULT          "ActivateResult"
#define G_PASTE_SEARCH_PROVIDER_LAUNCH_SEARCH            "LaunchSearch"

#define G_PASTE_SEARCH_PROVIDER_INTERFACE                                        \
        "<node>"                                                                 \
        " <interface name='" G_PASTE_SEARCH_PROVIDER_INTERFACE_NAME "'>"         \
        "  <method name='" G_PASTE_SEARCH_PROVIDER_GET_INITIAL_RESULT_SET "'>"   \
        "   <arg type='as' name='terms' direction='in'    />"                    \
        "   <arg type='as' name='results' direction='out' />"                    \
        "  </method>"                                                            \
        "  <method name='" G_PASTE_SEARCH_PROVIDER_GET_SUBSEARCH_RESULT_SET "'>" \
        "   <arg type='as' name='previous_results' direction='in' />"            \
        "   <arg type='as' name='terms' direction='in'            />"            \
        "   <arg type='as' name='results' direction='out'         />"            \
        "  </method>"                                                            \
        "  <method name='" G_PASTE_SEARCH_PROVIDER_GET_RESULT_METAS "'>"         \
        "   <arg type='as' name='identifiers' direction='in' />"                 \
        "   <arg type='aa{sv}' name='metas' direction='out'  />"                 \
        "  </method>"                                                            \
        "  <method name='" G_PASTE_SEARCH_PROVIDER_ACTIVATE_RESULT "'>"          \
        "   <arg type='s' name='identifier' direction='in' />"                   \
        "   <arg type='as' name='terms' direction='in'     />"                   \
        "   <arg type='u' name='timestamp' direction='in'  />"                   \
        "  </method>"                                                            \
        "  <method name='" G_PASTE_SEARCH_PROVIDER_LAUNCH_SEARCH "'>"            \
        "   <arg type='as' name='terms' direction='in'    />"                    \
        "   <arg type='u' name='timestamp' direction='in' />"                    \
        "  </method>"                                                            \
        " </interface>"                                                          \
        "</node>"

G_END_DECLS
