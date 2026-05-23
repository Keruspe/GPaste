// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-bus-object.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_DAEMON (g_paste_daemon_get_type ())

G_PASTE_FINAL_TYPE (Daemon, daemon, DAEMON, GPasteBusObject)

void g_paste_daemon_show_history (GPasteDaemon *self,
                                  GError      **error);
gboolean g_paste_daemon_upload   (GPasteDaemon *self,
                                  const gchar  *uuid);

GPasteDaemon *g_paste_daemon_new (void);

G_END_DECLS
