// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-make-password.h>
#include <gpaste-ui-window.h>

/* What the dialog needs to answer: the call to make, and the window to report
 * through once it has. @name and @timeout are the rows themselves, read at
 * confirm time; @confirm is the button they gate, and @submitted is whether it
 * has already been pressed. */
typedef struct
{
    GPasteClient  *client;
    GtkWindow     *rootwin;
    gchar         *uuid;
    GtkEditable   *name;
    GtkAdjustment *timeout;
    GtkWidget     *confirm;
    gboolean       submitted;
} MakePasswordData;

static void
make_password_data_free (gpointer user_data)
{
    g_autofree MakePasswordData *data = user_data;

    g_clear_object (&data->client);
    g_clear_object (&data->rootwin);
    g_clear_pointer (&data->uuid, g_free);
}

static void
on_confirm (GtkButton *button,
            gpointer   user_data)
{
    MakePasswordData *data = user_data;
    const gchar *name = gtk_editable_get_text (data->name);

    /* Closing is asynchronous, so the button is still live until it happens: a
     * second press would send a second MakePassword, that one against a uuid the
     * first has already replaced. Recorded rather than only desensitized, since
     * the entry is live too and what follows it re-sensitizes from the text
     * alone. */
    data->submitted = TRUE;
    gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);

    /* The confirm button follows the entry, so this cannot be empty -- and an
     * empty name is what MakePassword refuses outright. */
    g_paste_client_make_password (data->client, data->uuid, name,
                                  (guint) gtk_adjustment_get_value (data->timeout),
                                  g_paste_ui_report_string_cb,
                                  g_paste_ui_report_string (GTK_WIDGET (data->rootwin), g_paste_client_make_password_finish,
                                                            _("Could not make the item a password")));

    adw_dialog_close (ADW_DIALOG (gtk_widget_get_ancestor (GTK_WIDGET (button), ADW_TYPE_DIALOG)));
}

/* A name is what identifies a password, so there is nothing to confirm without
 * one. Kept in step with the entry rather than checked on confirm: the button
 * saying so is what stops the user filling the rest in for nothing. Once
 * confirmed there is nothing left to gate, and anything still reaching the entry
 * while the dialog closes must not hand the button back. */
static void
on_name_changed (GtkEditable *entry,
                 gpointer     user_data)
{
    MakePasswordData *data = user_data;
    const gchar *name = gtk_editable_get_text (entry);

    if (data->submitted)
        return;

    gtk_widget_set_sensitive (data->confirm, name && *name);
}

/* The composer both entry points put up: a name and a timeout, in the shell
 * g_paste_gtk_util_form_dialog () puts up. */
static void
make_password_dialog (GPasteClient *client,
                      GtkWindow    *rootwin,
                      const gchar  *uuid,
                      const gchar  *heading,
                      const gchar  *confirm_label,
                      const gchar  *name,
                      guint         timeout)
{
    GtkWidget *name_row = adw_entry_row_new ();
    /* The range a password timeout has, which the "password-timeout" key and the
     * preferences row offer the same way: a value this refused would be one no
     * other surface can produce. */
    GtkWidget *timeout_row = adw_spin_row_new_with_range (0, (gdouble) G_PASTE_PASSWORD_TIMEOUT_MAX, 5);

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (name_row), _("Name"));
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (timeout_row), _("Clear After (seconds)"));
    adw_action_row_set_subtitle (ADW_ACTION_ROW (timeout_row),
                                 _("0 lets the password stay on the clipboard for as long as anything else would"));

    if (name)
        gtk_editable_set_text (GTK_EDITABLE (name_row), name);
    adw_spin_row_set_value (ADW_SPIN_ROW (timeout_row), timeout);

    GtkWidget *group = adw_preferences_group_new ();

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), name_row);
    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), timeout_row);

    GtkWidget *page = adw_preferences_page_new ();

    adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));

    GtkWidget *confirm;
    AdwDialog *dialog = g_paste_gtk_util_form_dialog (heading, confirm_label, page, 450, 0, &confirm);

    gtk_widget_set_sensitive (confirm, name && *name);

    MakePasswordData *data = g_new0 (MakePasswordData, 1);

    data->client = g_object_ref (client);
    data->rootwin = g_object_ref (rootwin);
    data->uuid = g_strdup (uuid);
    data->name = GTK_EDITABLE (name_row);
    data->timeout = adw_spin_row_get_adjustment (ADW_SPIN_ROW (timeout_row));
    data->confirm = confirm;

    g_signal_connect (name_row, "changed", G_CALLBACK (on_name_changed), data);
    g_signal_connect (confirm, "clicked", G_CALLBACK (on_confirm), data);

    /* The dialog outlives this function and the data outlives the dialog's
     * widgets, so it hangs off the dialog rather than off the confirm button:
     * cancelling frees it just as confirming does. */
    g_object_set_data_full (G_OBJECT (dialog), "make-password-data", data, make_password_data_free);

    adw_dialog_present (dialog, GTK_WIDGET (rootwin));
    gtk_widget_grab_focus (name_row);
}

