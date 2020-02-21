/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_UTIL_H__
#define __G_PASTE_UTIL_H__

#include <gpaste-settings.h>
#include <gpaste-client.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

gboolean g_paste_util_confirm_dialog    (GtkWindow   *parent,
                                         const gchar *action,
                                         const gchar *msg);
void     g_paste_util_spawn             (const gchar *app);
gboolean g_paste_util_spawn_sync        (const gchar *app,
                                         GError     **error);
void     g_paste_util_activate_ui       (const gchar *action,
                                         GVariant    *arg);
gboolean g_paste_util_activate_ui_sync  (const gchar *action,
                                         GVariant    *arg,
                                         GError     **error);
void     g_paste_util_empty_with_confirmation (GPasteClient         *client,
                                               const GPasteSettings *settings,
                                               const gchar          *history);
gboolean g_paste_util_empty_with_confirmation_sync (GPasteClient         *client,
                                                    const GPasteSettings *settings,
                                                    const gchar          *history,
                                                    GError              **error);
gchar   *g_paste_util_replace           (const gchar *text,
                                         const gchar *pattern,
                                         const gchar *substitution);
gchar   *g_paste_util_compute_checksum  (GdkPixbuf   *image);

void     g_paste_util_empty_history     (GtkWindow      *parent_window,
                                         GPasteClient   *client,
                                         GPasteSettings *settings,
                                         const gchar    *history);

gboolean g_paste_util_has_gnome_shell (void);

void g_paste_util_show_win (GApplication *application);

guint32 *g_paste_util_get_dbus_au_result (GVariant *variant,
                                          guint64  *len);

GPasteClientItem *g_paste_util_get_dbus_item_result  (GVariant *variant);
GList            *g_paste_util_get_dbus_items_result (GVariant *variant);

void g_paste_util_write_pid_file (const gchar *component);
GPid g_paste_util_read_pid_file  (const gchar *component);

gchar *g_paste_util_xml_decode (const gchar *text);
gchar *g_paste_util_xml_encode (const gchar *text);

gchar *g_paste_util_get_history_dir_path  (void);
GFile *g_paste_util_get_history_dir       (void);
gchar *g_paste_util_get_history_file_path (const gchar *name,
                                           const gchar *extension);
GFile *g_paste_util_get_history_file      (const gchar *name,
                                           const gchar *extension);

gboolean g_paste_util_ensure_history_dir_exists (const GPasteSettings *settings);

G_END_DECLS

#endif /*__G_PASTE_UTIL_H__*/
