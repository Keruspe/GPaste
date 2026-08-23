// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-3/gpaste-client.h>
#include <gpaste-3/gpaste-macros.h>

#include <adwaita.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_UI_WINDOW (g_paste_ui_window_get_type ())

G_PASTE_FINAL_TYPE (UiWindow, ui_window, UI_WINDOW, AdwApplicationWindow)

/* The two shapes a client call's _finish () comes in, for the callbacks below. */
typedef void   (*GPasteUiVoidFinish)   (GPasteClient *client,
                                        GAsyncResult *result,
                                        GError      **error);
typedef gchar *(*GPasteUiStringFinish) (GPasteClient *client,
                                        GAsyncResult *result,
                                        GError      **error);

/* Report a failed call to the user rather than to a console nobody is reading.
 * Pair the matching _cb with what _report_* returns:
 *
 *   g_paste_client_delete_item (client, uuid,
 *                               g_paste_ui_report_void_cb,
 *                               g_paste_ui_report_void (widget,
 *                                                       g_paste_client_delete_item_finish,
 *                                                       what_to_say_if_it_failed));
 *
 * @origin is anything inside the window; @message is a translated literal, kept
 * by pointer rather than copied. */
gpointer g_paste_ui_report_void   (GtkWidget           *origin,
                                   GPasteUiVoidFinish   finish,
                                   const gchar         *message);
gpointer g_paste_ui_report_string (GtkWidget           *origin,
                                   GPasteUiStringFinish finish,
                                   const gchar         *message);

void g_paste_ui_report_void_cb   (GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data);
void g_paste_ui_report_string_cb (GObject      *source_object,
                                  GAsyncResult *result,
                                  gpointer      user_data);

void g_paste_ui_window_empty_history (GPasteUiWindow *self,
                                      const gchar    *history);
void g_paste_ui_window_search        (GPasteUiWindow *self,
                                      const gchar    *search);
void g_paste_ui_window_show_prefs    (GPasteUiWindow *self);
void g_paste_ui_window_show_about    (GPasteUiWindow *self);

GtkWidget *g_paste_ui_window_new (GtkApplication *app);

G_END_DECLS

