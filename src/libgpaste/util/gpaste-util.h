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

#include <gpaste-macros.h>

#include <gtk/gtk.h>

void     g_paste_util_show_about_dialog (GtkWindow *parent);
gboolean g_paste_util_confirm_dialog    (GtkWindow   *parent,
                                         const gchar *msg);
gboolean g_paste_util_spawn             (const gchar *app,
                                         GError     **error);

#endif /*__G_PASTE_UTIL_H__*/
