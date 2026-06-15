// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-bus-object.h>
#include <gpaste-daemon/gpaste-clipboard-provider.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_DAEMON (g_paste_daemon_get_type ())

G_PASTE_FINAL_TYPE (Daemon, daemon, DAEMON, GPasteBusObject)

void g_paste_daemon_show_history (GPasteDaemon *self,
                                  GError      **error);
gboolean g_paste_daemon_upload   (GPasteDaemon *self,
                                  const gchar  *uuid);

GPasteDaemon *g_paste_daemon_new     (GPasteSettings          *settings,
                                      GPasteClipboardProvider *clipboard,
                                      GPasteClipboardProvider *primary);
GPasteDaemon *g_paste_daemon_new_gdk (GPasteSettings          *settings);

#ifdef G_PASTE_ENABLE_GNOME_SHELL
/* @selection is the mutter MetaSelection (global.display.get_selection ()),
 * typed as a plain #GObject so this header needs no libmutter. */
GPasteDaemon *g_paste_daemon_new_meta (GPasteSettings *settings,
                                       GObject        *selection);
#endif

G_END_DECLS
