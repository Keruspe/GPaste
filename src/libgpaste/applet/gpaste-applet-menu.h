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

#ifndef __G_PASTE_APPLET_MENU_H__
#define __G_PASTE_APPLET_MENU_H__

#include <gpaste-applet-footer.h>
#include <gpaste-applet-header.h>
#include <gpaste-applet-item.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_MENU            (g_paste_applet_menu_get_type ())
#define G_PASTE_APPLET_MENU(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_APPLET_MENU, GPasteAppletMenu))
#define G_PASTE_IS_APPLET_MENU(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_APPLET_MENU))
#define G_PASTE_APPLET_MENU_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_APPLET_MENU, GPasteAppletMenuClass))
#define G_PASTE_IS_APPLET_MENU_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_APPLET_MENU))
#define G_PASTE_APPLET_MENU_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_APPLET_MENU, GPasteAppletMenuClass))

typedef struct _GPasteAppletMenu GPasteAppletMenu;
typedef struct _GPasteAppletMenuClass GPasteAppletMenuClass;

G_PASTE_VISIBLE
GType g_paste_applet_menu_get_type (void);

void g_paste_applet_menu_append  (GPasteAppletMenu *self,
                                  GSList           *items);
void g_paste_applet_menu_prepend (GPasteAppletMenu *self,
                                  GSList           *items);

gboolean g_paste_applet_menu_get_active (const GPasteAppletMenu *self);
void     g_paste_applet_menu_set_active (GPasteAppletMenu *self,
                                         gboolean          active);

void g_paste_applet_menu_set_text_mode (GPasteAppletMenu *self,
                                        gboolean          value);

GPasteAppletMenu *g_paste_applet_menu_new (GPasteClient *client,
                                           GApplication *app);

G_END_DECLS

#endif /*__G_PASTE_APPLET_MENU_H__*/
