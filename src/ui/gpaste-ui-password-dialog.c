// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-error.h>
#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-ui-password-dialog.h>
#include <gpaste-ui-window.h>

/* What the dialog needs to answer: the call to make and the form to update
 * when it replies. @name, @password and @timeout are the rows themselves,
 * read at confirm time, @strength the row rating what @password holds; @confirm
 * is the button the two entries gate, and @submitted guards a pending call.
 *
 * @uuid is what says which call: an item to turn into a password, or %NULL for
 * a password being added from nothing -- which is the one shape carrying
 * @password, since that is the only thing the daemon cannot be asked for. */
typedef struct
{
    GPasteClient        *client;
    gchar               *uuid;
    GtkEditable         *name;
    GtkEditable         *password;
    GtkWidget           *strength;
    GtkAdjustment       *timeout;
    GtkWidget           *confirm;
    AdwPreferencesGroup *group;
    gboolean             submitted;
    gboolean             closed;
} PasswordDialogData;

static void
password_dialog_data_free (gpointer user_data)
{
    g_autofree PasswordDialogData *data = user_data;

    g_clear_object (&data->client);
    g_clear_pointer (&data->uuid, g_free);
}

/* Everything the form has to carry before it can be sent: a name, which is what
 * identifies a password, and on the add path a password to carry under it. An
 * empty one of either is what the daemon refuses outright, so the button saying
 * so is what stops the user filling the rest in for nothing. */
static gboolean
is_complete (PasswordDialogData *data)
{
    const gchar *name = gtk_editable_get_text (data->name);

    if (!name || !*name)
        return FALSE;

    if (!data->password)
        return TRUE;

    const gchar *password = gtk_editable_get_text (data->password);

    return password && *password;
}

static void
on_dialog_closed (AdwDialog *dialog G_GNUC_UNUSED,
                  gpointer   user_data)
{
    PasswordDialogData *data = user_data;

    data->closed = TRUE;
}

/* A pending call must not keep a cancelled dialog and its password alive. */
typedef struct
{
    GWeakRef             dialog;
    GPasteUiStringFinish finish;
} PasswordRequest;

static void
on_password_saved (GObject      *source_object,
                   GAsyncResult *result,
                   gpointer      user_data)
{
    g_autofree PasswordRequest *request = user_data;
    g_autoptr (GError) error = NULL;
    g_autofree gchar *uuid = request->finish (G_PASTE_CLIENT (source_object), result, &error);
    g_autoptr (AdwDialog) dialog = g_weak_ref_get (&request->dialog);

    g_weak_ref_clear (&request->dialog);

    if (!dialog)
        return;

    PasswordDialogData *data = g_object_get_data (G_OBJECT (dialog), "password-dialog-data");

    if (data->closed)
        return;

    if (uuid)
    {
        adw_dialog_close (dialog);
        return;
    }

    data->submitted = FALSE;
    gtk_widget_set_sensitive (GTK_WIDGET (data->group), TRUE);
    gtk_widget_set_sensitive (data->confirm, is_complete (data));

    gboolean conflict = g_error_matches (error, G_PASTE_ERROR, G_PASTE_ERROR_ALREADY_EXISTS);
    const gchar *message = conflict ? _("A password with this name already exists. Choose another name.")
                         : data->uuid ? _("Could not make the item a password")
                                      : _("Could not add the password");

    adw_preferences_group_set_description (data->group, message);
    if (conflict)
        gtk_widget_grab_focus (GTK_WIDGET (data->name));
    else if (error)
        g_warning ("%s: %s", message, error->message);
}

static void
on_confirm (GtkButton *button,
            gpointer   user_data)
{
    PasswordDialogData *data = user_data;

    if (data->submitted || data->closed || !is_complete (data))
        return;

    const gchar *name = gtk_editable_get_text (data->name);
    guint timeout = (guint) gtk_adjustment_get_value (data->timeout);
    AdwDialog *dialog = ADW_DIALOG (gtk_widget_get_ancestor (GTK_WIDGET (button), ADW_TYPE_DIALOG));
    PasswordRequest *request = g_new0 (PasswordRequest, 1);

    g_weak_ref_init (&request->dialog, dialog);
    request->finish = data->uuid ? g_paste_client_make_password_finish : g_paste_client_add_password_finish;

    data->submitted = TRUE;
    gtk_widget_set_sensitive (GTK_WIDGET (button), FALSE);
    gtk_widget_set_sensitive (GTK_WIDGET (data->group), FALSE);
    adw_preferences_group_set_description (data->group, NULL);

    /* The confirm button follows the entries, so neither of these can be empty
     * -- which is exactly what both calls refuse outright. */
    if (data->uuid)
    {
        g_paste_client_make_password (data->client, data->uuid, name, timeout,
                                      NULL /* cancellable */, on_password_saved, request);
    }
    else
    {
        g_paste_client_add_password (data->client, name, gtk_editable_get_text (data->password), timeout,
                                     NULL /* cancellable */, on_password_saved, request);
    }
}

/* Editing cannot enable a second submission while the daemon is answering. */
static void
on_form_changed (GtkEditable *entry G_GNUC_UNUSED,
                 gpointer     user_data)
{
    PasswordDialogData *data = user_data;

    if (data->submitted)
        return;

    gtk_widget_set_sensitive (data->confirm, is_complete (data));
}

/* The password gates the button as the name does, and is the one thing here
 * worth rating as it is typed. */
static void
on_password_changed (GtkEditable *entry,
                     gpointer     user_data)
{
    PasswordDialogData *data = user_data;

    g_paste_gtk_util_password_strength_row_rate (data->strength, entry);
    on_form_changed (entry, data);
}

