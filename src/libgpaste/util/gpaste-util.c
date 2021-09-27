/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-gsettings-keys.h>
#include <gpaste-util.h>

#include "gpaste-gtk-compat.h"

/**
 * g_paste_util_confirm_dialog:
 * @parent: (nullable): the parent #GtkWindow
 * @msg: the message to display
 *
 * Show GPaste about dialog
 */
G_PASTE_VISIBLE gboolean
g_paste_util_confirm_dialog (GtkWindow   *parent,
                             const gchar *action,
                             const gchar *msg)
{
    g_return_val_if_fail (!parent || GTK_IS_WINDOW (parent), FALSE);
    g_return_val_if_fail (g_utf8_validate (msg, -1, NULL), FALSE);

    GtkWidget *dialog = gtk_dialog_new_with_buttons (PACKAGE_STRING, parent,
                                                     GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_USE_HEADER_BAR,
                                                     action,      GTK_RESPONSE_OK,
                                                     _("Cancel"), GTK_RESPONSE_CANCEL,
                                                     NULL);
    GtkWidget *label = gtk_label_new (msg);
    GtkDialog *d = GTK_DIALOG (dialog);

    gtk_widget_set_vexpand (label, TRUE);
    gtk_widget_set_valign (label, TRUE);
    gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (d)), label, TRUE, TRUE);
    gtk_widget_show (label);

    gboolean ret = gtk_dialog_run (d) == GTK_RESPONSE_OK;

    gtk_widget_destroy (dialog);

    return ret;
}

/* Copied from glib's gio/gapplication-tool.c */
static GVariant *
app_get_platform_data (void)
{
    g_auto (GVariantBuilder) builder;
    const gchar *startup_id;

    g_variant_builder_init (&builder, G_VARIANT_TYPE_VARDICT);

    if ((startup_id = g_getenv ("DESKTOP_STARTUP_ID")))
        g_variant_builder_add (&builder, "{sv}", "desktop-startup-id", g_variant_new_string (startup_id));

    return g_variant_builder_end (&builder);
}

static void
g_paste_util_spawn_on_proxy_ready (GObject      *source_object G_GNUC_UNUSED,
                                   GAsyncResult *res,
                                   gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_finish (res, NULL /* error */);

    if (proxy)
    {
        g_dbus_proxy_call (proxy,
                           "Activate",
                           g_variant_new ("(@a{sv})", app_get_platform_data ()),
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL, /* cancellable */
                           NULL, /* callback */
                           NULL); /* user_data */
    }
}

/**
 * g_paste_util_spawn:
 * @app: the GPaste app to spawn
 *
 * spawn a GPaste app
 */
G_PASTE_VISIBLE void
g_paste_util_spawn (const gchar *app)
{
    g_return_if_fail (g_utf8_validate (app, -1, NULL));

    g_autofree gchar *name = g_strdup_printf ("org.gnome.GPaste.%s", app);
    g_autofree gchar *object = g_strdup_printf ("/org/gnome/GPaste/%s", app);

    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              name,
                              object,
                              "org.freedesktop.Application",
                              NULL,
                              g_paste_util_spawn_on_proxy_ready,
                              NULL);
}

static GDBusProxy *
_bus_proxy_new_sync (const gchar *app,
                     GError     **error)
{
    g_autofree gchar *name = g_strdup_printf ("org.gnome.GPaste.%s", app);
    g_autofree gchar *object = g_strdup_printf ("/org/gnome/GPaste/%s", app);

    return g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          NULL,
                                          name,
                                          object,
                                          "org.freedesktop.Application",
                                          NULL,
                                          error);
}

static gboolean
_spawn_sync (GDBusProxy *proxy,
             GError    **error)
{
    G_GNUC_UNUSED g_autoptr (GVariant) res = g_dbus_proxy_call_sync (proxy,
                                                                     "Activate",
                                                                     g_variant_new ("(@a{sv})", app_get_platform_data ()),
                                                                     G_DBUS_CALL_FLAGS_NONE,
                                                                     -1,
                                                                     NULL,
                                                                     error);

    return !error || !(*error);
}

