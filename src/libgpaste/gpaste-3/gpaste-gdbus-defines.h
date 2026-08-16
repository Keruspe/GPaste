// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/*
 * Where GPaste lives on the session bus.
 *
 * The shape of each interface -- its methods, their arguments, its signals and
 * its properties -- is not here: it is in the XML gdbus-codegen builds both
 * sides from (data/dbus/org.gnome.GPaste3.xml, and gnome-shell's search
 * provider interface next to its implementation). What remains are the few
 * names that name a place rather than describe a call.
 */

#define G_PASTE_BUS_NAME "org.gnome.GPaste"

#define G_PASTE_DAEMON_OBJECT_PATH    "/org/gnome/GPaste"
#define G_PASTE_DAEMON_INTERFACE_NAME "org.gnome.GPaste3"

/* GPasteClient turns each of these into its own, richer signal, so it matches
 * on the wire name. */
#define G_PASTE_DAEMON_SIG_DELETE_HISTORY    "DeleteHistory"
#define G_PASTE_DAEMON_SIG_EMPTY_HISTORY     "EmptyHistory"
#define G_PASTE_DAEMON_SIG_HISTORIES_CHANGED "HistoriesChanged"
#define G_PASTE_DAEMON_SIG_SHOW_HISTORY      "ShowHistory"
#define G_PASTE_DAEMON_SIG_UPDATE            "Update"

/* Read from the proxy's property cache, which is keyed by the wire name. */
#define G_PASTE_DAEMON_PROP_ACTIVE  "Active"
#define G_PASTE_DAEMON_PROP_HISTORY "History"
#define G_PASTE_DAEMON_PROP_VERSION "Version"

#define G_PASTE_SEARCH_PROVIDER_OBJECT_PATH "/org/gnome/GPaste/SearchProvider"

G_END_DECLS
