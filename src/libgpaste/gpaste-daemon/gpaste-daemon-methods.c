// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <string.h>

static void
g_paste_daemon_private_do_add_item (const GPasteDaemonMethods *priv,
                                    GPasteItem                *item)
{
    /* Every item constructor can refuse its input and hand back %NULL. Each
     * caller validates what it passes (that is where a bad input becomes a D-Bus
     * error rather than a dropped add), and a constructor that refuses anyway has
     * already logged the reason itself: just don't dereference it here. */
    if (!item)
        return;

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
                               GError                   **error)
{
    G_PASTE_DBUS_ASSERT (text && length, G_PASTE_ERROR_INVALID_ARGUMENT, "no content to add");

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
                            const gchar               *text,
                            GError                   **error)
{
    g_paste_daemon_methods_do_add (priv, text, (text) ? strlen (text) : 0, error);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_add_file (const GPasteDaemonMethods *priv,
                                 const gchar               *file,
                                 GError                   **error)
{
    g_autofree gchar *content = NULL;
    gsize length;

    G_PASTE_DBUS_ASSERT (file && *file, G_PASTE_ERROR_INVALID_ARGUMENT, "no file to add");

    if (g_file_get_contents (file,
                             &content,
                             &length,
                             error))
    {
        if (g_utf8_validate (content, length, NULL))
            g_paste_daemon_methods_do_add (priv, content, length, error);
        else
        {
            g_autoptr (GError) img_error = NULL;
            g_autoptr (GdkTexture) img = gdk_texture_new_from_filename (file, &img_error);

            /* Neither text nor a loadable image: there is nothing to add, and
             * building an item out of a NULL texture would only yield a NULL
             * item the add path then dereferences. */
            if (!img)
            {
                g_warning ("Failed to load image from %s: %s", file, (img_error) ? img_error->message : "unknown error");
                G_PASTE_DBUS_ASSERT (FALSE, G_PASTE_ERROR_WRONG_ITEM_KIND, "the file is neither text nor an image");
            }

            g_paste_daemon_private_do_add_item (priv, g_paste_image_item_new (img));
        }
    }
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_add_password (const GPasteDaemonMethods *priv,
                                     const gchar               *name,
                                     const gchar               *password,
                                     GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && password, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to add");

    g_paste_history_delete_password (priv->history, name);
    g_paste_daemon_private_do_add_item (priv, g_paste_password_item_new (name, password));
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_backup_history (const GPasteDaemonMethods *priv,
                                       const gchar               *history,
                                       const gchar               *backup,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT (history && backup, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to backup");

    GPasteSettings *settings = priv->settings;

    /* create a new history to do the backup without polluting the current one */
    g_autoptr (GPasteHistory) _history = g_paste_history_new (settings);
    const gchar *old_name = g_paste_history_get_current (priv->history);

    /* We emit all those signals to be sure that all the guis have their histories list updated */
    g_paste_history_load (_history, history);
    g_paste_daemon2_emit_raw_switch_history (priv->skeleton, history);
    g_paste_history_save (_history, backup);
    g_paste_daemon2_emit_raw_switch_history (priv->skeleton, backup);
    g_paste_daemon2_emit_raw_switch_history (priv->skeleton, old_name);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete (const GPasteDaemonMethods *priv,
                               const gchar               *uuid,
                               GError                   **error)
{
    G_PASTE_DBUS_ASSERT (g_paste_history_remove_by_uuid (priv->history, uuid), G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_history (const GPasteDaemonMethods *priv,
                                       const gchar               *name,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to delete");

    GPasteHistory *history = priv->history;

    /* Nothing was deleted if this fails (the store is being handed over), so
     * neither announce the deletion nor switch away from a history that is
     * still there; the caller turns @error into the method's reply. */
    if (!g_paste_history_delete (history, name, error))
        return;

    g_paste_daemon2_emit_raw_delete_history (priv->skeleton, name);

    if (g_paste_str_equal (name, g_paste_history_get_current (priv->history)))
        g_paste_history_switch (history, G_PASTE_DEFAULT_HISTORY);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_password (const GPasteDaemonMethods *priv,
                                        const gchar               *name,
                                        GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to delete");

    g_paste_history_delete_password (priv->history, name);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_empty_history (const GPasteDaemonMethods *priv,
                                      const gchar               *name)
{
    if (g_paste_str_equal (name, g_paste_history_get_current (priv->history)))
        g_paste_history_empty (priv->history);
    else
    {
        g_autoptr (GPasteHistory) history = g_paste_history_new (priv->settings);

        g_paste_history_save (history, name);
    }

    g_paste_daemon2_emit_raw_empty_history (priv->skeleton, name);
}

G_PASTE_VISIBLE const gchar *
g_paste_daemon_methods_get_element (const GPasteDaemonMethods *priv,
                                    const gchar               *uuid,
                                    GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (priv->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);

    return g_paste_item_get_display_string (item);
}

G_PASTE_VISIBLE gboolean
g_paste_daemon_methods_get_element_at_index (const GPasteDaemonMethods *priv,
                                             guint64                    index,
                                             const gchar              **uuid,
                                             const gchar              **value,
                                             GError                   **error)
{
    GPasteHistory *history = priv->history;

    G_PASTE_DBUS_ASSERT_FULL (index < g_paste_history_get_length (history), G_PASTE_ERROR_INVALID_INDEX, "invalid index received", FALSE);

    GPasteItem *item = g_paste_history_get (history, index);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_INVALID_INDEX, "received no value for this index", FALSE);

    *uuid = g_paste_item_get_uuid (item);
    *value = g_paste_item_get_display_string (item);

    return TRUE;
}

G_PASTE_VISIBLE const gchar *
g_paste_daemon_methods_get_element_kind (const GPasteDaemonMethods *priv,
                                         const gchar               *uuid,
                                         GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (priv->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_INVALID_INDEX, "received no item for this index", NULL);

    return g_paste_item_kind_to_string (g_paste_item_get_kind (item));
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_elements (const GPasteDaemonMethods *priv,
                                     const gchar * const       *uuids,
                                     GError                   **error)
{
    GPasteHistory *history = priv->history;
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ss)"));

    for (gsize i = 0; uuids && uuids[i]; ++i)
    {
        GPasteItem *item = g_paste_history_get_by_uuid (history, uuids[i]);

        if (!item)
        {
            g_variant_builder_clear (&builder);
            G_PASTE_DBUS_ASSERT_FULL (FALSE, G_PASTE_ERROR_INVALID_INDEX, "received no value for this index", NULL);
        }

        g_variant_builder_add (&builder, "(ss)", g_paste_item_get_uuid (item), g_paste_item_get_display_string (item));
    }

    return g_variant_builder_end (&builder);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_history (const GPasteDaemonMethods *priv)
{
    const GPtrArray *history = g_paste_history_get_history (priv->history);
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ss)"));

    for (guint i = 0; i < history->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (history, i);

        g_variant_builder_add (&builder, "(ss)", g_paste_item_get_uuid (item), g_paste_item_get_display_string (item));
    }

    return g_variant_builder_end (&builder);
}

G_PASTE_VISIBLE const gchar *
g_paste_daemon_methods_get_history_name (const GPasteDaemonMethods *priv)
{
    return g_paste_history_get_current (priv->history);
}

G_PASTE_VISIBLE guint64
g_paste_daemon_methods_get_history_size (const GPasteDaemonMethods *priv,
                                         const gchar               *name)
{
    if (g_paste_str_equal (name, g_paste_history_get_current (priv->history)))
        return g_paste_history_get_length (priv->history);

    g_autoptr (GPasteHistory) history = g_paste_history_new (priv->settings);

    g_paste_history_load (history, name);

    return g_paste_history_get_length (history);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_image (const GPasteDaemonMethods *priv,
                                  const gchar               *uuid,
                                  GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (priv->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);
    G_PASTE_DBUS_ASSERT_FULL (G_PASTE_IS_IMAGE_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "Provided uuid doesn't match an image item.", NULL);

    /* Hand the bytes over so clients never dereference the item's path
     * themselves: how and where the image is stored stays the daemon's
     * business. The item carries its PNG when it came from a blob-storing
     * backend (or a fresh capture); older path-based items read their file. */
    GBytes *png = g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (item));
    g_autoptr (GBytes) bytes = NULL;

    if (png)
        bytes = g_bytes_ref (png);
    else
    {
        gchar *data = NULL;
        gsize length = 0;

        if (!g_file_get_contents (g_paste_item_get_value (item), &data, &length, error))
            return NULL;

        bytes = g_bytes_new_take (data, length);
    }

    return g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), bytes, TRUE);
}

G_PASTE_VISIBLE const gchar *
g_paste_daemon_methods_get_raw_element (const GPasteDaemonMethods *priv,
                                        const gchar               *uuid,
                                        GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (priv->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);

    return g_paste_item_get_value (item);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_raw_history (const GPasteDaemonMethods *priv)
{
    const GPtrArray *history = g_paste_history_get_history (priv->history);
    GVariantBuilder builder;

    g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ss)"));

    for (guint i = 0; i < history->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (history, i);

        g_variant_builder_add (&builder, "(ss)", g_paste_item_get_uuid (item), g_paste_item_get_value (item));
    }

    return g_variant_builder_end (&builder);
}

G_PASTE_VISIBLE GStrv
g_paste_daemon_methods_list_histories (const GPasteDaemonMethods *priv,
                                       GError                   **error)
{
    return g_paste_history_list (priv->history, error);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_merge (const GPasteDaemonMethods *priv,
                              const gchar               *decoration,
                              const gchar               *separator,
                              const gchar * const       *uuids,
                              GError                   **error)
{
    gsize length = (uuids) ? g_strv_length ((GStrv) uuids) : 0;

    G_PASTE_DBUS_ASSERT (length, G_PASTE_ERROR_INVALID_ARGUMENT, "nothing to merge");

    GPasteHistory *history = priv->history;
    g_autoptr (GString) str = g_string_new (NULL);

    for (gsize i = 0; i < length; ++i)
    {
        GPasteItem *item = g_paste_history_get_by_uuid (history, uuids[i]);

        G_PASTE_DBUS_ASSERT (item, G_PASTE_ERROR_NOT_FOUND, "no item matching this uuid");

        g_string_append_printf (str, "%s%s%s%s",
                                (i) ? separator : "",
                                decoration,
                                g_paste_item_get_value (item),
                                decoration);
    }

    g_paste_daemon_methods_do_add (priv, str->str, str->len, error);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_track (const GPasteDaemonMethods *priv,
                              gboolean                   tracking_state)
{
    g_paste_settings_set_track_changes (priv->settings, tracking_state);
}

/* The policy itself, in one place: whether the extension coming or going should
 * drive the tracking is the "track-extension-state" key's business. Reached both
 * from the D-Bus method and, without a bus round trip, from a host running the
 * daemon in its own process (see g_paste_daemon_extension_state_changed()). */
G_PASTE_VISIBLE void
g_paste_daemon_methods_extension_state_changed (const GPasteDaemonMethods *priv,
                                                gboolean                   state)
{
    if (g_paste_settings_get_track_extension_state (priv->settings))
        g_paste_settings_set_track_changes (priv->settings, state);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_rename_password (const GPasteDaemonMethods *priv,
                                        const gchar               *old_name,
                                        const gchar               *new_name,
                                        GError                   **error)
{
    G_PASTE_DBUS_ASSERT (old_name && *old_name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to rename");

    g_paste_history_rename_password (priv->history, old_name, new_name);
}

G_PASTE_VISIBLE GStrv
g_paste_daemon_methods_search (const GPasteDaemonMethods *priv,
                               const gchar               *query,
                               GError                   **error)
{
    GStrv results = g_paste_history_search (priv->history, query);

    G_PASTE_DBUS_ASSERT_FULL (results, G_PASTE_ERROR_FAILED, "Error while performing search", NULL);

    return results;
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_select (const GPasteDaemonMethods *priv,
                               const gchar               *uuid,
                               GError                   **error)
{
    G_PASTE_DBUS_ASSERT (g_paste_history_select (priv->history, uuid), G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_replace (const GPasteDaemonMethods *priv,
                                const gchar               *uuid,
                                const gchar               *contents,
                                GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (priv->history, uuid);

    G_PASTE_DBUS_ASSERT (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
    G_PASTE_DBUS_ASSERT (G_PASTE_IS_TEXT_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "attempted to replace an item other than GPasteTextItem");
    G_PASTE_DBUS_ASSERT (contents && *contents, G_PASTE_ERROR_INVALID_ARGUMENT, "no contents given");

    g_paste_history_replace (priv->history, uuid, contents);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_set_password (const GPasteDaemonMethods *priv,
                                     const gchar               *uuid,
                                     const gchar               *name,
                                     GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (priv->history, uuid);

    G_PASTE_DBUS_ASSERT (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
    G_PASTE_DBUS_ASSERT (G_PASTE_IS_TEXT_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "attempted to replace an item other than GPasteTextItem");
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password name given");
    G_PASTE_DBUS_ASSERT (!g_paste_history_get_password (priv->history, name), G_PASTE_ERROR_ALREADY_EXISTS, "a password with that name already exists");

    g_paste_history_set_password (priv->history, uuid, name);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_switch_history (const GPasteDaemonMethods *priv,
                                       const gchar               *name,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to switch to");

    g_paste_history_switch (priv->history, name);
}