/* The two reads an edit needs -- the password's name and how long it may stay on
 * the clipboard -- issued together rather than one out of the other's reply: the
 * daemon holds both, and neither answer depends on the other. @pending is what
 * tells the second reply from the first, only the last one having a whole dialog
 * to put up. */
typedef struct
{
    GPasteClient *client;
    GtkWindow    *rootwin;
    gchar        *uuid;
    gchar        *name;
    guint         timeout;
    guint         pending;
    gboolean      failed;
} EditData;

/* The dialog goes up on the last reply, and only when both came back: a timeout
 * that could not be read is not a timeout of 0, and 0 is what Save would write
 * over the one the password actually carries -- the dialog having no state that
 * says "unknown" and no way to refuse what the user then confirms. */
static void
edit_data_reply (EditData *data)
{
    if (--data->pending)
        return;

    if (!data->failed)
        make_password_dialog (data->client, data->rootwin, data->uuid, _("Edit Password"), _("Save"), data->name, data->timeout);

    g_clear_object (&data->client);
    g_clear_object (&data->rootwin);
    g_clear_pointer (&data->uuid, g_free);
    g_clear_pointer (&data->name, g_free);
    g_free (data);
}

static void
on_timeout_ready (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      user_data)
{
    EditData *data = user_data;
    g_autoptr (GError) error = NULL;
    guint timeout = g_paste_client_get_password_timeout_finish (G_PASTE_CLIENT (source_object), res, &error);

    if (error)
    {
        g_warning ("Could not read the password's timeout: %s", error->message);
        g_paste_gtk_util_toast (GTK_WIDGET (data->rootwin), _("Could not read the password's timeout"));
        data->failed = TRUE;
    }
    else
        data->timeout = timeout;

    edit_data_reply (data);
}

static void
on_item_ready (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
    EditData *data = user_data;
    g_autoptr (GError) error = NULL;
    g_autoptr (GPasteClientItem) item = g_paste_client_get_item_finish (G_PASTE_CLIENT (source_object), res, &error);

    if (!item)
    {
        /* As in g_paste_ui_edit_item_show ()'s own reader, a reply that parsed
         * into no item sets no error. */
        g_warning ("Could not read the password to edit: %s", (error) ? error->message : "the daemon answered with no usable item");
        g_paste_gtk_util_toast (GTK_WIDGET (data->rootwin), _("Could not read the password to edit"));
        data->failed = TRUE;
    }
    else
    {
        /* A password item's value is its name: that is the one string it travels
         * with, the daemon never putting the password itself on the wire. */
        data->name = g_strdup (g_paste_client_item_get_value (item));
    }

    edit_data_reply (data);
}

/**
 * g_paste_ui_make_password_show:
 * @client: a #GPasteClient
 * @settings: a #GPasteSettings
 * @rootwin: the root #GtkWindow
 * @uuid: the uuid of the text item to turn into a password
 *
 * Offer to turn a text item into a password item
 *
 * Nothing has to be read back: a text item carries no name, and the timeout
 * starts at whatever "password-timeout" says.
 */
void
g_paste_ui_make_password_show (GPasteClient   *client,
                               GPasteSettings *settings,
                               GtkWindow      *rootwin,
                               const gchar    *uuid)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));
    g_return_if_fail (GTK_IS_WINDOW (rootwin));
    g_return_if_fail (uuid);

    make_password_dialog (client, rootwin, uuid, _("Make Password"), _("Make Password"), NULL,
                          (guint) g_paste_settings_get_password_timeout (settings));
}

/**
 * g_paste_ui_make_password_edit:
 * @client: a #GPasteClient
 * @rootwin: the root #GtkWindow
 * @uuid: the uuid of the password item to edit
 *
 * Read a password item's name and timeout back and offer them for editing
 *
 * The same dialog and the same call as g_paste_ui_make_password_show (): editing
 * a password is MakePassword handed the password's own uuid, which updates it
 * where it stands. Both reads it needs go out at once.
 */
void
g_paste_ui_make_password_edit (GPasteClient *client,
                               GtkWindow    *rootwin,
                               const gchar  *uuid)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (GTK_IS_WINDOW (rootwin));
    g_return_if_fail (uuid);

    EditData *data = g_new0 (EditData, 1);

    data->client = g_object_ref (client);
    data->rootwin = g_object_ref (rootwin);
    data->uuid = g_strdup (uuid);
    data->pending = 2;

    g_paste_client_get_item (client, uuid, on_item_ready, data);
    g_paste_client_get_password_timeout (client, uuid, on_timeout_ready, data);
}