/**
 * g_paste_util_spawn_sync:
 * @app: the GPaste app to spawn
 * @error: a #GError or %NULL
 *
 * spawn a GPaste app
 *
 * Returns: whether the spawn was successful
 */
G_PASTE_VISIBLE gboolean
g_paste_util_spawn_sync (const gchar *app,
                         GError     **error)
{
    g_return_val_if_fail (g_utf8_validate (app, -1, NULL), FALSE);
    g_return_val_if_fail (!error || !(*error), FALSE);

    g_autoptr (GDBusProxy) proxy = _bus_proxy_new_sync (app, error);

    if (!proxy)
        return FALSE;

    return _spawn_sync (proxy, error);
}

static void
g_paste_util_activate_ui_on_proxy_ready (GObject      *source_object G_GNUC_UNUSED,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
    g_autofree gpointer *data = (gpointer *) user_data;
    g_autofree gchar *action = data[0];
    GVariant *arg = data[1];
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_finish (res, NULL /* error */);

    if (proxy)
    {
        g_auto (GVariantBuilder) params;

        g_variant_builder_init (&params, G_VARIANT_TYPE ("av"));

        if (arg)
            g_variant_builder_add (&params, "v", arg);

        g_dbus_proxy_call (proxy,
                           "ActivateAction",
                           g_variant_new ("(sav@a{sv})", action, &params, app_get_platform_data ()),
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,
                           NULL, /* cancellable */
                           NULL, /* callback */
                           NULL); /* user_data */
    }
}

/**
 * g_paste_util_activate_ui:
 * @action: the action to activate
 * @arg: (nullable): the action argument
 *
 * Activate an action on a GPaste app
 */
G_PASTE_VISIBLE void
g_paste_util_activate_ui (const gchar *action,
                          GVariant    *arg)
{
    g_return_if_fail (g_utf8_validate (action, -1, NULL));

    gpointer *data = g_new (gpointer, 2);
    data[0] = g_strdup (action);
    data[1] = arg;

    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              "org.gnome.GPaste.Ui",
                              "/org/gnome/GPaste/Ui",
                              "org.freedesktop.Application",
                              NULL,
                              g_paste_util_activate_ui_on_proxy_ready,
                              data);
}

/**
 * g_paste_util_activate_ui_sync:
 * @action: the action to activate
 * @arg: (nullable): the action argument
 * @error: a #GError or %NULL
 *
 * activate an action from GPaste Ui
 *
 * Returns: whether the action was successful
 */
G_PASTE_VISIBLE gboolean
g_paste_util_activate_ui_sync (const gchar *action,
                               GVariant    *arg,
                               GError     **error)
{
    g_return_val_if_fail (g_utf8_validate (action, -1, NULL), FALSE);
    g_return_val_if_fail (!error || !(*error), FALSE);

    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                                  NULL,
                                                                  "org.gnome.GPaste.Ui",
                                                                  "/org/gnome/GPaste/Ui",
                                                                  "org.freedesktop.Application",
                                                                  NULL,
                                                                  error);

    if (!proxy)
        return FALSE;

    g_auto (GVariantBuilder) params;

    g_variant_builder_init (&params, G_VARIANT_TYPE ("av"));

    if (arg)
        g_variant_builder_add (&params, "v", arg);

    /* We only consume it */
    G_GNUC_UNUSED g_autoptr (GVariant) res = g_dbus_proxy_call_sync (proxy,
                                                                     "ActivateAction",
                                                                     g_variant_new ("(sav@a{sv})",
                                                                                    action,
                                                                                    &params,
                                                                                    app_get_platform_data ()),
                                                                     G_DBUS_CALL_FLAGS_NONE,
                                                                     -1,
                                                                     NULL, /* cancellable */
                                                                     error);

    return TRUE;
}

/**
 * g_paste_util_empty_with_confirmation:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @history: the name of the history to empty
 *
 * Empty a history after confirmation.
 * Confirmation is skipped if GPaste is configured to do so.
 */
G_PASTE_VISIBLE void
g_paste_util_empty_with_confirmation (GPasteClient         *client,
                                      const GPasteSettings *settings,
                                      const gchar          *history)
{
    g_return_if_fail (_G_PASTE_IS_CLIENT (client));
    g_return_if_fail (_G_PASTE_IS_SETTINGS (settings));
    g_return_if_fail (g_utf8_validate (history, -1, NULL));

    if (g_paste_settings_get_empty_history_confirmation (settings))
        g_paste_util_activate_ui ("empty", g_variant_new_string (history));
    else
        g_paste_client_empty_history (client, history, NULL, NULL);
}

