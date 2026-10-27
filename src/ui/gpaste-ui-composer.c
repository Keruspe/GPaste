// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-composer.h>

/**
 * g_paste_ui_composer_reply:
 * @dialog: the dialog g_paste_gtk_util_text_dialog () handed to its submit
 * @finish: the call's _finish ()
 * @message: what to say should the call have failed
 *
 * Returns: (transfer full): the user_data to pass alongside
 *          g_paste_ui_composer_reply_cb (), or to a reply of the caller's own,
 *          which frees it with g_paste_ui_composer_reply_free ()
 */
GPasteUiComposerReply *
g_paste_ui_composer_reply (AdwDialog           *dialog,
                           GPasteUiStringFinish finish,
                           const gchar         *message)
{
    GPasteUiComposerReply *reply = g_new0 (GPasteUiComposerReply, 1);

    g_weak_ref_init (&reply->dialog, dialog);
    reply->finish = finish;
    reply->message = message;

    return reply;
}

void
g_paste_ui_composer_reply_free (GPasteUiComposerReply *reply)
{
    g_weak_ref_clear (&reply->dialog);
    g_free (reply);
}

/**
 * g_paste_ui_composer_reply_cb:
 * @source_object: the #GPasteClient the call was made on
 * @result: the #GAsyncResult handed to the callback
 * @user_data: what g_paste_ui_composer_reply () returned
 *
 * Close the dialog if the call went through, or hand it back to the user with
 * the failure said above what they wrote. The uuid the call answers is of no
 * use here: the item arrives as an update of its own.
 */
void
g_paste_ui_composer_reply_cb (GObject      *source_object,
                              GAsyncResult *result,
                              gpointer      user_data)
{
    g_autoptr (GPasteUiComposerReply) reply = user_data;
    g_autoptr (GError) error = NULL;
    g_autofree gchar *uuid = reply->finish (G_PASTE_CLIENT (source_object), result, &error);
    g_autoptr (AdwDialog) dialog = g_weak_ref_get (&reply->dialog);

    if (error)
        g_warning ("%s: %s", reply->message, error->message);

    if (dialog)
        g_paste_gtk_util_text_dialog_done (dialog, (error) ? reply->message : NULL);
}
