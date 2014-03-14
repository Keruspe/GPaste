/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_APPLET_DELETE_H__
#define __G_PASTE_APPLET_DELETE_H__

#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_DELETE            (g_paste_applet_delete_get_type ())
#define G_PASTE_APPLET_DELETE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_APPLET_DELETE, GPasteAppletDelete))
#define G_PASTE_IS_APPLET_DELETE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_APPLET_DELETE))
#define G_PASTE_APPLET_DELETE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_APPLET_DELETE, GPasteAppletDeleteClass))
#define G_PASTE_IS_APPLET_DELETE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_APPLET_DELETE))
#define G_PASTE_APPLET_DELETE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_APPLET_DELETE, GPasteAppletDeleteClass))

typedef struct _GPasteAppletDelete GPasteAppletDelete;
typedef struct _GPasteAppletDeleteClass GPasteAppletDeleteClass;

G_PASTE_VISIBLE
GType g_paste_applet_delete_get_type (void);

GtkWidget *g_paste_applet_delete_new (GPasteClient *client,
                                      guint32       index);

G_END_DECLS

#endif /*__G_PASTE_APPLET_DELETE_H__*/
