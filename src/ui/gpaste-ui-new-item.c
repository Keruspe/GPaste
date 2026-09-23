// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-new-item.h>
#include <gpaste-ui-window.h>

/* The window is held weakly, as in gpaste-ui-edit-item.c's EditData and for the
 * same reason: a reference kept for as long as the dialog is up would keep a
 * window the user closed from ever being disposed. */
typedef struct
{
    GPasteClient *client;
    GWeakRef      rootwin;
} NewItemData;

static void
new_item_data_free (NewItemData *data)
{
    g_clear_object (&data->client);
    g_weak_ref_clear (&data->rootwin);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (NewItemData, new_item_data_free)

static void
on_new_item (const gchar *text,
             gpointer     user_data)
{
    g_autoptr (NewItemData) data = user_data;
    g_autoptr (GtkWindow) rootwin = g_weak_ref_get (&data->rootwin);

    /* %NULL means cancelled, empty means nothing was written; a window gone took
     * the dialog with it, which answers as a cancellation. */
    if (!text || !*text || !rootwin)
        return;

    g_paste_client_add_text (data->client, text,
                             NULL /* cancellable */,
                             g_paste_ui_report_string_cb,
                             g_paste_ui_report_string (GTK_WIDGET (rootwin), g_paste_client_add_text_finish,
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

    NewItemData *data = g_new0 (NewItemData, 1);

    data->client = g_object_ref (client);
    g_weak_ref_init (&data->rootwin, rootwin);

    g_paste_gtk_util_text_dialog (rootwin, _("Add New Item"), _("Add"), NULL, on_new_item, data);
}
