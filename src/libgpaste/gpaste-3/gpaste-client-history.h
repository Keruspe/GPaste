// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-3/gpaste-macros.h>

G_BEGIN_DECLS

/* How a history travels: name, and how many items it holds. Declared in
 * data/dbus/org.gnome.GPaste3.xml, which is the contract; these are the same
 * thing spelled for the C that builds and reads it, so that the daemon's
 * builder and the client's parser cannot come to disagree. */
#define G_PASTE_HISTORY_VARIANT_STRING   "(st)"
#define G_PASTE_HISTORIES_VARIANT_STRING "a" G_PASTE_HISTORY_VARIANT_STRING

#define G_PASTE_HISTORY_VARIANT_TYPE   G_VARIANT_TYPE (G_PASTE_HISTORY_VARIANT_STRING)
#define G_PASTE_HISTORIES_VARIANT_TYPE G_VARIANT_TYPE (G_PASTE_HISTORIES_VARIANT_STRING)

#define G_PASTE_TYPE_CLIENT_HISTORY (g_paste_client_history_get_type ())

G_PASTE_FINAL_TYPE (ClientHistory, client_history, CLIENT_HISTORY, GObject)

const gchar *g_paste_client_history_get_name (GPasteClientHistory *self);
guint64      g_paste_client_history_get_size (GPasteClientHistory *self);

GPasteClientHistory *g_paste_client_history_new (const gchar *name,
                                                 guint64      size);

G_END_DECLS
