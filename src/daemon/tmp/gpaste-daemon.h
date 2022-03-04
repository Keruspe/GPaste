/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_DAEMON_H__
#define __G_PASTE_DAEMON_H__

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

#endif /*__G_PASTE_DAEMON_H__*/
