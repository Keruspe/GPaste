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

#ifndef __G_PASTE_GDBUS_DEFINES_H__
#define __G_PASTE_GDBUS_DEFINES_H__

#define G_PASTE_GDBUS_BUS_NAME       "org.gnome.GPaste"
#define G_PASTE_GDBUS_OBJECT_PATH    "/org/gnome/GPaste"
#define G_PASTE_GDBUS_INTERFACE_NAME "org.gnome.GPaste"

#define G_PASTE_GDBUS_ADD                        "Add"
#define G_PASTE_GDBUS_ADD_FILE                   "AddFile"
#define G_PASTE_GDBUS_BACKUP_HISTORY             "BackupHistory"
#define G_PASTE_GDBUS_DELETE                     "Delete"
#define G_PASTE_GDBUS_DELETE_HISTORY             "DeleteHistory"
#define G_PASTE_GDBUS_EMPTY                      "Empty"
#define G_PASTE_GDBUS_GET_ELEMENT                "GetElement"
#define G_PASTE_GDBUS_GET_HISTORY                "GetHistory"
#define G_PASTE_GDBUS_LIST_HISTORIES             "ListHistories"
#define G_PASTE_GDBUS_ON_EXTENSION_STATE_CHANGED "OnExtensionStateChanged"
#define G_PASTE_GDBUS_REEXECUTE                  "Reexecute"
#define G_PASTE_GDBUS_SELECT                     "Select"
#define G_PASTE_GDBUS_SWITCH_HISTORY             "SwitchHistory"
#define G_PASTE_GDBUS_TRACK                      "Track"

#define G_PASTE_GDBUS_SIG_CHANGED        "Changed"
#define G_PASTE_GDBUS_SIG_NAME_LOST      "NameLost"
#define G_PASTE_GDBUS_SIG_REEXECUTE_SELF "ReexecuteSelf"
#define G_PASTE_GDBUS_SIG_SHOW_HISTORY   "ShowHistory"
#define G_PASTE_GDBUS_SIG_TRACKING       "Tracking"

#define G_PASTE_GDBUS_PROP_ACTIVE "Active"

#define G_PASTE_GDBUS_INTERFACE_INFO                                          \
        "<node>"                                                              \
        "   <interface name='" G_PASTE_GDBUS_INTERFACE_NAME "'>"              \
        "       <method name='" G_PASTE_GDBUS_GET_HISTORY "'>"                \
        "           <arg type='as' direction='out' />"                        \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_BACKUP_HISTORY "'>"             \
        "           <arg type='s' direction='in' />"                          \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_SWITCH_HISTORY "'>"             \
        "           <arg type='s' direction='in' />"                          \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_DELETE_HISTORY "'>"             \
        "           <arg type='s' direction='in' />"                          \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_LIST_HISTORIES "'>"             \
        "           <arg type='as' direction='out' />"                        \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_ADD "'>"                        \
        "           <arg type='s' direction='in' />"                          \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_ADD_FILE "'>"                   \
        "           <arg type='s' direction='in' />"                          \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_GET_ELEMENT "'>"                \
        "           <arg type='u' direction='in' />"                          \
        "           <arg type='s' direction='out' />"                         \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_SELECT "'>"                     \
        "           <arg type='u' direction='in' />"                          \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_DELETE "'>"                     \
        "           <arg type='u' direction='in' />"                          \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_EMPTY "' />"                    \
        "       <method name='" G_PASTE_GDBUS_TRACK "'>"                      \
        "           <arg type='b' direction='in' />"                          \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_ON_EXTENSION_STATE_CHANGED "'>" \
        "           <arg type='b' direction='in' />"                          \
        "       </method>"                                                    \
        "       <method name='" G_PASTE_GDBUS_REEXECUTE "' />"                \
        "       <signal name='" G_PASTE_GDBUS_SIG_REEXECUTE_SELF "' />"       \
        "       <signal name='" G_PASTE_GDBUS_SIG_TRACKING "'>"               \
        "           <arg type='b' direction='out' />"                         \
        "       </signal>"                                                    \
        "       <signal name='" G_PASTE_GDBUS_SIG_CHANGED "' />"              \
        "       <signal name='" G_PASTE_GDBUS_SIG_NAME_LOST "' />"            \
        "       <signal name='" G_PASTE_GDBUS_SIG_SHOW_HISTORY "' />"         \
        "       <property name='" G_PASTE_GDBUS_PROP_ACTIVE "'"               \
        "                 type='b' access='read' />"                          \
        "   </interface>"                                                     \
        "</node>"

#endif /*__G_PASTE_GDBUS_DEFINES_H__*/
