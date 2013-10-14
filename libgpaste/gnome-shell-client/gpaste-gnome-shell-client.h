/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_GNOME_SHELL_CLIENT_H__
#define __G_PASTE_GNOME_SHELL_CLIENT_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_GNOME_SHELL_CLIENT            (g_paste_gnome_shell_client_get_type ())
#define G_PASTE_GNOME_SHELL_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_GNOME_SHELL_CLIENT, GPasteGnomeShellClient))
#define G_PASTE_IS_GNOME_SHELL_CLIENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_GNOME_SHELL_CLIENT))
#define G_PASTE_GNOME_SHELL_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_GNOME_SHELL_CLIENT, GPasteGnomeShellClientClass))
#define G_PASTE_IS_GNOME_SHELL_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_GNOME_SHELL_CLIENT))
#define G_PASTE_GNOME_SHELL_CLIENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_GNOME_SHELL_CLIENT, GPasteGnomeShellClientClass))

typedef struct _GPasteGnomeShellClient GPasteGnomeShellClient;
typedef struct _GPasteGnomeShellClientClass GPasteGnomeShellClientClass;

G_PASTE_VISIBLE
GType g_paste_gnome_shell_client_get_type (void);

const gchar *g_paste_gnome_shell_client_get_mode           (GPasteGnomeShellClient *self);
gboolean     g_paste_gnome_shell_client_overview_is_active (GPasteGnomeShellClient *self);
const gchar *g_paste_gnome_shell_client_get_shell_version  (GPasteGnomeShellClient *self);

gboolean g_paste_gnome_shell_client_overview_set_active (GPasteGnomeShellClient *self,
                                                         gboolean                value,
                                                         GError                **error);

GPasteGnomeShellClient *g_paste_gnome_shell_client_new (GError **error);

G_END_DECLS

#endif /*__G_PASTE_GNOME_SHELL_CLIENT_H__*/
