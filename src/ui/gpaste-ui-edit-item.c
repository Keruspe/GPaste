// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-edit-item.h>
#include <gpaste-ui-window.h>

/* One edit's state, from the read of the item to the answer of its dialog.
 *
 * As in g_paste_ui_password_dialog_edit (), the read goes out on a cancellable
 * the window cancels when it goes, and the window is held weakly. That holds for
 * the dialog's stretch too: the dialog lives in the window, and a reference kept
 * for as long as it is up would keep a window the user closed from ever being
 * disposed, the dialog and the text being edited with it
 * (/ui/edit_item/closed_with_dialog_up). The dialog always answers, a window gone
 * under it included (g_paste_gtk_util_text_dialog ()), which is what frees this. */
typedef struct
{
    GPasteClient *client;
    gchar        *uuid;
    GWeakRef      rootwin;
    GCancellable *cancellable;
} EditData;

static void
edit_data_free (EditData *data)
{
    g_clear_object (&data->client);
    g_free (data->uuid);
    g_weak_ref_clear (&data->rootwin);
    g_clear_object (&data->cancellable);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EditData, edit_data_free)

static void
on_edit_response (const gchar *text,
                  gpointer     user_data)
{
    g_autoptr (EditData) data = user_data;
    g_autoptr (GtkWindow) rootwin = g_weak_ref_get (&data->rootwin);

    /* %NULL means cancelled, empty means the item was emptied -- which is not
     * an edit but a deletion, and is not what Save was asked for. A window gone
     * took the dialog with it, which answers as a cancellation. */
    if (!text || !*text || !rootwin)
        return;

    g_paste_client_replace (data->client, data->uuid, text,
                            NULL /* cancellable */,
                            g_paste_ui_report_string_cb,
                            g_paste_ui_report_string (GTK_WIDGET (rootwin), g_paste_client_replace_finish,
                                                      _("Could not save the edited item")));
}

static void
on_item_ready (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
    g_autoptr (EditData) data = user_data;
    g_autoptr (GtkWindow) rootwin = g_weak_ref_get (&data->rootwin);
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClientItem) item = g_paste_client_get_item_finish (G_PASTE_CLIENT (source_object), res, &error);

    /* As in the password edit's replies. */
    if (!rootwin || g_cancellable_is_cancelled (data->cancellable))
        return;

    /* Without it there is no dialog to show, and the Edit the user asked for
     * would simply not happen. */
    if (!item)
    {
        /* A reply that parsed into no item sets no error: the uuid or the value
         * did not survive validation, which a daemon of another version can
         * well produce. */
        g_warning ("Could not read the item to edit: %s", (error) ? error->message : "the daemon answered with no usable item");

        g_paste_gtk_util_toast (GTK_WIDGET (rootwin), _("Could not read the item to edit"));

        return;
    }

    g_paste_gtk_util_text_dialog (rootwin, _("Edit Item"), _("Save"), g_paste_client_item_get_value (item), on_edit_response, g_steal_pointer (&data));
}

/**
 * g_paste_ui_edit_item_show:
 * @client: a #GPasteClient
 * @rootwin: the root window, one g_paste_gtk_util_can_host_dialog () accepts
 * @uuid: the uuid of the item to edit
 *
 * Read an item back and offer its text for editing
 */
void
g_paste_ui_edit_item_show (GPasteClient *client,
                           GtkWindow    *rootwin,
                           const gchar  *uuid)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    /* Checked here and not left to the dialog, which only comes after the read:
     * a window it would refuse is the caller's mistake, and reported as it is
     * made. */
    g_return_if_fail (g_paste_gtk_util_can_host_dialog (rootwin));
    g_return_if_fail (uuid);

    EditData *data = g_new0 (EditData, 1);

    data->client = g_object_ref (client);
    data->uuid = g_strdup (uuid);
    g_weak_ref_init (&data->rootwin, rootwin);
    data->cancellable = g_cancellable_new ();

    /* Unrealize as in g_paste_ui_password_dialog_edit (). The handlers go with
     * the cancellable, which this edit's state holds until the dialog answers:
     * past the read, while the dialog is up, they cancel nothing anything still
     * waits on. */
    g_signal_connect_object (rootwin, "destroy", G_CALLBACK (g_cancellable_cancel), data->cancellable, G_CONNECT_SWAPPED);
    g_signal_connect_object (rootwin, "unrealize", G_CALLBACK (g_cancellable_cancel), data->cancellable, G_CONNECT_SWAPPED);

    /* The plain getter: Edit is only offered for a text item, whose display
     * string is its value. */
    g_paste_client_get_item (client, uuid, data->cancellable, on_item_ready, data);
}
