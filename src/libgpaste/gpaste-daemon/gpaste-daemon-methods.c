// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <string.h>

static gchar *
g_paste_daemon_get_dbus_string_parameter (GVariant *parameters,
                                          gsize    *length)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);

    return g_variant_dup_string (variant, length);
}

static void
_variant_iter_read_strings_parameter (GVariantIter *parameters_iter,
                                      gchar       **str1,
                                      gchar       **str2)
{
    g_autoptr (GVariant) variant1 = g_variant_iter_next_value (parameters_iter);
    g_autoptr (GVariant) variant2 = g_variant_iter_next_value (parameters_iter);
    gsize length;

    *str1 = g_variant_dup_string (variant1, &length);
    *str2 = g_variant_dup_string (variant2, &length);
}

static void
g_paste_daemon_get_dbus_strings_parameter (GVariant *parameters,
                                           gchar   **str1,
                                           gchar   **str2)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);
    _variant_iter_read_strings_parameter (&parameters_iter, str1, str2);
}

static guint64
g_paste_daemon_get_dbus_uint64_parameter (GVariant *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
    return g_variant_get_uint64 (variant);
}

static void
g_paste_daemon_private_do_add_item (const GPasteDaemonMethods *priv,
                                    GPasteItem                *item)
{
    /* g_paste_history_add takes ownership; keep our own ref for the select call below */
    g_paste_history_add (priv->history, g_object_ref (item));
    if (!g_paste_clipboards_manager_select (priv->clipboards_manager, item))
        g_paste_history_remove (priv->history, 0);
    g_object_unref (item);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_do_add (const GPasteDaemonMethods *priv,
                               const gchar               *text,
                               guint64                    length,
                               GPasteDBusError          **err)
{
    G_PASTE_DBUS_ASSERT (text && length, "no content to add");

    GPasteSettings *settings = priv->settings;
    gboolean trim_items = g_paste_settings_get_trim_items (settings);
    g_autofree gchar *stripped = trim_items ? g_strstrip (g_strdup (text)) : NULL;
    const gchar *to_add = trim_items ? stripped : text;

    if (length >= g_paste_settings_get_min_text_item_size (settings) &&
        length <= g_paste_settings_get_max_text_item_size (settings) &&
        strlen (to_add) != 0)
    {
        g_paste_daemon_private_do_add_item (priv, g_paste_text_item_new (to_add));
    }
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_add (const GPasteDaemonMethods *priv,
                            GVariant                  *parameters,
                            GPasteDBusError          **err)
{
    gsize length;
    g_autofree gchar *text = g_paste_daemon_get_dbus_string_parameter (parameters, &length);

    g_paste_daemon_methods_do_add (priv, text, length, err);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_add_file (const GPasteDaemonMethods *priv,
                                 GVariant                  *parameters,
                                 GError                   **error,
                                 GPasteDBusError          **err)
{
    gsize length;
    g_autofree gchar *file = g_paste_daemon_get_dbus_string_parameter (parameters, &length);
    g_autofree gchar *content = NULL;

    G_PASTE_DBUS_ASSERT (file, "no file to add");

    if (g_file_get_contents (file,
                             &content,
                             &length,
                             error))
    {
        if (g_utf8_validate (content, length, NULL))
            g_paste_daemon_methods_do_add (priv, content, length, err);
        else
        {
            g_autoptr (GError) img_error = NULL;
            g_autoptr (GdkTexture) img = gdk_texture_new_from_filename (file, &img_error);
            if (img_error)
                g_warning ("Failed to load image from %s: %s", file, img_error->message);

            g_paste_daemon_private_do_add_item (priv, g_paste_image_item_new (img));
        }
    }
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_add_password (const GPasteDaemonMethods *priv,
                                     GVariant                  *parameters,
                                     GPasteDBusError          **err)
{
    g_autofree gchar *name = NULL;
    g_autofree gchar *password = NULL;

    g_paste_daemon_get_dbus_strings_parameter (parameters, &name, &password);

    G_PASTE_DBUS_ASSERT (name && password, "no password to add");

    g_paste_history_delete_password (priv->history, name);
    g_paste_daemon_private_do_add_item (priv, g_paste_password_item_new (name, password));
}

static void
g_paste_daemon_private_delete_history_signal (const GPasteDaemonMethods *priv,
                                              const gchar               *history)
{
    GVariant *variant = g_variant_new_string (history);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA (DELETE_HISTORY, variant);
}

static void
g_paste_daemon_private_empty_history_signal (const GPasteDaemonMethods *priv,
                                             const gchar               *history)
{
    GVariant *variant = g_variant_new_string (history);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA (EMPTY_HISTORY, variant);
}

static void
g_paste_daemon_private_switch_history_signal (const GPasteDaemonMethods *priv,
                                              const gchar               *history)
{
    GVariant *variant = g_variant_new_string (history);

    G_PASTE_SEND_DBUS_SIGNAL_WITH_DATA (SWITCH_HISTORY, variant);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_backup_history (const GPasteDaemonMethods *priv,
                                       GVariant                  *parameters,
                                       GPasteDBusError          **err)
{
    g_autofree gchar *history = NULL;
    g_autofree gchar *backup = NULL;

    g_paste_daemon_get_dbus_strings_parameter (parameters, &history, &backup);

    G_PASTE_DBUS_ASSERT (history && backup, "no history to backup");

    GPasteSettings *settings = priv->settings;

    /* create a new history to do the backup without polluting the current one */
    g_autoptr (GPasteHistory) _history = g_paste_history_new (settings);
    const gchar *old_name = g_paste_history_get_current (priv->history);

    /* We emit all those signals to be sure that all the guis have their histories list updated */
    g_paste_history_load (_history, history);
    g_paste_daemon_private_switch_history_signal (priv, history);
    g_paste_history_save (_history, backup);
    g_paste_daemon_private_switch_history_signal (priv, backup);
    g_paste_daemon_private_switch_history_signal (priv, old_name);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete (const GPasteDaemonMethods *priv,
                               GVariant                  *parameters,
                               GPasteDBusError          **err)
{
    g_autofree gchar *uuid = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    G_PASTE_DBUS_ASSERT (g_paste_history_remove_by_uuid (priv->history, uuid), "Provided uuid doesn't match any item.");
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_history (const GPasteDaemonMethods *priv,
                                       GVariant                  *parameters,
                                       GPasteDBusError          **err)
{
    g_autofree gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    G_PASTE_DBUS_ASSERT (name, "no history to delete");

    GPasteHistory *history = priv->history;

    g_paste_history_delete (history, name, NULL);
    g_paste_daemon_private_delete_history_signal (priv, name);

    if (g_paste_str_equal (name, g_paste_history_get_current (priv->history)))
        g_paste_history_switch (history, G_PASTE_DEFAULT_HISTORY);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_password (const GPasteDaemonMethods *priv,
                                        GVariant                  *parameters,
                                        GPasteDBusError          **err)
{
    g_autofree gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    G_PASTE_DBUS_ASSERT (name, "no password to delete");

    g_paste_history_delete_password (priv->history, name);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_empty_history (const GPasteDaemonMethods *priv,
                                      GVariant                  *parameters)
{
    g_autofree gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    if (g_paste_str_equal (name, g_paste_history_get_current (priv->history)))
        g_paste_history_empty (priv->history);
    else
    {
        g_autoptr (GPasteHistory) history = g_paste_history_new (priv->settings);

        g_paste_history_save (history, name);
    }

    g_paste_daemon_private_empty_history_signal (priv, name);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_element (const GPasteDaemonMethods *priv,
                                    GVariant                  *parameters,
                                    GPasteDBusError          **err)
{
    g_autofree gchar *uuid = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);
    const GPasteItem *item = g_paste_history_get_by_uuid (priv->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, "Provided uuid doesn't match any item.", NULL);

    GVariant *variant = g_variant_new_string (g_paste_item_get_display_string (item));
    return g_variant_new_tuple (&variant, 1);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_element_at_index (const GPasteDaemonMethods *priv,
                                             GVariant                  *parameters,
                                             GPasteDBusError          **err)
{
    GPasteHistory *history = priv->history;
    guint64 index = g_paste_daemon_get_dbus_uint64_parameter (parameters);

    G_PASTE_DBUS_ASSERT_FULL (index < g_paste_history_get_length (history), "invalid index received", NULL);

    const GPasteItem *item = g_paste_history_get (history, index);

    G_PASTE_DBUS_ASSERT_FULL (item, "received no value for this index", NULL);

    GVariant *data[] = {
        g_variant_new_string (g_paste_item_get_uuid (item)),
        g_variant_new_string (g_paste_item_get_display_string (item))
    };

    return g_variant_new_tuple (data, 2);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_element_kind (const GPasteDaemonMethods *priv,
                                         GVariant                  *parameters,
                                         GPasteDBusError          **err)
{
    GPasteHistory *history = priv->history;
    g_autofree gchar *uuid = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    const GPasteItem *item = g_paste_history_get_by_uuid (history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, "received no item for this index", NULL);

    GVariant *variant = g_variant_new_string (g_paste_item_get_kind (item));

    return g_variant_new_tuple (&variant, 1);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_elements (const GPasteDaemonMethods *priv,
                                     GVariant                  *parameters,
                                     GPasteDBusError          **err)
{
    GPasteHistory *history = priv->history;
    GVariantIter parameters_iter;
    GVariantBuilder builder;

    g_variant_iter_init (&parameters_iter, parameters);
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ss)"));

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
    gsize len;
    g_autofree const gchar **uuids = g_variant_get_strv (variant, &len);

    for (gsize i = 0; i < len; ++i)
    {
        const GPasteItem *item = g_paste_history_get_by_uuid (history, uuids[i]);
        G_PASTE_DBUS_ASSERT_FULL (item, "received no value for this index", NULL);
        g_variant_builder_add (&builder, "(ss)", g_paste_item_get_uuid (item), g_paste_item_get_display_string (item));
    }

    GVariant *ans = g_variant_builder_end (&builder);

    return g_variant_new_tuple (&ans, 1);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_history (const GPasteDaemonMethods *priv)
{
    const GList *history = g_paste_history_get_history (priv->history);
    guint64 length = g_list_length ((GList *) history);
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ss)"));

    for (guint64 i = 0; i < length; ++i, history = g_list_next (history))
    {
        const GPasteItem *item = history->data;
        g_variant_builder_add (&builder, "(ss)", g_paste_item_get_uuid (item), g_paste_item_get_display_string (item));
    }

    GVariant *variant = g_variant_builder_end (&builder);

    return g_variant_new_tuple (&variant, 1);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_history_name (const GPasteDaemonMethods *priv)
{
    GVariant *variant = g_variant_new_string (g_paste_history_get_current (priv->history));
    return g_variant_new_tuple (&variant, 1);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_history_size (const GPasteDaemonMethods *priv,
                                         GVariant                  *parameters)
{
    g_autofree gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);
    guint64 size;

    if (g_paste_str_equal (name, g_paste_history_get_current (priv->history)))
        size = g_paste_history_get_length (priv->history);
    else
    {
        g_autoptr (GPasteHistory) history = g_paste_history_new (priv->settings);

        g_paste_history_load (history, name);
        size = g_paste_history_get_length (history);
    }


    GVariant *variant = g_variant_new_uint64 (size);
    return g_variant_new_tuple (&variant, 1);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_raw_element (const GPasteDaemonMethods *priv,
                                        GVariant                  *parameters,
                                        GPasteDBusError          **err)
{
    g_autofree gchar *uuid = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);
    const GPasteItem *item = g_paste_history_get_by_uuid (priv->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, "Provided uuid doesn't match any item.", NULL);

    GVariant *variant = g_variant_new_string (g_paste_item_get_value (item));
    return g_variant_new_tuple (&variant, 1);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_raw_history (const GPasteDaemonMethods *priv)
{
    const GList *history = g_paste_history_get_history (priv->history);
    guint64 length = g_list_length ((GList *) history);
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ss)"));

    for (guint64 i = 0; i < length; ++i, history = g_list_next (history))
    {
        const GPasteItem *item = history->data;
        g_variant_builder_add (&builder, "(ss)", g_paste_item_get_uuid (item), g_paste_item_get_value (item));
    }

    GVariant *variant = g_variant_builder_end (&builder);

    return g_variant_new_tuple (&variant, 1);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_list_histories (const GPasteDaemonMethods *priv,
                                       GError                   **error)
{
    g_auto (GStrv) history_names = g_paste_history_list (priv->history, error);

    if (!history_names)
        return NULL;

    GVariant *variant = g_variant_new_strv ((const gchar * const *) history_names, -1);

    return g_variant_new_tuple (&variant, 1);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_merge (const GPasteDaemonMethods *priv,
                              GVariant                  *parameters,
                              GPasteDBusError          **err)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autofree gchar *decoration = NULL;
    g_autofree gchar *separator  = NULL;

    _variant_iter_read_strings_parameter (&parameters_iter, &decoration, &separator);

    g_autoptr (GVariant) v_uuids = g_variant_iter_next_value (&parameters_iter);
    gsize length;
    const GStrv uuids = (const GStrv) g_variant_get_strv (v_uuids, &length);

    G_PASTE_DBUS_ASSERT (length, "nothing to merge");

    GPasteHistory *history = priv->history;
    g_autoptr (GString) str = g_string_new (NULL);

    for (guint64 i = 0; i < length; ++i)
    {
        const GPasteItem *item = g_paste_history_get_by_uuid (history, uuids[i]);

        G_PASTE_DBUS_ASSERT (item, "no item matching this uuid");

        g_string_append_printf (str, "%s%s%s%s",
                                (i) ? separator : "",
                                decoration,
                                g_paste_item_get_value (item),
                                decoration);
    }

    g_paste_daemon_methods_do_add (priv, str->str, str->len, err);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_track (const GPasteDaemonMethods *priv,
                              GVariant                  *parameters)
{
    GVariantIter parameters_iter;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant = g_variant_iter_next_value (&parameters_iter);
    gboolean tracking_state = g_variant_get_boolean (variant);

    g_paste_settings_set_track_changes (priv->settings, tracking_state);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_on_extension_state_changed (const GPasteDaemonMethods *priv,
                                                   GVariant                  *parameters)
{
    if (g_paste_settings_get_track_extension_state (priv->settings))
        g_paste_daemon_methods_track (priv, parameters);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_rename_password (const GPasteDaemonMethods *priv,
                                        GVariant                  *parameters,
                                        GPasteDBusError          **err)
{
    g_autofree gchar *old_name = NULL;
    g_autofree gchar *new_name = NULL;

    g_paste_daemon_get_dbus_strings_parameter (parameters, &old_name, &new_name);

    G_PASTE_DBUS_ASSERT (old_name, "no password to rename");

    g_paste_history_rename_password (priv->history, old_name, new_name);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_search (const GPasteDaemonMethods *priv,
                               GVariant                  *parameters,
                               GPasteDBusError          **err)
{
    g_autofree gchar *search = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);
    g_auto (GStrv) results = g_paste_history_search (priv->history, search);

    G_PASTE_DBUS_ASSERT_FULL (results, "Error while performing search", NULL);

    GVariant *variant = g_variant_new_strv ((const gchar * const *) results, -1);
    return g_variant_new_tuple (&variant, 1);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_select (const GPasteDaemonMethods *priv,
                               GVariant                  *parameters,
                               GPasteDBusError          **err)
{
    g_autofree gchar *uuid = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    G_PASTE_DBUS_ASSERT (g_paste_history_select (priv->history, uuid), "Provided uuid doesn't match any item.");
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_replace (const GPasteDaemonMethods *priv,
                                GVariant                  *parameters,
                                GPasteDBusError          **err)
{
    GPasteHistory *history = priv->history;
    GVariantIter parameters_iter;
    gsize length;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant1 = g_variant_iter_next_value (&parameters_iter);
    const gchar *uuid = g_variant_get_string (variant1, NULL);

    const GPasteItem *item = g_paste_history_get_by_uuid (history, uuid);

    G_PASTE_DBUS_ASSERT (item, "Provided uuid doesn't match any item.");
    G_PASTE_DBUS_ASSERT (_G_PASTE_IS_TEXT_ITEM (item) && g_paste_str_equal (g_paste_item_get_kind (item), "Text"), "attempted to replace an item other than GPasteTextItem");

    g_autoptr (GVariant) variant2 = g_variant_iter_next_value (&parameters_iter);
    g_autofree gchar *contents = g_variant_dup_string (variant2, &length);

    G_PASTE_DBUS_ASSERT (contents, "no contents given");

    g_paste_history_replace (priv->history, uuid, contents);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_set_password (const GPasteDaemonMethods *priv,
                                     GVariant                  *parameters,
                                     GPasteDBusError          **err)
{
    GPasteHistory *history = priv->history;
    GVariantIter parameters_iter;
    gsize length;

    g_variant_iter_init (&parameters_iter, parameters);

    g_autoptr (GVariant) variant1 = g_variant_iter_next_value (&parameters_iter);
    const gchar *uuid = g_variant_get_string (variant1, NULL);
    const GPasteItem *item = g_paste_history_get_by_uuid (history, uuid);

    G_PASTE_DBUS_ASSERT (item, "Provided uuid doesn't match any item.");
    G_PASTE_DBUS_ASSERT (_G_PASTE_IS_TEXT_ITEM (item) && g_paste_str_equal (g_paste_item_get_kind (item), "Text"), "attempted to replace an item other than GPasteTextItem");

    g_autoptr (GVariant) variant2 = g_variant_iter_next_value (&parameters_iter);
    g_autofree gchar *name = g_variant_dup_string (variant2, &length);

    G_PASTE_DBUS_ASSERT (name, "no password name given");
    G_PASTE_DBUS_ASSERT (!g_paste_history_get_password (priv->history, name), "a password with tat name already exists");

    g_paste_history_set_password (priv->history, uuid, name);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_switch_history (const GPasteDaemonMethods *priv,
                                       GVariant                  *parameters,
                                       GPasteDBusError          **err)
{
    g_autofree gchar *name = g_paste_daemon_get_dbus_string_parameter (parameters, NULL);

    G_PASTE_DBUS_ASSERT (name, "no history to switch to");

    g_paste_history_switch (priv->history, name);
}
