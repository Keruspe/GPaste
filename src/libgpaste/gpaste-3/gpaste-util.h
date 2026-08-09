// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-3/gpaste-settings.h>
#include <gpaste-3/gpaste-client.h>

G_BEGIN_DECLS

void     g_paste_util_spawn             (const gchar *app);
gboolean g_paste_util_spawn_sync        (const gchar *app,
                                         GError     **error);
void     g_paste_util_activate_ui       (const gchar *action,
                                         GVariant    *arg);
void     g_paste_util_empty_with_confirmation (GPasteClient   *client,
                                               GPasteSettings *settings,
                                               const gchar    *history);
gchar   *g_paste_util_one_line          (const gchar *text);

gboolean g_paste_util_has_gnome_shell (void);

GList *g_paste_util_get_dbus_items_result (GVariant *variant);

void g_paste_util_write_pid_file (const gchar *component);
GPid g_paste_util_read_pid_file  (const gchar *component);

gboolean g_paste_util_reexecute_daemon          (GPasteClient *client,
                                                 GError      **error);
gboolean g_paste_util_trigger_storage_migration (GPasteClient *client,
                                                 GError      **error);

G_END_DECLS
