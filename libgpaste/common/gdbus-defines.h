/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __GDBUS_DEFINES_H__
#define __GDBUS_DEFINES_H__

#define G_PASTE_BUS_NAME       "org.gnome.GPaste"
#define G_PASTE_OBJECT_PATH    "/org/gnome/GPaste"
#define G_PASTE_INTERFACE_NAME "org.gnome.GPaste"

#define ADD                        "Add"
#define ADD_FILE                   "AddFile"
#define BACKUP_HISTORY             "BackupHistory"
#define DELETE                     "Delete"
#define DELETE_HISTORY             "DeleteHistory"
#define EMPTY                      "Empty"
#define GET_ELEMENT                "GetElement"
#define GET_HISTORY                "GetHistory"
#define LIST_HISTORIES             "ListHistories"
#define ON_EXTENSION_STATE_CHANGED "OnExtensionStateChanged"
#define REEXECUTE                  "Reexecute"
#define SELECT                     "Select"
#define SWITCH_HISTORY             "SwitchHistory"
#define TRACK                      "Track"

#define SIG_CHANGED        "Changed"
#define SIG_NAME_LOST      "NameLost"
#define SIG_REEXECUTE_SELF "ReexecuteSelf"
#define SIG_SHOW_HISTORY   "ShowHistory"
#define SIG_TRACKING       "Tracking"

#define PROP_ACTIVE "Active"

#define G_PASTE_IFACE_INFO                                                  \
        "<node>"                                                            \
        "   <interface name='" G_PASTE_INTERFACE_NAME "'>"                  \
        "       <method name='" GET_HISTORY "'>"                            \
        "           <arg type='as' direction='out' />"                      \
        "       </method>"                                                  \
        "       <method name='" BACKUP_HISTORY "'>"                         \
        "           <arg type='s' direction='in' />"                        \
        "       </method>"                                                  \
        "       <method name='" SWITCH_HISTORY "'>"                         \
        "           <arg type='s' direction='in' />"                        \
        "       </method>"                                                  \
        "       <method name='" DELETE_HISTORY "'>"                         \
        "           <arg type='s' direction='in' />"                        \
        "       </method>"                                                  \
        "       <method name='" LIST_HISTORIES "'>"                         \
        "           <arg type='as' direction='out' />"                      \
        "       </method>"                                                  \
        "       <method name='" ADD "'>"                                    \
        "           <arg type='s' direction='in' />"                        \
        "       </method>"                                                  \
        "       <method name='" ADD_FILE "'>"                               \
        "           <arg type='s' direction='in' />"                        \
        "       </method>"                                                  \
        "       <method name='" GET_ELEMENT "'>"                            \
        "           <arg type='u' direction='in' />"                        \
        "           <arg type='s' direction='out' />"                       \
        "       </method>"                                                  \
        "       <method name='" SELECT "'>"                                 \
        "           <arg type='u' direction='in' />"                        \
        "       </method>"                                                  \
        "       <method name='" DELETE "'>"                                 \
        "           <arg type='u' direction='in' />"                        \
        "       </method>"                                                  \
        "       <method name='" EMPTY "' />"                                \
        "       <method name='" TRACK "'>"                                  \
        "           <arg type='b' direction='in' />"                        \
        "       </method>"                                                  \
        "       <method name='" ON_EXTENSION_STATE_CHANGED "'>"             \
        "           <arg type='b' direction='in' />"                        \
        "       </method>"                                                  \
        "       <method name='" REEXECUTE "' />"                            \
        "       <signal name='" SIG_REEXECUTE_SELF "' />"                   \
        "       <signal name='" SIG_TRACKING "'>"                           \
        "           <arg type='b' direction='out' />"                       \
        "       </signal>"                                                  \
        "       <signal name='" SIG_CHANGED "' />"                          \
        "       <signal name='" SIG_NAME_LOST "' />"                        \
        "       <signal name='" SIG_SHOW_HISTORY "' />"                     \
        "       <property name='" PROP_ACTIVE "' type='b' access='read' />" \
        "   </interface>"                                                   \
        "</node>"
#endif /*__GDBUS_DEFINES_H__*/
