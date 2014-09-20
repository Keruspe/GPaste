/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_DAEMON_H__
#define __G_PASTE_DAEMON_H__

#include <gpaste-clipboards-manager.h>
#include <gpaste-keybinder.h>
#include <gpaste-update-enums.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_DAEMON            (g_paste_daemon_get_type ())
#define G_PASTE_DAEMON(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_DAEMON, GPasteDaemon))
#define G_PASTE_IS_DAEMON(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_DAEMON))
#define G_PASTE_DAEMON_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_DAEMON, GPasteDaemonClass))
#define G_PASTE_IS_DAEMON_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_DAEMON))
#define G_PASTE_DAEMON_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_DAEMON, GPasteDaemonClass))

typedef struct _GPasteDaemon GPasteDaemon;
typedef struct _GPasteDaemonClass GPasteDaemonClass;

G_PASTE_VISIBLE
GType g_paste_daemon_get_type (void);

gboolean g_paste_daemon_own_bus_name (GPasteDaemon *self,
                                      GError      **error);
void     g_paste_daemon_show_history (GPasteDaemon *self,
                                      GError      **error);

GPasteDaemon *g_paste_daemon_new (GPasteHistory           *history,
                                  GPasteSettings          *settings,
                                  GPasteClipboardsManager *clipboards_manager,
                                  GPasteKeybinder         *keybinder);

G_END_DECLS

#endif /*__G_PASTE_DAEMON_H__*/
