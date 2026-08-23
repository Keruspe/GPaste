// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-client-history.h>
#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-util.h>

#include <string.h>

/* Every GPaste app is reached the same way: the standard interface a desktop
 * application exports, on the bus name and object path its own name makes. The
 * graphical tool is the one addressed by a caller that is not spawning it, so
 * it is spelled out rather than built. */
#define G_PASTE_APPLICATION_IFACE "org.freedesktop.Application"
#define G_PASTE_UI_BUS_NAME       "org.gnome.GPaste.Ui"
#define G_PASTE_UI_OBJECT_PATH    "/org/gnome/GPaste/Ui"

/* Copied from glib's gio/gapplication-tool.c */
static GVariant *
app_get_platform_data (void)
{
    g_auto (GVariantBuilder) builder;
    const gchar *startup_id;

    g_variant_builder_init_static (&builder, G_VARIANT_TYPE_VARDICT);

    if ((startup_id = g_getenv ("DESKTOP_STARTUP_ID")))
        g_variant_builder_add (&builder, "{sv}", "desktop-startup-id", g_variant_new_string (startup_id));

    return g_variant_builder_end (&builder);
}

static void
g_paste_util_spawn_on_proxy_ready (GObject      *source_object G_GNUC_UNUSED,
                                   GAsyncResult *res,
                                   gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

    if (!proxy)
    {
        g_warning ("Failed to get D-Bus proxy: %s", error->message);
        return;
    }

    g_dbus_proxy_call (proxy,
                       "Activate",
                       g_variant_new ("(@a{sv})", app_get_platform_data ()),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL, /* cancellable */
                       NULL, /* callback */
                       NULL); /* user_data */
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
                              G_PASTE_APPLICATION_IFACE,
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
                                          G_PASTE_APPLICATION_IFACE,
                                          NULL,
                                          error);
}

static gboolean
_spawn_sync (GDBusProxy *proxy,
             GError    **error)
{
    /* The call's own result is what says whether it worked: @error may be %NULL,
     * in which case there is nothing to look at. */
    g_autoptr (GVariant) res = g_dbus_proxy_call_sync (proxy,
                                                       "Activate",
                                                       g_variant_new ("(@a{sv})", app_get_platform_data ()),
                                                       G_DBUS_CALL_FLAGS_NONE,
                                                       -1,
                                                       NULL,
                                                       error);

    return !!res;
}

/**
 * g_paste_util_spawn_sync:
 * @app: the GPaste app to spawn
 * @error: return location for a #GError, or %NULL
 *
 * spawn a GPaste app
 *
 * The app is started through its org.freedesktop.Application interface, so a
 * failure is a %G_DBUS_ERROR or a %G_IO_ERROR rather than a %G_PASTE_ERROR.
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

/* The ActivateAction payload, which is the whole of what the two flavours below
 * have in common: same interface, same action, same floating @arg wrapped in the
 * "av" the method takes, same platform data. */
static GVariant *
activate_ui_params (const gchar *action,
                    GVariant    *arg)
{
    g_auto (GVariantBuilder) params;

    g_variant_builder_init_static (&params, G_VARIANT_TYPE ("av"));

    if (arg)
        g_variant_builder_add (&params, "v", arg);

    return g_variant_new ("(sav@a{sv})", action, &params, app_get_platform_data ());
}

/* @arg is floating, and the payload is what would have consumed it: a proxy that
 * never came up is the one path that has to do it by hand. */
static void
activate_ui_drop_arg (GVariant *arg)
{
    if (arg)
        g_variant_unref (g_variant_ref_sink (arg));
}

static void
g_paste_util_activate_ui_on_proxy_ready (GObject      *source_object G_GNUC_UNUSED,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
    g_autofree gpointer *data = (gpointer *) user_data;
    g_autofree gchar *action = data[0];
    GVariant *arg = data[1];
    g_autoptr (GError) error = NULL;
    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

    if (!proxy)
    {
        g_warning ("Failed to get D-Bus proxy: %s", error->message);
        activate_ui_drop_arg (arg);
        return;
    }

    g_dbus_proxy_call (proxy,
                       "ActivateAction",
                       activate_ui_params (action, arg),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL, /* cancellable */
                       NULL, /* callback */
                       NULL); /* user_data */
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
                              G_PASTE_UI_BUS_NAME,
                              G_PASTE_UI_OBJECT_PATH,
                              G_PASTE_APPLICATION_IFACE,
                              NULL,
                              g_paste_util_activate_ui_on_proxy_ready,
                              data);
}

