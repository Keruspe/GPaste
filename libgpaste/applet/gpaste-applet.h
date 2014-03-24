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

#ifndef __G_PASTE_APPLET_H__
#define __G_PASTE_APPLET_H__

#include <gpaste-config.h>
#include <gpaste-applet-menu.h>
#include <gpaste-applet-history.h>
#include <gpaste-applet-status-icon.h>

#if G_PASTE_CONFIG_ENABLE_UNITY
#include <gpaste-applet-app-indicator.h>
#endif

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET            (g_paste_applet_get_type ())
#define G_PASTE_APPLET(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_APPLET, GPasteApplet))
#define G_PASTE_IS_APPLET(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_APPLET))
#define G_PASTE_APPLET_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_APPLET, GPasteAppletClass))
#define G_PASTE_IS_APPLET_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_APPLET))
#define G_PASTE_APPLET_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_APPLET, GPasteAppletClass))

typedef struct _GPasteApplet GPasteApplet;
typedef struct _GPasteAppletClass GPasteAppletClass;

G_PASTE_VISIBLE
GType g_paste_applet_get_type (void);

gboolean g_paste_applet_get_active (const GPasteApplet *self);
void     g_paste_applet_set_active (GPasteApplet *self,
                                    gboolean      active);

void g_paste_applet_set_text_mode (GPasteApplet *self,
                                   gboolean      value);

#if G_PASTE_CONFIG_ENABLE_UNITY
GPasteApplet *g_paste_applet_new_app_indicator (GtkApplication *application);
#endif
GPasteApplet *g_paste_applet_new_status_icon   (GtkApplication *application);

G_END_DECLS

#endif /*__G_PASTE_APPLET_H__*/
