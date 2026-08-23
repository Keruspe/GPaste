// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_GTK4_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk4.h> can be included directly."
#endif

#pragma once

#include <gpaste.h>

#include <adwaita.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef void (*GPasteGtkConfirmDialogCallback) (gboolean confirmed,
                                                gpointer user_data);

void     g_paste_gtk_util_confirm_dialog   (GtkWindow                     *parent,
                                            const gchar                   *heading,
                                            const gchar                   *body,
                                            const gchar                   *action,
                                            AdwResponseAppearance          appearance,
                                            GPasteGtkConfirmDialogCallback on_confirmation,
                                            gpointer                       user_data);

GdkTexture *g_paste_gtk_util_get_image_finish (GPasteClient *client,
                                               GAsyncResult *result,
                                               GError      **error);

void     g_paste_gtk_util_empty_history    (GtkWindow      *parent_window,
                                            GPasteClient   *client,
                                            GPasteSettings *settings,
                                            const gchar    *history);

void     g_paste_gtk_util_show_window      (GApplication *application);

/* @text is what the user wrote, or %NULL if they cancelled. */
typedef void (*GPasteGtkTextDialogCallback) (const gchar *text,
                                             gpointer     user_data);

void     g_paste_gtk_util_text_dialog      (GtkWindow                  *parent,
                                            const gchar                *heading,
                                            const gchar                *confirm_label,
                                            const gchar                *text,
                                            GPasteGtkTextDialogCallback callback,
                                            gpointer                    user_data);

G_END_DECLS