/**
 * g_paste_util_empty_with_confirmation_sync:
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @history: the name of the history to empty
 * @error: a #GError or %NULL
 *
 * Empty a history after confirmation.
 * Confirmation is skipped if GPaste is configured to do so.
 *
 * Returns: whether the action was successful
 */
G_PASTE_VISIBLE gboolean
g_paste_util_empty_with_confirmation_sync (GPasteClient         *client,
                                           const GPasteSettings *settings,
                                           const gchar          *history,
                                           GError              **error)
{
    g_return_val_if_fail (_G_PASTE_IS_CLIENT (client), FALSE);
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), FALSE);
    g_return_val_if_fail (g_utf8_validate (history, -1, NULL), FALSE);
    g_return_val_if_fail (!error || !(*error), FALSE);

    if (g_paste_settings_get_empty_history_confirmation (settings))
    {
        return g_paste_util_activate_ui_sync ("empty", g_variant_new_string (history), error);
    }
    else
    {
        g_autoptr (GError) _error = NULL;

        g_paste_client_empty_history_sync (client, history, &_error);

        if (error)
        {
            *error = _error;
            _error = NULL;
        }

        return !_error && !(error && *error);
    }
}

/**
 * g_paste_util_relace:
 * @text: the initial text
 * @pattern: the pattern to replace
 * @substitution: the replacement text
 *
 * Replace some text
 *
 * Returns: the newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_util_replace (const gchar *text,
                      const gchar *pattern,
                      const gchar *substitution)
{
    g_return_val_if_fail (g_utf8_validate (text, -1, NULL), NULL);
    g_return_val_if_fail (g_utf8_validate (pattern, -1, NULL), NULL);
    g_return_val_if_fail (g_utf8_validate (substitution, -1, NULL), NULL);

    g_autofree gchar *regex_string = g_regex_escape_string (pattern, -1);
    g_autoptr (GRegex) regex = g_regex_new (regex_string,
                                            0, /* Compile options */
                                            0, /* Match options */
                                            NULL); /* Error */
    return g_regex_replace_literal (regex,
                                    text,
                                    (gssize) -1,
                                    0, /* Start position */
                                    substitution,
                                    0, /* Match options */
                                    NULL); /* Error */
}

/**
 * g_paste_util_compute_checksum:
 * @image: the #GdkPixbuf to checksum
 *
 * Compute the checksum of an image
 *
 * Returns: the newly allocated checksum
 */
G_PASTE_VISIBLE gchar *
g_paste_util_compute_checksum (GdkPixbuf *image)
{
    if (!image || !GDK_IS_PIXBUF (image))
        return NULL;

    const guint8 *data = gdk_pixbuf_read_pixels (image);
    gsize length = gdk_pixbuf_get_byte_length (image);

    return g_compute_checksum_for_data (G_CHECKSUM_SHA256, data, length);
}

/**
 * g_paste_util_empty_history:
 * @parent_window: (nullable): the parent #GtkWindow
 * @client: a #GPasteClient instance
 * @settings: a #GPasteSettings instance
 * @history: the name of the history to empty
 *
 * Empty history after prompting user for confirmation
 */
G_PASTE_VISIBLE void
g_paste_util_empty_history (GtkWindow      *parent_window,
                            GPasteClient   *client,
                            GPasteSettings *settings,
                            const gchar    *history)
{
    if (!g_paste_settings_get_empty_history_confirmation (settings) ||
        /* Translators: this is the translation for emptying the history */
        g_paste_util_confirm_dialog (parent_window, _("Empty"), _("Do you really want to empty the history?")))
            g_paste_client_empty_history (client, history, NULL, NULL);
}

/**
 * g_paste_util_has_gnome_shell:
 *
 * Check whether gnome-shell is installed or not
 *
 * Returns: %TRUE if gnome-shell is installed
 */
