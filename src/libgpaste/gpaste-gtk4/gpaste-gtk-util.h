/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK4_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk4.h> can be included directly."
#endif

#pragma once

#include <gpaste.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef void (*GPasteGtkConfirmDialogCallback) (gboolean confirmed,
                                                gpointer user_data);

void     g_paste_gtk_util_confirm_dialog   (GtkWindow                     *parent,
                                            const gchar                   *action,
                                            const gchar                   *msg,
                                            GPasteGtkConfirmDialogCallback on_confirmation,
                                            gpointer                       user_data);

gchar   *g_paste_gtk_util_compute_checksum (GdkTexture *texture);

void     g_paste_gtk_util_empty_history    (GtkWindow      *parent_window,
                                            GPasteClient   *client,
                                            GPasteSettings *settings,
                                            const gchar    *history);

void     g_paste_gtk_util_show_window      (GApplication *application);

G_END_DECLS
