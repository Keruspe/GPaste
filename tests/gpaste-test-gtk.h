// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

/* What a suite's stand-in for one of the client's calls -- a dialog's save, the
 * sidebar's switch -- was handed, kept for the case to answer
 * (g_paste_test_gtk_save_answer ()). */
typedef struct
{
    GAsyncReadyCallback callback;
    gpointer            data;
    /* What the save is answered with, once, when set. */
    GError             *error;
    guint               sent;
    /* The text the last save carried, for a stand-in to record. */
    gchar              *text;
} GPasteTestGtkSave;

GtkWidget     *g_paste_test_gtk_find_widget         (GtkWidget          *root,
                                                     GType               type);
GtkWidget     *g_paste_test_gtk_find_button         (GtkWidget          *root,
                                                     const gchar        *label);
GPtrArray     *g_paste_test_gtk_find_all            (GtkWidget          *root,
                                                     GType               type);
gboolean       g_paste_test_gtk_shows_label         (GtkWidget          *root,
                                                     const gchar        *text);
gboolean       g_paste_test_gtk_widget_mapped       (gconstpointer       widget,
                                                     gconstpointer       arg);
void           g_paste_test_gtk_wait_dialog_closed  (GtkWindow          *window);
GtkTextBuffer *g_paste_test_gtk_dialog_buffer       (AdwDialog          *dialog);
gchar         *g_paste_test_gtk_dialog_text         (AdwDialog          *dialog);
void           g_paste_test_gtk_set_dialog_text     (AdwDialog          *dialog,
                                                     const gchar        *text);
void           g_paste_test_gtk_save_send           (GPasteTestGtkSave  *save,
                                                     GAsyncReadyCallback callback,
                                                     gpointer            data);
gchar         *g_paste_test_gtk_save_finish         (GPasteTestGtkSave  *save,
                                                     GError            **error);
void           g_paste_test_gtk_save_answer         (GPasteTestGtkSave  *save,
                                                     gpointer            client);
void           g_paste_test_gtk_assert_refusal_kept (GPasteTestGtkSave  *save,
                                                     gpointer            client,
                                                     GtkWindow          *window,
                                                     AdwDialog          *dialog,
                                                     const gchar        *label,
                                                     const gchar        *text,
                                                     const gchar        *message);

G_END_DECLS