G_PASTE_VISIBLE gboolean
g_paste_util_has_gnome_shell (void)
{
    GSettingsSchemaSource *source = g_settings_schema_source_get_default ();

    if (!source)
        return FALSE;

    g_autoptr (GSettingsSchema) schema = g_settings_schema_source_lookup (source, G_PASTE_SHELL_SETTINGS_NAME, TRUE);

    return !!schema;
}

/**
 * g_paste_util_show_win:
 * @application: a #GtkApplication
 *
 * Present the application's window to user
 */
G_PASTE_VISIBLE void
g_paste_util_show_win (GApplication *application)
{
    g_return_if_fail (GTK_IS_APPLICATION (application));

    for (GList *wins = gtk_application_get_windows (GTK_APPLICATION (application)); wins; wins = g_list_next (wins))
    {
        if (GTK_IS_WIDGET (wins->data) && gtk_widget_get_realized (wins->data))
            gtk_window_present (wins->data);
    }
}

/**
 * g_paste_util_get_au_result:
 * @variant: a #GVariant
 * @len: the length of the resulting array
 *
 * Get the "au" GVariant as an array of guint32
 *
 * Returns: (array length=len): The resulting array
 */
G_PASTE_VISIBLE guint32 *
g_paste_util_get_dbus_au_result (GVariant *variant,
                                 guint64  *len)
{
    guint64 _len;
    const guint32 *r = g_variant_get_fixed_array (variant, &_len, sizeof (guint32));
    guint32 *ret = g_memdup2 (r, _len * sizeof (guint32));

    if (len)
        *len = _len;

    return ret;
}

/**
 * g_paste_util_get_dbus_item_result:
 * @variant: a #GVariant
 *
 * Get the "(ss)" GVariant as an item
 *
 * Returns: (transfer full): The item
 */
G_PASTE_VISIBLE GPasteClientItem *
g_paste_util_get_dbus_item_result (GVariant *variant)
{
    const gchar *uuid, *value;

    g_variant_get (variant, "(ss)", &uuid, &value);

    return g_paste_client_item_new (uuid, value);
}

/**
 * g_paste_util_get_dbus_items_result:
 * @variant: a #GVariant
 *
 * Get the "a(ss)" GVariant as a list of items
 *
 * Returns: (element-type GPasteClientItem) (transfer full): The items
 */
G_PASTE_VISIBLE GList *
g_paste_util_get_dbus_items_result (GVariant *variant)
{
    GList *items = NULL;
    GVariantIter iter;
    g_autoptr (GVariant) v = NULL;

    g_variant_iter_init (&iter, variant);
    while ((v = g_variant_iter_next_value (&iter)))
    {
        items = g_list_append (items, g_paste_util_get_dbus_item_result (v));
        g_variant_unref (v);
    }

    return items;
}

static gchar *
g_paste_util_get_runtime_dir (const gchar *component)
{
    g_return_val_if_fail (component, NULL);

    return g_strdup_printf ("%s/" PACKAGE_NAME "/%s", g_get_user_runtime_dir (), component);
}

/**
 * g_paste_util_write_pid_file:
 * @component: The component we're handling
 *
 * Write the pid file
 */
G_PASTE_VISIBLE void
g_paste_util_write_pid_file (const gchar *component)
{
    g_return_if_fail (component);

    g_autofree gchar *dir = g_paste_util_get_runtime_dir (component);

    g_mkdir_with_parents (dir, 0700);

    g_autofree gchar *pidfile = g_strdup_printf ("%s/pid", dir);
    g_autofree gchar *contents = g_strdup_printf ("%" G_PID_FORMAT, getpid ());

    g_file_set_contents (pidfile, contents, -1, NULL);
}

/**
 * g_paste_util_read_pid_file:
 * @component: The component we're handling
 *
 * Read the pid file
 *
 * Returns: the pid
 */
G_PASTE_VISIBLE GPid
g_paste_util_read_pid_file (const gchar *component)
{
    g_return_val_if_fail (component, (GPid) -1);

#ifdef G_OS_UNIX
    g_autofree gchar *dir = g_paste_util_get_runtime_dir (component);
    g_autofree gchar *pidfile = g_strdup_printf ("%s/pid", dir);
    g_autofree gchar *contents = NULL;

    if (!g_file_get_contents (pidfile, &contents, NULL, NULL))
        return (GPid) -1;

    return (GPid) g_ascii_strtoll (contents, NULL, 0);
#else
    return (GPid) -1;
#endif
}

