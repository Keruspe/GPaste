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

#ifndef __G_PASTE_APPLET_FOOTER_H__
#define __G_PASTE_APPLET_FOOTER_H__

#include <gpaste-applet-empty.h>
#include <gpaste-applet-settings.h>
#include <gpaste-applet-about.h>
#include <gpaste-applet-quit.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_APPLET_FOOTER            (g_paste_applet_footer_get_type ())
#define G_PASTE_APPLET_FOOTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_APPLET_FOOTER, GPasteAppletFooter))
#define G_PASTE_IS_APPLET_FOOTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_APPLET_FOOTER))
#define G_PASTE_APPLET_FOOTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_APPLET_FOOTER, GPasteAppletFooterClass))
#define G_PASTE_IS_APPLET_FOOTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_APPLET_FOOTER))
#define G_PASTE_APPLET_FOOTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_APPLET_FOOTER, GPasteAppletFooterClass))

typedef struct _GPasteAppletFooter GPasteAppletFooter;
typedef struct _GPasteAppletFooterClass GPasteAppletFooterClass;

G_PASTE_VISIBLE
GType g_paste_applet_footer_get_type (void);

void g_paste_applet_footer_add_to_menu      (GPasteAppletFooter *self,
                                             GtkMenu            *menu,
                                             gboolean            empty);
void g_paste_applet_footer_remove_from_menu (GPasteAppletFooter *self,
                                             GtkMenu            *menu);

GPasteAppletFooter *g_paste_applet_footer_new (GPasteClient *client,
                                               GApplication *app);

G_END_DECLS

#endif /*__G_PASTE_APPLET_FOOTER_H__*/