/* The composer every entry point puts up: a name and a timeout, in the shell
 * g_paste_gtk_util_form_dialog () puts up, plus -- when there is no @uuid to
 * turn into a password -- the password itself and how it rates. */
static void
password_dialog (GPasteClient *client,
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

    PasswordDialogData *data = g_new0 (PasswordDialogData, 1);

    /* Only an add composes a password: what an existing item carries is already
     * on the clipboard, and a password item's own value is the one thing no
     * client can read back -- so there would be nothing to put in the entry and
     * nothing to do with what was typed there. */
    if (!uuid)
    {
        GtkWidget *password_row = adw_password_entry_row_new ();

        adw_preferences_row_set_title (ADW_PREFERENCES_ROW (password_row), _("Password"));
        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), password_row);

        data->password = GTK_EDITABLE (password_row);
        /* No @unavailable of its own: what a build without libpwquality has to
         * say is the row's, and the passphrase prompt is the only caller with a
         * reason to word it differently. */
        data->strength = g_paste_gtk_util_password_strength_row_new (_("Strength"), NULL);

        adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), data->strength);
    }

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), timeout_row);

    GtkWidget *page = adw_preferences_page_new ();

    adw_preferences_page_add (ADW_PREFERENCES_PAGE (page), ADW_PREFERENCES_GROUP (group));

    GtkWidget *confirm;
    AdwDialog *dialog = g_paste_gtk_util_form_dialog (heading, confirm_label, page, 450, 0, &confirm);

    data->client = g_object_ref (client);
    data->uuid = g_strdup (uuid);
    data->name = GTK_EDITABLE (name_row);
    data->timeout = adw_spin_row_get_adjustment (ADW_SPIN_ROW (timeout_row));
    data->confirm = confirm;
    data->group = ADW_PREFERENCES_GROUP (group);

    gtk_widget_set_sensitive (confirm, is_complete (data));

    /* Every handler below reads the whole of @data, so none of them is connected
     * before all of it is filled in. */
    if (data->password)
        g_signal_connect (data->password, "changed", G_CALLBACK (on_password_changed), data);
    g_signal_connect (name_row, "changed", G_CALLBACK (on_form_changed), data);
    g_signal_connect (confirm, "clicked", G_CALLBACK (on_confirm), data);
    g_signal_connect (dialog, "closed", G_CALLBACK (on_dialog_closed), data);

    /* The dialog outlives this function and the data outlives the dialog's
     * widgets, so it hangs off the dialog rather than off the confirm button:
     * cancelling frees it just as confirming does. */
    g_object_set_data_full (G_OBJECT (dialog), "password-dialog-data", data, password_dialog_data_free);

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
 * says "unknown" and no way to refuse what the user then confirms.
 *
 * And only when there is still a window to put it on: the daemon answers a read
 * whether or not the window that asked for it is still up, and a dialog
 * presented on one the user has closed is parented to nothing. A #GPasteClient
 * call cannot be taken back, so the reference held here is what keeps the window
 * addressable until the answers land, and its visibility is what says whether
 * they are still worth anything.
 *
 * FIXME: cancel the two reads instead, once #GPasteClient's methods take a
 * #GCancellable. */
static void
edit_data_reply (EditData *data)
{
    if (--data->pending)
        return;

    if (!data->failed && gtk_widget_get_visible (GTK_WIDGET (data->rootwin)))
        password_dialog (data->client, data->rootwin, data->uuid, _("Edit Password"), _("Save"), data->name, data->timeout);

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
 * g_paste_ui_password_dialog_add:
 * @client: a #GPasteClient
 * @settings: a #GPasteSettings
 * @rootwin: the root #GtkWindow
 *
 * Ask the user for a password, and add it
 *
 * The one entry point composing a password rather than naming one that is
 * already in the history, so it is also the only one with a password entry. The
 * timeout starts at whatever "password-timeout" says.
 */
void
g_paste_ui_password_dialog_add (GPasteClient   *client,
                                GPasteSettings *settings,
                                GtkWindow      *rootwin)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));
    g_return_if_fail (GTK_IS_WINDOW (rootwin));

    password_dialog (client, rootwin, NULL, _("New Password"), _("Add"), NULL,
                     (guint) g_paste_settings_get_password_timeout (settings));
}

/**
 * g_paste_ui_password_dialog_make:
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
g_paste_ui_password_dialog_make (GPasteClient   *client,
                                 GPasteSettings *settings,
                                 GtkWindow      *rootwin,
                                 const gchar    *uuid)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));
    g_return_if_fail (GTK_IS_WINDOW (rootwin));
    g_return_if_fail (uuid);

    password_dialog (client, rootwin, uuid, _("Make Password"), _("Make Password"), NULL,
                     (guint) g_paste_settings_get_password_timeout (settings));
}

/**
 * g_paste_ui_password_dialog_edit:
 * @client: a #GPasteClient
 * @rootwin: the root #GtkWindow
 * @uuid: the uuid of the password item to edit
 *
 * Read a password item's name and timeout back and offer them for editing
 *
 * The same dialog and the same call as g_paste_ui_password_dialog_make ():
 * editing a password is MakePassword handed the password's own uuid, which
 * updates it where it stands. Both reads it needs go out at once.
 */
void
g_paste_ui_password_dialog_edit (GPasteClient *client,
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

    g_paste_client_get_item (client, uuid, NULL /* cancellable */, on_item_ready, data);
    g_paste_client_get_password_timeout (client, uuid, NULL /* cancellable */, on_timeout_ready, data);
}
