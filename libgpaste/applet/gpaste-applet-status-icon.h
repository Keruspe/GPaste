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

#ifndef __G_PASTE_APPLET_STATUS_ICON_H__
#define __G_PASTE_APPLET_STATUS_ICON_H__

#include <gpaste-applet-icon.h>
#include <gpaste-client.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_STATUS_ICON                (g_paste_applet_status_icon_get_type ())
#define G_PASTE_APPLET_STATUS_ICON(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_APPLET_STATUS_ICON, GPasteAppletStatusIcon))
#define G_PASTE_IS_APPLET_STATUS_ICON(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_APPLET_STATUS_ICON))
#define G_PASTE_APPLET_STATUS_ICON_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_APPLET_STATUS_ICON, GPasteAppletStatusIconClass))
#define G_PASTE_IS_APPLET_STATUS_ICON_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_APPLET_STATUS_ICON))
#define G_PASTE_APPLET_STATUS_ICON_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_APPLET_STATUS_ICON, GPasteAppletStatusIconClass))

typedef struct _GPasteAppletStatusIcon GPasteAppletStatusIcon;
typedef struct _GPasteAppletStatusIconClass GPasteAppletStatusIconClass;

G_PASTE_VISIBLE
GType g_paste_applet_status_icon_get_type (void);

GPasteAppletIcon *g_paste_applet_status_icon_new (GPasteClient *client,
                                                  GtkMenu      *menu);

G_END_DECLS

#endif /*__G_PASTE_APPLET_STATUS_ICON_H__*/