/**
 * g_paste_util_activate_ui_sync:
 * @action: the action to activate
 * @arg: (nullable): the action argument
 * @error: a #GError
 *
 * Activate an action on a GPaste app, and wait for the call to have been made.
 *
 * g_paste_util_activate_ui() only starts the work, finishing it from a callback
 * the main loop dispatches; a caller with no main loop to give it -- the command
 * line client -- would return before anything reached the bus, and exit
 * reporting the success of having asked. This is that caller's version.
 *
 * Returns: %FALSE, with @error set, when the app could not be reached
 */
G_PASTE_VISIBLE gboolean
g_paste_util_activate_ui_sync (const gchar *action,
                               GVariant    *arg,
                               GError     **error)
{
    g_return_val_if_fail (g_utf8_validate (action, -1, NULL), FALSE);

    g_autoptr (GDBusProxy) proxy = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SESSION,
                                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                                  NULL,
                                                                  G_PASTE_UI_BUS_NAME,
                                                                  G_PASTE_UI_OBJECT_PATH,
                                                                  G_PASTE_APPLICATION_IFACE,
                                                                  NULL, /* cancellable */
                                                                  error);

    if (!proxy)
    {
        activate_ui_drop_arg (arg);

        return FALSE;
    }

    g_autoptr (GVariant) ret = g_dbus_proxy_call_sync (proxy,
                                                       "ActivateAction",
                                                       activate_ui_params (action, arg),
                                                       G_DBUS_CALL_FLAGS_NONE,
                                                       -1,
                                                       NULL, /* cancellable */
                                                       error);

    return !!ret;
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
g_paste_util_empty_with_confirmation (GPasteClient   *client,
                                      GPasteSettings *settings,
                                      const gchar    *history)
{
    g_return_if_fail (G_PASTE_IS_CLIENT (client));
    g_return_if_fail (G_PASTE_IS_SETTINGS (settings));
    g_return_if_fail (g_utf8_validate (history, -1, NULL));

    if (g_paste_settings_get_empty_history_confirmation (settings))
        g_paste_util_activate_ui ("empty", g_variant_new_string (history));
    else
        g_paste_client_empty_history (client, history, NULL, NULL);
}

/**
 * g_paste_util_one_line:
 * @text: the initial text
 *
 * Fold @text onto a single line, replacing every carriage return, line feed and
 * tabulation with a space. This is what everything displaying an item in a
 * one-line context — the command line's --oneline, the history rows, the search
 * provider's results — has to do to it first.
 *
 * Returns: the newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_util_one_line (const gchar *text)
{
    g_return_val_if_fail (text, NULL);

    return g_strdelimit (g_strdup (text), "\n\r\t", ' ');
}

/* The uris a files item holds, as a line to read rather than a list to act on:
 * a local file shown as its path with $HOME written "~", anything else as the
 * uri it is. Percent-decoded, so a file named "a b" reads as one.
 *
 * Done here rather than in the daemon because it is presentation, and because
 * the daemon got it wrong when it did it: a blind substitution over the uri
 * spelled a home file "file://~/a". */
