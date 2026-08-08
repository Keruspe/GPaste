// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-error.h>
#include <gpaste-3/gpaste-gdbus-defines.h>

#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-history.h>

G_BEGIN_DECLS

/* The subset of the daemon's state the D-Bus method handlers operate on, so
 * they can live outside the daemon object. */
typedef struct
{
    GDBusConnection         *connection;
    GPasteHistory           *history;
    GPasteSettings          *settings;
    GPasteClipboardsManager *clipboards_manager;
} GPasteDaemonMethods;

/* @conn is explicit: these are expanded both from the daemon object and from
 * the free-standing method handlers, which hold their connection in different
 * places. They used to reach for a local named priv, which is why the context
 * struct above had to mirror the daemon's field names. */
#define G_PASTE_SEND_DBUS_SIGNAL_FULLER(conn, interface, sig, data, error) \
    g_dbus_connection_emit_signal (conn,                                   \
                                   NULL, /* destination_bus_name */        \
                                   G_PASTE_DAEMON_OBJECT_PATH,             \
                                   interface,                              \
                                   sig,                                    \
                                   data,                                   \
                                   error)

#define G_PASTE_SEND_DBUS_SIGNAL_FULL(conn,sig,data,error) \
    G_PASTE_SEND_DBUS_SIGNAL_FULLER (conn, G_PASTE_DAEMON_INTERFACE_NAME, G_PASTE_DAEMON_SIG_##sig, data, error)

#define G_PASTE_SEND_DBUS_PROPERTIES_CHANGED(conn, property, value) \
    GVariantDict dict;                                              \
    g_variant_dict_init (&dict, NULL);                              \
    g_variant_dict_insert_value (&dict, property, value);           \
    GVariant *data = g_variant_new ("(s@a{sv}@as)",                 \
                                    G_PASTE_DAEMON_INTERFACE_NAME,  \
                                    g_variant_dict_end (&dict),     \
                                    g_variant_new_strv (NULL, 0));  \
    G_PASTE_SEND_DBUS_SIGNAL_FULLER (conn, "org.freedesktop.DBus.Properties", "PropertiesChanged", data, NULL)

#define __NODATA     g_variant_new_tuple (NULL,  0)
#define __DATA(data) g_variant_new_tuple (&data, 1)

#define G_PASTE_SEND_DBUS_SIGNAL(conn,sig)             G_PASTE_SEND_DBUS_SIGNAL_FULL(conn, sig, __NODATA,  NULL)
#define G_PASTE_SEND_DBUS_SIGNAL_WITH_ERROR(conn,sig)  G_PASTE_SEND_DBUS_SIGNAL_FULL(conn, sig, __NODATA,  error)
#define G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA(conn,sig,d) G_PASTE_SEND_DBUS_SIGNAL_FULL(conn, sig, __DATA(d), NULL)

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

void      g_paste_daemon_methods_do_add                     (const GPasteDaemonMethods *priv,
                                                             const gchar               *text,
                                                             guint64                    length,
                                                             GError                   **error);
void      g_paste_daemon_methods_add                        (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_add_file                   (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_add_password               (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_backup_history             (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_delete                     (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_delete_history             (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_delete_password            (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_empty_history              (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters);
GVariant *g_paste_daemon_methods_get_element               (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_get_element_at_index      (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_get_element_kind          (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_get_elements              (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_get_history               (const GPasteDaemonMethods *priv);
GVariant *g_paste_daemon_methods_get_history_name          (const GPasteDaemonMethods *priv);
GVariant *g_paste_daemon_methods_get_history_size          (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters);
GVariant *g_paste_daemon_methods_get_image                 (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_get_raw_element           (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_get_raw_history           (const GPasteDaemonMethods *priv);
GVariant *g_paste_daemon_methods_list_histories            (const GPasteDaemonMethods *priv,
                                                             GError                   **error);
void      g_paste_daemon_methods_merge                      (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_rename_password            (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
GVariant *g_paste_daemon_methods_search                    (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_select                     (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_replace                    (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_set_password               (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_switch_history             (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters,
                                                             GError                   **error);
void      g_paste_daemon_methods_track                      (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters);
void      g_paste_daemon_methods_on_extension_state_changed (const GPasteDaemonMethods *priv,
                                                             GVariant                  *parameters);
void      g_paste_daemon_methods_extension_state_changed    (const GPasteDaemonMethods *priv,
                                                             gboolean                   state);

G_END_DECLS
