/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#if !defined (__G_PASTE_H_INSIDE__) && !defined (GPASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_KEYBINDER_H__
#define __G_PASTE_KEYBINDER_H__

#include <glib-object.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_KEYBINDER            (g_paste_keybinder_get_type ())
#define G_PASTE_KEYBINDER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_KEYBINDER, GPasteKeybinder))
#define G_PASTE_IS_KEYBINDER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_KEYBINDER))
#define G_PASTE_KEYBINDER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_KEYBINDER, GPasteKeybinderClass))
#define G_PASTE_IS_KEYBINDER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_KEYBINDER))
#define G_PASTE_KEYBINDER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_KEYBINDER, GPasteKeybinderClass))

typedef struct _GPasteKeybinder GPasteKeybinder;
typedef struct _GPasteKeybinderClass GPasteKeybinderClass;
typedef struct _GPasteKeybinderPrivate GPasteKeybinderPrivate;

GType g_paste_keybinder_get_type (void);
void g_paste_keybinder_unbind (GPasteKeybinder *self);
void g_paste_keybinder_rebind (GPasteKeybinder *self,
                               const gchar     *binding);
void g_paste_keybinder_activate (GPasteKeybinder *self);
GPasteKeybinder *g_paste_keybinder_new (const gchar *binding);

G_END_DECLS

#endif /*__G_PASTE_KEYBINDER_H__*/
