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

#ifndef __G_PASTE_UI_ABOUT_H__
#define __G_PASTE_UI_ABOUT_H__

#include <gpaste-macros.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_ABOUT            (g_paste_ui_about_get_type ())
#define G_PASTE_UI_ABOUT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_UI_ABOUT, GPasteUiAbout))
#define G_PASTE_IS_UI_ABOUT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_UI_ABOUT))
#define G_PASTE_UI_ABOUT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_UI_ABOUT, GPasteUiAboutClass))
#define G_PASTE_IS_UI_ABOUT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_UI_ABOUT))
#define G_PASTE_UI_ABOUT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_UI_ABOUT, GPasteUiAboutClass))

typedef struct _GPasteUiAbout GPasteUiAbout;
typedef struct _GPasteUiAboutClass GPasteUiAboutClass;

G_PASTE_VISIBLE
GType g_paste_ui_about_get_type (void);

GtkWidget *g_paste_ui_about_new (GtkWindow *topwin);

G_END_DECLS

#endif /*__G_PASTE_UI_ABOUT_H__*/
