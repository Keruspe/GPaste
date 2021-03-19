/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_CLIENT_H__
#define __G_PASTE_CLIENT_H__

#include <gpaste-client-item.h>
#include <gpaste-item-enums.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIENT (g_paste_client_get_type ())

G_PASTE_FINAL_TYPE (Client, client, CLIENT, GDBusProxy)

/*******************/
/* Methods /  Sync */
/*******************/

void     g_paste_client_about_sync                      (GPasteClient  *self,
                                                         GError       **error);
void     g_paste_client_add_sync                        (GPasteClient  *self,
                                                         const gchar   *text,
                                                         GError       **error);
void     g_paste_client_add_file_sync                   (GPasteClient  *self,
                                                         const gchar   *file,
                                                         GError       **error);
void     g_paste_client_add_password_sync               (GPasteClient  *self,
                                                         const gchar   *name,
                                                         const gchar   *password,
                                                         GError       **error);
void     g_paste_client_backup_history_sync             (GPasteClient  *self,
                                                         const gchar   *history,
                                                         const gchar   *backup,
                                                         GError       **error);
void     g_paste_client_delete_sync                     (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);
void     g_paste_client_delete_history_sync             (GPasteClient  *self,
                                                         const gchar   *name,
                                                         GError       **error);
void     g_paste_client_delete_password_sync            (GPasteClient  *self,
                                                         const gchar   *name,
                                                         GError       **error);
void     g_paste_client_empty_history_sync              (GPasteClient  *self,
                                                         const gchar   *name,
                                                         GError       **error);
gchar   *g_paste_client_get_element_sync                (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);
GList   *g_paste_client_get_elements_sync               (GPasteClient  *self,
                                                         const gchar  **uuids,
                                                         guint64        n_uuids,
                                                         GError       **error);
GList   *g_paste_client_get_history_sync                (GPasteClient  *self,
                                                         GError       **error);
gchar   *g_paste_client_get_history_name_sync           (GPasteClient  *self,
                                                         GError       **error);
guint64  g_paste_client_get_history_size_sync           (GPasteClient  *self,
                                                         const gchar   *name,
                                                         GError       **error);
gchar   *g_paste_client_get_raw_element_sync            (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);
GList   *g_paste_client_get_raw_history_sync            (GPasteClient  *self,
                                                         GError       **error);
GStrv    g_paste_client_list_histories_sync             (GPasteClient  *self,
                                                         GError       **error);
void     g_paste_client_merge_sync                      (GPasteClient  *self,
                                                         const gchar   *decoration,
                                                         const gchar   *separator,
                                                         const gchar  **uuids,
                                                         guint64        n_uuids,
                                                         GError       **error);
void     g_paste_client_on_extension_state_changed_sync (GPasteClient  *self,
                                                         gboolean       state,
                                                         GError       **error);
void     g_paste_client_reexecute_sync                  (GPasteClient  *self,
                                                         GError       **error);
void     g_paste_client_rename_password_sync            (GPasteClient  *self,
                                                         const gchar   *old_name,
                                                         const gchar   *new_name,
                                                         GError       **error);
void     g_paste_client_replace_sync                    (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         const gchar   *contents,
                                                         GError       **error);
GStrv    g_paste_client_search_sync                     (GPasteClient  *self,
                                                         const gchar   *pattern,
                                                         GError       **error);
void     g_paste_client_select_sync                     (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);
void     g_paste_client_set_password_sync               (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         const gchar   *name,
                                                         GError       **error);
void     g_paste_client_show_history_sync               (GPasteClient  *self,
                                                         GError       **error);
void     g_paste_client_switch_history_sync             (GPasteClient  *self,
                                                         const gchar   *name,
                                                         GError       **error);
void     g_paste_client_track_sync                      (GPasteClient  *self,
                                                         gboolean       state,
                                                         GError       **error);
void     g_paste_client_upload_sync                     (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);

GPasteClientItem *g_paste_client_get_element_at_index_sync (GPasteClient  *self,
                                                            guint64        index,
                                                            GError       **error);
GPasteItemKind    g_paste_client_get_element_kind_sync     (GPasteClient *self,
                                                            const gchar  *uuid,
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
                                                const gchar        *history,
                                                const gchar        *backup,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_delete                     (GPasteClient       *self,
                                                const gchar        *uuid,
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
void g_paste_client_empty_history              (GPasteClient       *self,
                                                const gchar        *name,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_element                (GPasteClient       *self,
                                                const gchar        *uuid,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_element_at_index       (GPasteClient       *self,
                                                guint64             index,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_element_kind           (GPasteClient       *self,
                                                const gchar        *uuid,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_elements               (GPasteClient       *self,
                                                const gchar       **uuids,
                                                guint64             n_uuids,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_history                (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_history_name           (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_history_size           (GPasteClient       *self,
                                                const gchar        *name,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_raw_element            (GPasteClient       *self,
                                                const gchar        *uuid,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_raw_history            (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_list_histories             (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_merge                      (GPasteClient       *self,
                                                const gchar        *decoration,
                                                const gchar        *separator,
                                                const gchar       **uuids,
                                                guint64             n_uuids,
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
void g_paste_client_replace                    (GPasteClient       *self,
                                                const gchar        *uuid,
                                                const gchar        *contents,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_search                     (GPasteClient       *self,
                                                const gchar        *pattern,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_select                     (GPasteClient       *self,
                                                const gchar        *uuid,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_set_password               (GPasteClient       *self,
                                                const gchar        *uuid,
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
void g_paste_client_upload                     (GPasteClient       *self,
                                                const gchar        *uuid,
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
void     g_paste_client_empty_history_finish              (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
gchar   *g_paste_client_get_element_finish                (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GList   *g_paste_client_get_elements_finish               (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GList   *g_paste_client_get_history_finish                (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
gchar   *g_paste_client_get_history_name_finish           (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
guint64  g_paste_client_get_history_size_finish           (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
gchar   *g_paste_client_get_raw_element_finish            (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GList   *g_paste_client_get_raw_history_finish            (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GStrv    g_paste_client_list_histories_finish             (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_merge_finish                      (GPasteClient *self,
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
void     g_paste_client_replace_finish                    (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GStrv    g_paste_client_search_finish                     (GPasteClient *self,
                                                           GAsyncResult *result,
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
void     g_paste_client_upload_finish                     (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);

GPasteClientItem *g_paste_client_get_element_at_index_finish (GPasteClient *self,
                                                              GAsyncResult *result,
                                                              GError      **error);
GPasteItemKind    g_paste_client_get_element_kind_finish     (GPasteClient *self,
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
