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

#define ADD            "Add"
#define BACKUP_HISTORY "BackupHistory"
#define DELETE         "Delete"
#define DELETE_HISTORY "DeleteHistory"
#define EMPTY          "Empty"
#define GET_ELEMENT    "GetElement"
#define GET_HISTORY    "GetHistory"
#define LIST_HISTORIES "ListHistories"
#define REEXECUTE      "Reexecute"
#define SELECT         "Select"
#define SWITCH_HISTORY "SwitchHistory"
#define TRACK          "Track"

#define SIG_CHANGED        "Changed"
#define SIG_NAME_LOST      "NameLost"
#define SIG_REEXECUTE_SELF "ReexecuteSelf"
#define SIG_SHOW_HISTORY   "ShowHistory"
#define SIG_TRACKING       "Tracking"

#endif /*__GDBUS_DEFINES_H__*/
