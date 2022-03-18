/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2022, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_GTK3_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste-gtk3.h> can be included directly."
#endif

#pragma once

#include <gpaste.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

gboolean g_paste_gtk_util_confirm_dialog   (GtkWindow   *parent,
                                            const gchar *action,
                                            const gchar *msg);

gchar   *g_paste_gtk_util_compute_checksum (GdkPixbuf   *image);

void     g_paste_gtk_util_empty_history    (GtkWindow      *parent_window,
                                            GPasteClient   *client,
                                            GPasteSettings *settings,
                                            const gchar    *history);

void     g_paste_gtk_util_show_win         (GApplication *application);

G_END_DECLS
