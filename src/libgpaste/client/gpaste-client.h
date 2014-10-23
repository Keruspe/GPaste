/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#ifndef __G_PASTE_CLIENT_H__
#define __G_PASTE_CLIENT_H__

#include <gpaste-update-enums.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIENT            (g_paste_client_get_type ())
#define G_PASTE_CLIENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), G_PASTE_TYPE_CLIENT, GPasteClient))
#define G_PASTE_IS_CLIENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), G_PASTE_TYPE_CLIENT))
#define G_PASTE_CLIENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), G_PASTE_TYPE_CLIENT, GPasteClientClass))
#define G_PASTE_IS_CLIENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), G_PASTE_TYPE_CLIENT))
#define G_PASTE_CLIENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), G_PASTE_TYPE_CLIENT, GPasteClientClass))

typedef struct _GPasteClient GPasteClient;
typedef struct _GPasteClientClass GPasteClientClass;

G_PASTE_VISIBLE
GType g_paste_client_get_type (void);

/*******************/
/* Methods /  Sync */
/*******************/

void     g_paste_client_about_sync                      (GPasteClient *self,
                                                         GError      **error);
void     g_paste_client_add_sync                        (GPasteClient *self,
                                                         const gchar  *text,
                                                         GError      **error);
void     g_paste_client_add_file_sync                   (GPasteClient *self,
                                                         const gchar  *file,
                                                         GError      **error);
void     g_paste_client_add_password_sync               (GPasteClient *self,
                                                         const gchar  *name,
                                                         const gchar  *password,
                                                         GError      **error);
void     g_paste_client_backup_history_sync             (GPasteClient *self,
                                                         const gchar  *name,
                                                         GError      **error);
void     g_paste_client_delete_sync                     (GPasteClient *self,
                                                         guint32       index,
                                                         GError      **error);
void     g_paste_client_delete_history_sync             (GPasteClient *self,
                                                         const gchar  *name,
                                                         GError      **error);
void     g_paste_client_delete_password_sync            (GPasteClient *self,
                                                         const gchar  *name,
                                                         GError      **error);
void     g_paste_client_empty_sync                      (GPasteClient *self,
                                                         GError      **error);
gchar   *g_paste_client_get_element_sync                (GPasteClient *self,
                                                         guint32       index,
                                                         GError      **error);
GStrv    g_paste_client_get_history_sync                (GPasteClient *self,
                                                         GError      **error);
guint32  g_paste_client_get_history_size_sync           (GPasteClient *self,
                                                         GError      **error);
gchar   *g_paste_client_get_raw_element_sync            (GPasteClient *self,
                                                         guint32       index,
                                                         GError      **error);
GStrv    g_paste_client_get_raw_history_sync            (GPasteClient *self,
                                                         GError      **error);
GStrv    g_paste_client_list_histories_sync             (GPasteClient *self,
                                                         GError      **error);
void     g_paste_client_on_extension_state_changed_sync (GPasteClient *self,
                                                         gboolean      state,
                                                         GError      **error);
void     g_paste_client_reexecute_sync                  (GPasteClient *self,
                                                         GError      **error);
void     g_paste_client_rename_password_sync            (GPasteClient *self,
                                                         const gchar  *old_name,
                                                         const gchar  *new_name,
                                                         GError      **error);
guint32 *g_paste_client_search_sync                     (GPasteClient *self,
                                                         const gchar  *pattern,
                                                         gsize        *hits,
                                                         GError      **error);
void     g_paste_client_select_sync                     (GPasteClient *self,
                                                         guint32       index,
                                                         GError      **error);
void     g_paste_client_set_password_sync               (GPasteClient *self,
                                                         guint32       index,
                                                         const gchar  *name,
                                                         GError      **error);
void     g_paste_client_show_history_sync               (GPasteClient *self,
                                                         GError      **error);
void     g_paste_client_switch_history_sync             (GPasteClient *self,
                                                         const gchar  *name,
                                                         GError      **error);
void     g_paste_client_track_sync                      (GPasteClient *self,
                                                         gboolean      state,
                                                         GError      **error);

/*******************/
/* Methods / Async */
/*******************/

void g_paste_client_about                      (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_add                        (GPasteClient       *self,
                                                const gchar        *text,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_add_file                   (GPasteClient       *self,
                                                const gchar        *file,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_add_password               (GPasteClient       *self,
                                                const gchar        *name,
                                                const gchar        *password,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_backup_history             (GPasteClient       *self,
                                                const gchar        *name,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_delete                     (GPasteClient       *self,
                                                guint32             index,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_delete_history             (GPasteClient       *self,
                                                const gchar        *name,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_delete_password            (GPasteClient       *self,
                                                const gchar        *name,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_empty                      (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_element                (GPasteClient       *self,
                                                guint32             index,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_history                (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_history_size           (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_raw_element            (GPasteClient       *self,
                                                guint32             index,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_raw_history            (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_list_histories             (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_on_extension_state_changed (GPasteClient       *self,
                                                gboolean            state,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_reexecute                  (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_rename_password            (GPasteClient       *self,
                                                const gchar        *old_name,
                                                const gchar        *new_name,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_search                     (GPasteClient       *self,
                                                const gchar        *pattern,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_select                     (GPasteClient       *self,
                                                guint32             index,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_set_password               (GPasteClient       *self,
                                                guint32             index,
                                                const gchar        *name,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_show_history               (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_switch_history             (GPasteClient       *self,
                                                const gchar        *name,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_track                      (GPasteClient       *self,
                                                gboolean            state,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);

/****************************/
/* Methods / Async - Finish */
/****************************/

void     g_paste_client_about_finish                      (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_add_finish                        (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_add_file_finish                   (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_add_password_finish               (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_backup_history_finish             (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_delete_finish                     (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_delete_history_finish             (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_delete_password_finish            (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_empty_finish                      (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
gchar   *g_paste_client_get_element_finish                (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GStrv    g_paste_client_get_history_finish                (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
guint32  g_paste_client_get_history_size_finish           (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
gchar   *g_paste_client_get_raw_element_finish            (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GStrv    g_paste_client_get_raw_history_finish            (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GStrv    g_paste_client_list_histories_finish             (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_on_extension_state_changed_finish (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_reexecute_finish                  (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_rename_password_finish            (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
guint32 *g_paste_client_search_finish                     (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           gsize        *hits,
                                                           GError      **error);
void     g_paste_client_select_finish                     (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_set_password_finish               (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_show_history_finish               (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_switch_history_finish             (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_track_finish                      (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);

/**************/
/* Properties */
/**************/

gboolean g_paste_client_is_active   (GPasteClient *self);
gchar   *g_paste_client_get_version (GPasteClient *self);

/****************/
/* Constructors */
/****************/

GPasteClient *g_paste_client_new_sync   (GError            **error);
void          g_paste_client_new        (GAsyncReadyCallback callback,
                                         gpointer            user_data);
GPasteClient *g_paste_client_new_finish (GAsyncResult       *result,
                                         GError            **error);

G_END_DECLS

#endif /*__G_PASTE_CLIENT_H__*/
