/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_GTK_UTIL_H__
#define __G_PASTE_GTK_UTIL_H__

#include <gpaste/gpaste-settings.h>
#include <gpaste/gpaste-client.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

gboolean g_paste_util_confirm_dialog    (GtkWindow   *parent,
                                         const gchar *action,
                                         const gchar *msg);

gchar   *g_paste_util_compute_checksum  (GdkPixbuf   *image);

void     g_paste_util_empty_history     (GtkWindow      *parent_window,
                                         GPasteClient   *client,
                                         GPasteSettings *settings,
                                         const gchar    *history);

void     g_paste_util_show_win          (GApplication *application);

G_END_DECLS

#endif /*__G_PASTE_GTK_UTIL_H__*/
