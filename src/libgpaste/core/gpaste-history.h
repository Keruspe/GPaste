/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_HISTORY_H__
#define __G_PASTE_HISTORY_H__

#include <gpaste-password-item.h>
#include <gpaste-settings.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_HISTORY (g_paste_history_get_type ())

G_PASTE_FINAL_TYPE (History, history, HISTORY, GObject)

void              g_paste_history_add                (GPasteHistory *self,
                                                      GPasteItem    *item);
void              g_paste_history_remove             (GPasteHistory *self,
                                                      guint64        index);
gboolean          g_paste_history_remove_by_uuid     (GPasteHistory *self,
                                                      const gchar   *uuid);
const GPasteItem *g_paste_history_get                (GPasteHistory *self,
                                                      guint64        index);
const GPasteItem *g_paste_history_get_by_uuid        (GPasteHistory *self,
                                                      const gchar   *uuid);
GPasteItem       *g_paste_history_dup                (GPasteHistory *self,
                                                      guint64        index);
gboolean          g_paste_history_select             (GPasteHistory *self,
                                                      const gchar   *uuid);
void              g_paste_history_replace            (GPasteHistory *self,
                                                      const gchar   *uuid,
                                                      const gchar   *contents);
void              g_paste_history_refresh_item_size  (GPasteHistory    *self,
                                                      const GPasteItem *item,
                                                      guint64           old_size);
void                      g_paste_history_set_password    (GPasteHistory *self,
                                                           const gchar   *uuid,
                                                           const gchar   *name);
const GPastePasswordItem *g_paste_history_get_password    (GPasteHistory *self,
                                                           const gchar   *name);
void                      g_paste_history_delete_password (GPasteHistory *self,
                                                           const gchar   *name);
void                      g_paste_history_rename_password (GPasteHistory *self,
                                                           const gchar   *old_name,
                                                           const gchar   *new_name);
void         g_paste_history_empty       (GPasteHistory *self);
void         g_paste_history_save        (GPasteHistory *self,
                                          const gchar   *name);
void         g_paste_history_load        (GPasteHistory *self,
                                          const gchar   *name);
void         g_paste_history_switch      (GPasteHistory *self,
                                          const gchar   *name);
void         g_paste_history_delete      (GPasteHistory *self,
                                          const gchar   *name,
                                          GError       **error);
const GList *g_paste_history_get_history (const GPasteHistory *self);
guint64      g_paste_history_get_length  (const GPasteHistory *self);
const gchar *g_paste_history_get_current (const GPasteHistory *self);

GStrv g_paste_history_search (const GPasteHistory *self,
                              const gchar         *pattern);

GPasteHistory *g_paste_history_new (GPasteSettings *settings);

GStrv g_paste_history_list (GError **error);

G_END_DECLS

#endif /*__G_PASTE_HISTORY_H__*/