/**
 * g_paste_util_xml_decode:
 * @text: The text to decode
 *
 * Decode the text to its original pre-xml form
 *
 * Returns: the decoded text
 */
G_PASTE_VISIBLE gchar *
g_paste_util_xml_decode (const gchar *text)
{
    g_return_val_if_fail (text, NULL);

    g_autofree gchar *_decoded_text = g_paste_util_replace (text, "&gt;", ">");

    return g_paste_util_replace (_decoded_text, "&amp;", "&");
}

/**
 * g_paste_util_xml_encode:
 * @text: The text to encode
 *
 * Encode the text into its xml form
 *
 * Returns: the encoded text
 */
G_PASTE_VISIBLE gchar *
g_paste_util_xml_encode (const gchar *text)
{
    g_return_val_if_fail (text, NULL);

    g_autofree gchar *_encoded_text = g_paste_util_replace (text, "&", "&amp;");

    return g_paste_util_replace (_encoded_text, ">", "&gt;");
}

/**
 * g_paste_util_get_history_dir_path:
 *
 * Get the path to the directory where we store the history
 *
 * Returns: the directory path
 */
G_PASTE_VISIBLE gchar *
g_paste_util_get_history_dir_path (void)
{
    return g_build_filename (g_get_user_data_dir (), PACKAGE, NULL);
}

/**
 * g_paste_util_get_history_dir:
 *
 * Get the directory where we store the history
 *
 * Returns: (transfer full): the directory
 */
G_PASTE_VISIBLE GFile *
g_paste_util_get_history_dir (void)
{
    g_autofree gchar *history_dir_path = g_paste_util_get_history_dir_path ();

    return g_file_new_for_path (history_dir_path);
}

/**
 * g_paste_util_get_history_file_path:
 * @name: the name of the history
 * @extension: the file extension
 *
 * Get the path to the file in which we store the history
 *
 * Returns: the file path
 */
G_PASTE_VISIBLE gchar *
g_paste_util_get_history_file_path (const gchar *name,
                                    const gchar *extension)
{
    g_return_val_if_fail (name, NULL);
    g_return_val_if_fail (extension, NULL);

    g_autofree gchar *history_dir_path = g_paste_util_get_history_dir_path ();
    g_autofree gchar *history_file_name = g_strconcat (name, ".", extension, NULL);

    return g_build_filename (history_dir_path, history_file_name, NULL);
}

/**
 * g_paste_util_get_history_file:
 * @name: the name of the history
 * @extension: the file extension
 *
 * Get the file in which we store the history
 *
 * Returns: (transfer full): the file
 */
G_PASTE_VISIBLE GFile *
g_paste_util_get_history_file (const gchar *name,
                               const gchar *extension)
{
    g_return_val_if_fail (name, NULL);
    g_return_val_if_fail (extension, NULL);

    g_autofree gchar *history_file_path = g_paste_util_get_history_file_path (name, extension);

    return g_file_new_for_path (history_file_path);
}

/**
 * g_paste_util_ensure_history_dir_exists:
 * @settings: a #GPasteSettings instance
 *
 * Ensure the history dir exists
 *
 * Returns: where it exists or if there was an error creating it
 */
G_PASTE_VISIBLE gboolean
g_paste_util_ensure_history_dir_exists (const GPasteSettings *settings)
{
    g_return_val_if_fail (_G_PASTE_IS_SETTINGS (settings), FALSE);

    g_autoptr (GFile) history_dir = g_paste_util_get_history_dir ();

    if (!g_file_query_exists (history_dir,
                              NULL)) /* cancellable */
    {
        if (!g_paste_settings_get_save_history (settings))
            return TRUE;

        g_autoptr (GError) error = NULL;

        g_file_make_directory_with_parents (history_dir,
                                            NULL, /* cancellable */
                                            &error);
        if (error)
        {
            g_critical ("%s: %s", _("Could not create history dir"), error->message);
            return FALSE;
        }
    }

    return TRUE;
}
