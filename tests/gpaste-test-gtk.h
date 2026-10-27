// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

/* A save a dialog sent through a suite's stand-in for the client's call, kept
 * for the case to answer (g_paste_test_gtk_save_answer ()). */
typedef struct
{
    GAsyncReadyCallback callback;
    gpointer            data;
    /* What the save is answered with, once, when set. */
    GError             *error;
    guint               sent;
} GPasteTestGtkSave;

GtkWidget *g_paste_test_gtk_find_widget         (GtkWidget          *root,
                                                 GType               type);
GtkWidget *g_paste_test_gtk_find_button         (GtkWidget          *root,
                                                 const gchar        *label);
gboolean   g_paste_test_gtk_shows_label         (GtkWidget          *root,
                                                 const gchar        *text);
void       g_paste_test_gtk_wait_dialog_closed  (GtkWindow          *window);
gchar     *g_paste_test_gtk_dialog_text         (AdwDialog          *dialog);
void       g_paste_test_gtk_set_dialog_text     (AdwDialog          *dialog,
                                                 const gchar        *text);
void       g_paste_test_gtk_save_send           (GPasteTestGtkSave  *save,
                                                 GAsyncReadyCallback callback,
                                                 gpointer            data);
gchar     *g_paste_test_gtk_save_finish         (GPasteTestGtkSave  *save,
                                                 GError            **error);
void       g_paste_test_gtk_save_answer         (GPasteTestGtkSave  *save,
                                                 gpointer            client);
void       g_paste_test_gtk_assert_refusal_kept (GPasteTestGtkSave  *save,
                                                 gpointer            client,
                                                 GtkWindow          *window,
                                                 AdwDialog          *dialog,
                                                 const gchar        *label,
                                                 const gchar        *message);

G_END_DECLS
