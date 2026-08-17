// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-3/gpaste-client-item.h>
#include <gpaste-3/gpaste-item-enums.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIENT (g_paste_client_get_type ())

G_PASTE_FINAL_TYPE (Client, client, CLIENT, GDBusProxy)

/*******************/
/* Methods /  Sync */
/*******************/

void     g_paste_client_show_about_sync                 (GPasteClient  *self,
                                                         GError       **error);
void     g_paste_client_add_text_sync                   (GPasteClient  *self,
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
void     g_paste_client_change_passphrase_sync          (GPasteClient  *self,
                                                         GError       **error);
void     g_paste_client_delete_item_sync                (GPasteClient  *self,
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
GPasteClientItem *g_paste_client_get_item_sync          (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);
GList   *g_paste_client_get_items_sync                  (GPasteClient  *self,
                                                         const gchar * const *uuids,
                                                         GError       **error);
GList   *g_paste_client_get_favourites_sync             (GPasteClient  *self,
                                                         GError       **error);
GList   *g_paste_client_get_history_sync                (GPasteClient  *self,
                                                         GError       **error);
guint64  g_paste_client_get_history_size_sync           (GPasteClient  *self,
                                                         const gchar   *name,
                                                         GError       **error);
GBytes  *g_paste_client_get_image_sync                  (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);
GStrv    g_paste_client_get_uris_sync                   (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);
GStrv    g_paste_client_list_histories_sync             (GPasteClient  *self,
                                                         GError       **error);
void     g_paste_client_merge_sync                      (GPasteClient  *self,
                                                         const gchar   *decoration,
                                                         const gchar   *separator,
                                                         const gchar * const *uuids,
                                                         GError       **error);
void     g_paste_client_report_extension_state_sync     (GPasteClient  *self,
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
GList   *g_paste_client_search_sync                     (GPasteClient  *self,
                                                         const gchar   *pattern,
                                                         GError       **error);
void     g_paste_client_select_sync                     (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);
void     g_paste_client_set_favourite_sync              (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         gboolean       favourite,
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
void     g_paste_client_set_active_sync                 (GPasteClient  *self,
                                                         gboolean       state,
                                                         GError       **error);
void     g_paste_client_upload_sync                     (GPasteClient  *self,
                                                         const gchar   *uuid,
                                                         GError       **error);

GPasteClientItem *g_paste_client_get_item_at_index_sync (GPasteClient  *self,
                                                         guint64        index,
                                                         GError       **error);
/*******************/
/* Methods / Async */
/*******************/

void g_paste_client_show_about                 (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_add_text                   (GPasteClient       *self,
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
void g_paste_client_change_passphrase          (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_delete_item                (GPasteClient       *self,
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
void g_paste_client_get_item                   (GPasteClient       *self,
                                                const gchar        *uuid,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_item_at_index          (GPasteClient       *self,
                                                guint64             index,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_items                  (GPasteClient       *self,
                                                const gchar * const *uuids,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_favourites             (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_history                (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_history_size           (GPasteClient       *self,
                                                const gchar        *name,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_image                  (GPasteClient       *self,
                                                const gchar        *uuid,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_get_uris                   (GPasteClient       *self,
                                                const gchar        *uuid,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_list_histories             (GPasteClient       *self,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_merge                      (GPasteClient       *self,
                                                const gchar        *decoration,
                                                const gchar        *separator,
                                                const gchar * const *uuids,
                                                GAsyncReadyCallback callback,
                                                gpointer            user_data);
void g_paste_client_report_extension_state     (GPasteClient       *self,
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
void g_paste_client_set_favourite              (GPasteClient       *self,
                                                const gchar        *uuid,
                                                gboolean            favourite,
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
void g_paste_client_set_active                 (GPasteClient       *self,
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

void     g_paste_client_show_about_finish                 (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_add_text_finish                   (GPasteClient *self,
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
void     g_paste_client_change_passphrase_finish          (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_delete_item_finish                (GPasteClient *self,
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
GPasteClientItem *g_paste_client_get_item_finish          (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GList   *g_paste_client_get_items_finish                  (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GList   *g_paste_client_get_favourites_finish             (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GList   *g_paste_client_get_history_finish                (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
guint64  g_paste_client_get_history_size_finish           (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GBytes  *g_paste_client_get_image_finish                  (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GStrv    g_paste_client_get_uris_finish                   (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
GStrv    g_paste_client_list_histories_finish             (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_merge_finish                      (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_report_extension_state_finish     (GPasteClient *self,
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
GList   *g_paste_client_search_finish                     (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_select_finish                     (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_set_favourite_finish              (GPasteClient *self,
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
void     g_paste_client_set_active_finish                 (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);
void     g_paste_client_upload_finish                     (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);

GPasteClientItem *g_paste_client_get_item_at_index_finish (GPasteClient *self,
                                                           GAsyncResult *result,
                                                           GError      **error);

/**************/
/* Properties */
/**************/

gboolean g_paste_client_is_active        (GPasteClient *self);
gchar   *g_paste_client_get_history_name (GPasteClient *self);
gchar   *g_paste_client_get_version      (GPasteClient *self);

/****************/
/* Constructors */
/****************/

GPasteClient *g_paste_client_new_sync   (GError            **error);
void          g_paste_client_new        (GAsyncReadyCallback callback,
                                         gpointer            user_data);
GPasteClient *g_paste_client_new_finish (GAsyncResult       *result,
                                         GError            **error);

G_END_DECLS
