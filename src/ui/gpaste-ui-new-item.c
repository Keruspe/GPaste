// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-new-item.h>
#include <gpaste-ui-window.h>

typedef struct
{
    GPasteClient *client;
    GtkWindow    *rootwin;
} NewItemData;

static void
on_new_item (const gchar *text,
             gpointer     user_data)
{
    g_autofree NewItemData *data = user_data;
    g_autoptr (GPasteClient) client = data->client;
    g_autoptr (GtkWindow) rootwin = data->rootwin;

    /* %NULL means cancelled, empty means nothing was written. */
    if (!text || !*text)
        return;

    g_paste_client_add_text (client, text,
                             g_paste_ui_report_string_cb,
                             g_paste_ui_report_string (GTK_WIDGET (rootwin), g_paste_client_add_text_finish,
                                                       _("Could not add the item")));
}

/**
 * g_paste_ui_new_item_show:
 * @client: a #GPasteClient
 * @rootwin: the root #GtkWindow
 *
 * Ask the user for the text of a new item, and add it
 */
void
g_paste_ui_new_item_show (GPasteClient *client,
                          GtkWindow    *rootwin)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (GTK_IS_WINDOW (rootwin));

    NewItemData *data = g_new (NewItemData, 1);

    data->client = g_object_ref (client);
    data->rootwin = g_object_ref (rootwin);

    g_paste_gtk_util_text_dialog (rootwin, _("Add New Item"), _("Add"), NULL, on_new_item, data);
}