static gchar *
g_paste_util_uris_display_string (const gchar *value)
{
    g_auto (GStrv) uris = g_strsplit (value, "\n", -1);
    g_autoptr (GString) shown = g_string_new (NULL);
    const gchar *home = g_get_home_dir ();
    gsize home_len = (home) ? strlen (home) : 0;

    for (GStrv u = uris; *u; ++u)
    {
        const gchar *uri = *u;

        /* A value ending in a newline splits into a trailing empty piece, which
         * is no uri and would only hang a separator off the end of the line. */
        if (!*uri)
            continue;

        if (shown->len)
            g_string_append_c (shown, ' ');

        if (!g_str_has_prefix (uri, "file://"))
        {
            g_string_append (shown, uri);
            continue;
        }

        const gchar *escaped = uri + strlen ("file://");
        g_autofree gchar *path = g_uri_unescape_string (escaped, NULL);
        const gchar *p = (path) ? path : escaped;

        /* Only a whole leading component, so /home/joe never shortens /home/joey. */
        if (home_len && g_str_has_prefix (p, home) && (!p[home_len] || p[home_len] == '/'))
            g_string_append_printf (shown, "~%s", p + home_len);
        else
            g_string_append (shown, p);
    }

    return g_string_free_and_steal (g_steal_pointer (&shown));
}

/**
 * g_paste_util_display_string:
 * @value: the value of an item
 * @kind: the kind of that item
 *
 * Compose what a client draws for an item: its @value, with the decoration its
 * @kind calls for around it.
 *
 * Composed by whoever draws the item rather than sent by the daemon so that it
 * comes out in *that* process's language: the daemon has a locale of its own,
 * and the gnome-shell extension a whole catalogue of its own; a client handed a
 * finished string could neither re-translate it nor read the value back out of
 * it, which is what used to make a second round trip the only way to recover an
 * item's contents. Here rather than on #GPasteClientItem so that everything
 * drawing an item shares the one implementation: the item caches what this
 * returns, the search provider describes its results with it, and the
 * gnome-shell extension calls it through introspection instead of keeping a
 * second copy of it in JavaScript.
 *
 * Only the words are translated. The brackets around them are punctuation, not
 * language, so they are spelled here.
 *
 * Returns: the newly allocated string
 */
G_PASTE_VISIBLE gchar *
g_paste_util_display_string (const gchar   *value,
                             GPasteItemKind kind)
{
    g_return_val_if_fail (value, NULL);

    switch (kind)
    {
    /* The one whose payload belongs inside the brackets: it reads as a
     * description of the image rather than as a name in front of one. */
    case G_PASTE_ITEM_KIND_IMAGE:
        return g_strdup_printf ("[%s, %s]", _("Image"), value);
    case G_PASTE_ITEM_KIND_COLOR:
        return g_strdup_printf ("[%s] %s", _("Color"), value);
    case G_PASTE_ITEM_KIND_URIS:
    {
        g_autofree gchar *uris = g_paste_util_uris_display_string (value);

        return g_strdup_printf ("[%s] %s", _("Files"), uris);
    }
    /* The value here is the password's *name*: the daemon masks the password
     * itself and never sends it. */
    case G_PASTE_ITEM_KIND_PASSWORD:
        return g_strdup_printf ("[%s] %s", _("Password"), value);
    /* Text wears nothing, and a kind we do not know is shown as it arrived
     * rather than mislabelled. */
    default:
        return g_strdup (value);
    }
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
 * g_paste_util_get_dbus_item_result:
 * @variant: a #GVariant
 *
 * Get an item out of the %G_PASTE_ITEM_VARIANT_STRING #GVariant the daemon
 * answers every single-item method with
 *
 * Returns: (transfer full): The item
 */
G_PASTE_VISIBLE GPasteClientItem *
g_paste_util_get_dbus_item_result (GVariant *variant)
{
    /* Owned, not borrowed: the format string is the one the daemon *builds*
     * these variants with, so it spells "s" rather than the "&s" that would
     * point into the variant, and g_variant_get hands back a copy of each
     * string for the caller to free. */
    g_autofree gchar *uuid = NULL;
    g_autofree gchar *value = NULL;
    guint32 kind;
    gboolean favourite;

    g_variant_get (variant, G_PASTE_ITEM_VARIANT_STRING, &uuid, &value, &kind, &favourite);

    return g_paste_client_item_new (uuid, value, kind, favourite);
}

/**
 * g_paste_util_get_dbus_items_result:
 * @variant: a #GVariant
 *
 * Get the %G_PASTE_ITEMS_VARIANT_STRING #GVariant as a list of items
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
        items = g_list_prepend (items, g_paste_util_get_dbus_item_result (v));
        g_variant_unref (v);
    }

    /* Prepended and reversed once: appending walks the whole list on every
     * item, which a full history makes quadratic. */
    return g_list_reverse (items);
}

