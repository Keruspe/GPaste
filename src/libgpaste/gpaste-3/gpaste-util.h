// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-3/gpaste-settings.h>
#include <gpaste-3/gpaste-client.h>

G_BEGIN_DECLS

void     g_paste_util_spawn                   (const gchar *app);
gboolean g_paste_util_spawn_sync              (const gchar *app,
                                               GError     **error);
void     g_paste_util_activate_ui             (const gchar *action,
                                               GVariant    *arg);
gboolean g_paste_util_activate_ui_sync        (const gchar *action,
                                               GVariant    *arg,
                                               GError     **error);
void     g_paste_util_empty_with_confirmation (GPasteClient   *client,
                                               GPasteSettings *settings,
                                               const gchar    *history);
gchar   *g_paste_util_one_line                (const gchar *text);
gchar   *g_paste_util_display_string          (const gchar   *value,
                                               GPasteItemKind kind);

gboolean g_paste_util_has_gnome_shell                  (void);
void     g_paste_util_has_gnome_shell_extension        (GCancellable       *cancellable,
                                                        GAsyncReadyCallback callback,
                                                        gpointer            user_data);
gboolean g_paste_util_has_gnome_shell_extension_finish (GAsyncResult *result);

GPasteClientItem *g_paste_util_get_dbus_item_result      (GVariant *variant);
GList            *g_paste_util_get_dbus_items_result     (GVariant *variant);
GList            *g_paste_util_get_dbus_histories_result (GVariant *variant);

void g_paste_util_write_pid_file (const gchar *component);
GPid g_paste_util_read_pid_file  (const gchar *component);

gboolean g_paste_util_reexecute_daemon          (GPasteClient *client,
                                                 GError      **error);
gboolean g_paste_util_trigger_storage_migration (GPasteClient *client,
                                                 GError      **error);

/* The scale g_paste_util_password_strength() rates on, so a meter is built
 * against the contract rather than against a number copied out of it. */
#define G_PASTE_UTIL_STRENGTH_MAX 4

/* Whether this build can rate a password at all (libpwquality). A form that
 * cannot must say so rather than show a meter pinned at zero: someone choosing
 * a password should know it is not being judged, instead of reading a silent
 * zero as a verdict. */
gboolean g_paste_util_pwquality_available (void);

/* Rate @password on a 0-4 scale, GNOME-style (libpwquality, as
 * gnome-control-center does), returning in @hint the rating word or
 * libpwquality's own advice. Built without libpwquality there is no rating to
 * give: the level is 0 and @hint is %NULL. */
guint g_paste_util_password_strength (const gchar  *password,
                                      gchar       **hint);

G_END_DECLS
