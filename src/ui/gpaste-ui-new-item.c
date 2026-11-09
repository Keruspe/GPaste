// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-composer.h>
#include <gpaste-ui-new-item.h>

/* @user_data is the client, the dialog's own reference to it: a reference to
 * the window kept for as long as the dialog is up would keep a window the user
 * closed from ever being disposed. */
static void
on_new_item (AdwDialog   *dialog,
             const gchar *text,
             gpointer     user_data)
{
    g_paste_client_add_text (user_data, text,
                             NULL /* cancellable */,
                             g_paste_ui_composer_reply_cb,
                             g_paste_ui_composer_reply (dialog, g_paste_client_add_text_finish,
                                                        _("Could not add the item")));
}

/**
 * g_paste_ui_new_item_show:
 * @client: a #GPasteClient
 * @rootwin: the root window, one g_paste_gtk_util_can_host_dialog () accepts
 *
 * Ask the user for the text of a new item, and add it
 */
void
g_paste_ui_new_item_show (GPasteClient *client,
                          GtkWindow    *rootwin)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (g_paste_gtk_util_can_host_dialog (rootwin));

    g_paste_gtk_util_text_dialog (rootwin, _("Add New Item"), _("Add"), NULL, on_new_item, g_object_ref (client), g_object_unref);
}
