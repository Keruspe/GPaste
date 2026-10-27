// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-ui-window.h>

G_BEGIN_DECLS

/* The dialog is held weakly: one whose window went before the reply has
 * nothing left to be told (g_paste_gtk_util_text_dialog ()). */
typedef struct
{
    GWeakRef             dialog;
    GPasteUiStringFinish finish;
    const gchar         *message;
} GPasteUiComposerReply;

/* Tell a text dialog how the call made of what it submitted went, the way
 * g_paste_ui_report_string () tells the window:
 *
 *   g_paste_client_add_text (client, text, NULL,
 *                            g_paste_ui_composer_reply_cb,
 *                            g_paste_ui_composer_reply (dialog,
 *                                                       g_paste_client_add_text_finish,
 *                                                       what_to_say_if_it_failed));
 *
 * @message is a translated literal, kept by pointer rather than copied. */
GPasteUiComposerReply *g_paste_ui_composer_reply      (AdwDialog             *dialog,
                                                       GPasteUiStringFinish   finish,
                                                       const gchar           *message);
void                   g_paste_ui_composer_reply_free (GPasteUiComposerReply *reply);

void                   g_paste_ui_composer_reply_cb   (GObject               *source_object,
                                                       GAsyncResult          *result,
                                                       gpointer               user_data);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPasteUiComposerReply, g_paste_ui_composer_reply_free)

G_END_DECLS
