/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_UTIL_H__
#define __G_PASTE_UTIL_H__

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

gboolean g_paste_util_confirm_dialog    (GtkWindow   *parent,
                                         const gchar *action,
                                         const gchar *msg);
void     g_paste_util_spawn             (const gchar *app);
gboolean g_paste_util_spawn_sync        (const gchar *app,
                                         GError     **error);
void     g_paste_util_activate_ui       (const gchar *action,
                                         GVariant    *arg);
gboolean g_paste_util_activate_ui_sync  (const gchar *action,
                                         GVariant    *arg,
                                         GError     **error);
gchar   *g_paste_util_replace           (const gchar *text,
                                         const gchar *pattern,
                                         const gchar *substitution);
gchar   *g_paste_util_compute_checksum  (GdkPixbuf *image);

gboolean g_paste_util_has_applet      (void);
gboolean g_paste_util_has_unity       (void);
gboolean g_paste_util_has_gnome_shell (void);

void g_paste_util_show_win (GApplication *application);

guint64 *g_paste_util_get_dbus_at_result (GVariant *variant,
                                          guint64  *len);

guint32 *g_paste_util_get_dbus_au_result (GVariant *variant,
                                          guint64  *len);

void g_paste_util_write_pid_file (void);

G_END_DECLS

#endif /*__G_PASTE_UTIL_H__*/
