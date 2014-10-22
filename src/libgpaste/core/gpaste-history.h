/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_HISTORY_H__
#define __G_PASTE_HISTORY_H__

#include <gpaste-password-item.h>
#include <gpaste-settings.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_HISTORY            (g_paste_history_get_type ())
#define G_PASTE_HISTORY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_HISTORY, GPasteHistory))
#define G_PASTE_IS_HISTORY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_HISTORY))
#define G_PASTE_HISTORY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_HISTORY, GPasteHistoryClass))
#define G_PASTE_IS_HISTORY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_HISTORY))
#define G_PASTE_HISTORY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_HISTORY, GPasteHistoryClass))

typedef struct _GPasteHistory GPasteHistory;
typedef struct _GPasteHistoryClass GPasteHistoryClass;

G_PASTE_VISIBLE
GType g_paste_history_get_type (void);

void              g_paste_history_add                (GPasteHistory *self,
                                                      GPasteItem    *item);
void              g_paste_history_remove             (GPasteHistory *self,
                                                      guint32        index);
const GPasteItem *g_paste_history_get                (GPasteHistory *self,
                                                      guint32        index);
GPasteItem       *g_paste_history_dup                (GPasteHistory *self,
                                                      guint32        index);
const gchar      *g_paste_history_get_display_string (GPasteHistory *self,
                                                      guint32        index);
const gchar      *g_paste_history_get_value          (GPasteHistory *self,
                                                      guint32        index);
void              g_paste_history_select             (GPasteHistory *self,
                                                      guint32        index);
void                      g_paste_history_set_password    (GPasteHistory *self,
                                                           guint32        index,
                                                           const gchar   *name);
const GPastePasswordItem *g_paste_history_get_password    (GPasteHistory *self,
                                                           const gchar   *name);
void                      g_paste_history_delete_password (GPasteHistory *self,
                                                           const gchar   *name);
void                      g_paste_history_rename_password (GPasteHistory *self,
                                                           const gchar   *old_name,
                                                           const gchar   *new_name);
void          g_paste_history_empty       (GPasteHistory *self);
gboolean      g_paste_history_save        (GPasteHistory *self);
void          g_paste_history_load        (GPasteHistory *self);
void          g_paste_history_switch      (GPasteHistory *self,
                                           const gchar   *name);
void          g_paste_history_delete      (GPasteHistory *self,
                                           GError       **error);
const GSList *g_paste_history_get_history (const GPasteHistory *self);
guint32       g_paste_history_get_length  (const GPasteHistory *self);

GArray *g_paste_history_search (const GPasteHistory *self,
                                const gchar         *pattern);

GPasteHistory *g_paste_history_new (GPasteSettings *settings);

GStrv g_paste_history_list (GError **error);

G_END_DECLS

#endif /*__G_PASTE_HISTORY_H__*/