/**
 * g_paste_util_get_dbus_histories_result:
 * @variant: a #GVariant
 *
 * Get the %G_PASTE_HISTORIES_VARIANT_STRING #GVariant as a list of histories
 *
 * Returns: (element-type GPasteClientHistory) (transfer full): The histories
 */
G_PASTE_VISIBLE GList *
g_paste_util_get_dbus_histories_result (GVariant *variant)
{
    GList *histories = NULL;
    GVariantIter iter;
    g_autofree gchar *name = NULL;
    guint64 size;

    g_variant_iter_init (&iter, variant);
    while (g_variant_iter_next (&iter, G_PASTE_HISTORY_VARIANT_STRING, &name, &size))
    {
        histories = g_list_prepend (histories, g_paste_client_history_new (name, size));
        g_clear_pointer (&name, g_free);
    }

    return g_list_reverse (histories);
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

    g_autoptr (GError) error = NULL;
    if (!g_file_set_contents (pidfile, contents, -1, &error))
        g_warning ("Failed to write pid file: %s", error->message);
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

    g_autoptr (GError) error = NULL;
    if (!g_file_get_contents (pidfile, &contents, NULL, &error))
    {
        g_warning ("Failed to read pid file: %s", error->message);
        return (GPid) -1;
    }

    return (GPid) g_ascii_strtoll (contents, NULL, 0);
#else
    return (GPid) -1;
#endif
}

/**
 * g_paste_util_reexecute_daemon:
 * @client: a connected #GPasteClient
 * @error: return location for a #GError, or %NULL
 *
 * Ask the daemon to re-execute itself through @client. It tears its D-Bus
 * connection down before replying, so a missing reply (%G_DBUS_ERROR_NO_REPLY)
 * is the expected success, not a failure, and @error is left unset for it.
 *
 * Returns: %TRUE if the daemon honoured the re-exec
 */
G_PASTE_VISIBLE gboolean
g_paste_util_reexecute_daemon (GPasteClient *client,
                               GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), FALSE);
    g_return_val_if_fail (!error || !*error, FALSE);

    g_autoptr (GError) err = NULL;

    g_paste_client_reexecute_sync (client, &err);

    /* Match the domain too: G_DBUS_ERROR_NO_REPLY is 4, and so is
     * G_IO_ERROR_NOT_DIRECTORY — a bare code comparison would report an
     * unrelated local failure as a successful re-exec. */
    if (!err || g_error_matches (err, G_DBUS_ERROR, G_DBUS_ERROR_NO_REPLY))
        return TRUE;

    g_propagate_error (error, g_steal_pointer (&err));

    return FALSE;
}

/**
 * g_paste_util_trigger_storage_migration:
 * @client: a connected #GPasteClient
 * @error: return location for a #GError, or %NULL
 *
 * Open the storage-migration gate and re-execute the daemon through @client
 * (g_paste_util_reexecute_daemon()), so on its next start it flushes, re-runs
 * the migration and reloads the chosen backend instead of another process racing
 * the running daemon. Resetting the backend revision to its default is exactly the
 * "never migrated" state that opens the gate.
 *
 * Errors come from the re-exec it delegates to, so they carry that call's
 * domains (%G_PASTE_ERROR from the daemon, %G_DBUS_ERROR or %G_IO_ERROR from
 * the transport) rather than any of its own.
 *
 * Returns: %TRUE if the migration was triggered
 */
G_PASTE_VISIBLE gboolean
g_paste_util_trigger_storage_migration (GPasteClient *client,
                                        GError      **error)
{
    g_return_val_if_fail (G_PASTE_IS_CLIENT (client), FALSE);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();

    g_paste_settings_reset (settings, G_PASTE_STORAGE_BACKEND_REVISION_SETTING);
    g_paste_settings_sync (settings);

    return g_paste_util_reexecute_daemon (client, error);
}
