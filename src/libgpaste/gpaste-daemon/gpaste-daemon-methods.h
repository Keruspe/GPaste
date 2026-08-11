// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-daemon2.h>
#include <gpaste-3/gpaste-error.h>

#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-history.h>

G_BEGIN_DECLS

/* The subset of the daemon's state the D-Bus method handlers operate on, so
 * they can live outside the daemon object.
 *
 * @skeleton is the generated org.gnome.GPaste2 implementation the daemon
 * exports: handlers reach it to emit the interface's signals, which is all they
 * need the bus for — marshalling both ways is the skeleton's job now. */
typedef struct
{
    GPasteDaemon2           *skeleton;
    GPasteHistory           *history;
    GPasteSettings          *settings;
    GPasteClipboardsManager *clipboards_manager;
} GPasteDaemonMethods;

/* Bail out of a method handler with a #GPasteError. The domain is registered
 * with GDBus, so @code decides the error name the client sees and the code it
 * gets back; @_msg stays human-readable detail rather than the thing callers
 * have to match on. */
#define G_PASTE_DBUS_ASSERT_FULL(cond, code, _msg, ret)                   \
    do {                                                                  \
        if (!(cond))                                                      \
        {                                                                 \
            g_set_error_literal (error, G_PASTE_ERROR, (code), (_msg));   \
            return ret;                                                   \
        }                                                                 \
    } while (FALSE)

#define G_PASTE_DBUS_ASSERT(cond, code, _msg) G_PASTE_DBUS_ASSERT_FULL (cond, code, _msg, ;)

/* The handlers below take the method's arguments as the generated skeleton
 * hands them over, and return what its matching g_paste_daemon2_complete_*()
 * wants. A borrowed const gchar * is one the reply copies before the call
 * returns; a GVariant * is floating and consumed by the completion. */

void      g_paste_daemon_methods_do_add                     (const GPasteDaemonMethods *self,
                                                             const gchar               *text,
                                                             guint64                    length,
                                                             GError                   **error);
void      g_paste_daemon_methods_add                        (const GPasteDaemonMethods *self,
                                                             const gchar               *text,
                                                             GError                   **error);
void      g_paste_daemon_methods_add_file                   (const GPasteDaemonMethods *self,
                                                             const gchar               *file,
                                                             GError                   **error);
void      g_paste_daemon_methods_add_password               (const GPasteDaemonMethods *self,
                                                             const gchar               *name,
                                                             const gchar               *password,
                                                             GError                   **error);
void      g_paste_daemon_methods_backup_history             (const GPasteDaemonMethods *self,
                                                             const gchar               *history,
                                                             const gchar               *backup,
                                                             GError                   **error);
void      g_paste_daemon_methods_delete                     (const GPasteDaemonMethods *self,
                                                             const gchar               *uuid,
                                                             GError                   **error);
void      g_paste_daemon_methods_delete_history             (const GPasteDaemonMethods *self,
                                                             const gchar               *name,
                                                             GError                   **error);
void      g_paste_daemon_methods_delete_password            (const GPasteDaemonMethods *self,
                                                             const gchar               *name,
                                                             GError                   **error);
void      g_paste_daemon_methods_empty_history              (const GPasteDaemonMethods *self,
                                                             const gchar               *name);
const gchar *g_paste_daemon_methods_get_element             (const GPasteDaemonMethods *self,
                                                             const gchar               *uuid,
                                                             GError                   **error);
gboolean  g_paste_daemon_methods_get_element_at_index       (const GPasteDaemonMethods *self,
                                                             guint64                    index,
                                                             const gchar              **uuid,
                                                             const gchar              **value,
                                                             GError                   **error);
const gchar *g_paste_daemon_methods_get_element_kind        (const GPasteDaemonMethods *self,
                                                             const gchar               *uuid,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_get_elements               (const GPasteDaemonMethods *self,
                                                             const gchar * const       *uuids,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_get_history                (const GPasteDaemonMethods *self);
const gchar *g_paste_daemon_methods_get_history_name        (const GPasteDaemonMethods *self);
guint64   g_paste_daemon_methods_get_history_size           (const GPasteDaemonMethods *self,
                                                             const gchar               *name);
GVariant *g_paste_daemon_methods_get_image                  (const GPasteDaemonMethods *self,
                                                             const gchar               *uuid,
                                                             GError                   **error);
const gchar *g_paste_daemon_methods_get_raw_element         (const GPasteDaemonMethods *self,
                                                             const gchar               *uuid,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_get_raw_history            (const GPasteDaemonMethods *self);
GStrv     g_paste_daemon_methods_list_histories             (const GPasteDaemonMethods *self,
                                                             GError                   **error);
void      g_paste_daemon_methods_merge                      (const GPasteDaemonMethods *self,
                                                             const gchar               *decoration,
                                                             const gchar               *separator,
                                                             const gchar * const       *uuids,
                                                             GError                   **error);
void      g_paste_daemon_methods_rename_password            (const GPasteDaemonMethods *self,
                                                             const gchar               *old_name,
                                                             const gchar               *new_name,
                                                             GError                   **error);
GStrv     g_paste_daemon_methods_search                     (const GPasteDaemonMethods *self,
                                                             const gchar               *query,
                                                             GError                   **error);
void      g_paste_daemon_methods_select                     (const GPasteDaemonMethods *self,
                                                             const gchar               *uuid,
                                                             GError                   **error);
void      g_paste_daemon_methods_replace                    (const GPasteDaemonMethods *self,
                                                             const gchar               *uuid,
                                                             const gchar               *contents,
                                                             GError                   **error);
void      g_paste_daemon_methods_set_password               (const GPasteDaemonMethods *self,
                                                             const gchar               *uuid,
                                                             const gchar               *name,
                                                             GError                   **error);
void      g_paste_daemon_methods_switch_history             (const GPasteDaemonMethods *self,
                                                             const gchar               *name,
                                                             GError                   **error);
void      g_paste_daemon_methods_track                      (const GPasteDaemonMethods *self,
                                                             gboolean                   tracking_state);
void      g_paste_daemon_methods_extension_state_changed    (const GPasteDaemonMethods *self,
                                                             gboolean                   state);

G_END_DECLS
