// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-text-item.h>

#include <string.h>

/* The one shape an item takes on the wire, and the one value it carries: the
 * string a user is shown. */
static GVariant *
g_paste_daemon_methods_item_variant (GPasteItem *item)
{
    return g_variant_new (G_PASTE_ITEM_VARIANT_STRING,
                          g_paste_item_get_uuid (item),
                          g_paste_item_get_display_string (item),
                          (guint32) g_paste_item_get_kind (item),
                          g_paste_item_is_favourite (item));
}

/* The same for a whole array of them, which is every listing the daemon
 * answers: the history and a search. */
static GVariant *
g_paste_daemon_methods_items_variant (const GPtrArray *items)
{
    g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_PASTE_ITEMS_VARIANT_TYPE);

    for (guint i = 0; i < items->len; ++i)
        g_variant_builder_add_value (&builder, g_paste_daemon_methods_item_variant (g_ptr_array_index (items, i)));

    return g_variant_builder_end (&builder);
}

static void
g_paste_daemon_methods_do_add_item (const GPasteDaemonMethods *self,
                                    GPasteItem                *item)
{
    /* Every item constructor can refuse its input and hand back %NULL. Each
     * caller validates what it passes (that is where a bad input becomes a D-Bus
     * error rather than a dropped add), and a constructor that refuses anyway has
     * already logged the reason itself: just don't dereference it here. */
    if (!item)
        return;

    /* g_paste_history_add takes ownership; keep our own ref for the select call below */
    g_paste_history_add (self->history, g_object_ref (item));
    if (!g_paste_clipboards_manager_select (self->clipboards_manager, item))
        g_paste_history_remove (self->history, 0);
    g_object_unref (item);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_do_add (const GPasteDaemonMethods *self,
                               const gchar               *text,
                               guint64                    length,
                               GError                   **error)
{
    G_PASTE_DBUS_ASSERT (text && length, G_PASTE_ERROR_INVALID_ARGUMENT, "no content to add");

    GPasteSettings *settings = self->settings;
    gboolean trim_items = g_paste_settings_get_trim_items (settings);
    g_autofree gchar *stripped = trim_items ? g_strstrip (g_strdup (text)) : NULL;
    const gchar *to_add = trim_items ? stripped : text;

    if (length >= g_paste_settings_get_min_text_item_size (settings) &&
        length <= g_paste_settings_get_max_text_item_size (settings) &&
        strlen (to_add) != 0)
    {
        g_paste_daemon_methods_do_add_item (self, g_paste_text_item_new (to_add));
    }
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_add_text (const GPasteDaemonMethods *self,
                                 const gchar               *text,
                                 GError                   **error)
{
    g_paste_daemon_methods_do_add (self, text, (text) ? strlen (text) : 0, error);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_add_file (const GPasteDaemonMethods *self,
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
            g_paste_daemon_methods_do_add (self, content, length, error);
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

            g_paste_daemon_methods_do_add_item (self, g_paste_image_item_new (img));
        }
    }
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_add_password (const GPasteDaemonMethods *self,
                                     const gchar               *name,
                                     const gchar               *password,
                                     GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && password, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to add");

    g_paste_history_delete_password (self->history, name);
    g_paste_daemon_methods_do_add_item (self, g_paste_password_item_new (name, password));
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_backup_history (const GPasteDaemonMethods *self,
                                       const gchar               *history,
                                       const gchar               *backup,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT (history && backup, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to backup");

    GPasteSettings *settings = self->settings;

    /* A history of its own, so the backup never disturbs the current one: the
     * current history is not switched away from, and no UI has to be told it
     * was. What did change is the set of histories, which has a signal of its
     * own precisely because a new one appearing is not a switch. */
    g_autoptr (GPasteHistory) _history = g_paste_history_new (settings);

    g_paste_history_load (_history, history);
    g_paste_history_save (_history, backup);

    g_paste_daemon3_emit_raw_histories_changed (self->skeleton);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_item (const GPasteDaemonMethods *self,
                                    const gchar               *uuid,
                                    GError                   **error)
{
    G_PASTE_DBUS_ASSERT (g_paste_history_remove_by_uuid (self->history, uuid), G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_history (const GPasteDaemonMethods *self,
                                       const gchar               *name,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to delete");

    GPasteHistory *history = self->history;

    /* Nothing was deleted if this fails (the store is being handed over), so
     * neither announce the deletion nor switch away from a history that is
     * still there; the caller turns @error into the method's reply. */
    if (!g_paste_history_delete (history, name, error))
        return;

    g_paste_daemon3_emit_raw_delete_history (self->skeleton, name);

    if (g_paste_str_equal (name, g_paste_history_get_current (self->history)))
        g_paste_history_switch (history, G_PASTE_DEFAULT_HISTORY);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_password (const GPasteDaemonMethods *self,
                                        const gchar               *name,
                                        GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to delete");

    g_paste_history_delete_password (self->history, name);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_empty_history (const GPasteDaemonMethods *self,
                                      const gchar               *name)
{
    if (g_paste_str_equal (name, g_paste_history_get_current (self->history)))
        g_paste_history_empty (self->history);
    else
    {
        g_autoptr (GPasteHistory) history = g_paste_history_new (self->settings);

        g_paste_history_save (history, name);
    }

    g_paste_daemon3_emit_raw_empty_history (self->skeleton, name);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_item (const GPasteDaemonMethods *self,
                                 const gchar               *uuid,
                                 GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);

    return g_paste_daemon_methods_item_variant (item);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_item_at_index (const GPasteDaemonMethods *self,
                                          guint64                    index,
                                          GError                   **error)
{
    GPasteHistory *history = self->history;

    G_PASTE_DBUS_ASSERT_FULL (index < g_paste_history_get_length (history), G_PASTE_ERROR_INVALID_INDEX, "invalid index received", NULL);

    GPasteItem *item = g_paste_history_get (history, index);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_INVALID_INDEX, "received no value for this index", NULL);

    return g_paste_daemon_methods_item_variant (item);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_items (const GPasteDaemonMethods *self,
                                  const gchar * const       *uuids,
                                  GError                   **error)
{
    GPasteHistory *history = self->history;
    g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_PASTE_ITEMS_VARIANT_TYPE);

    for (gsize i = 0; uuids && uuids[i]; ++i)
    {
        GPasteItem *item = g_paste_history_get_by_uuid (history, uuids[i]);

        G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_INVALID_INDEX, "received no value for this index", NULL);

        g_variant_builder_add_value (&builder, g_paste_daemon_methods_item_variant (item));
    }

    return g_variant_builder_end (&builder);
}

/* The pinned items alone. Sifting the history is the daemon's job here rather
 * than each client's: the flag lives on this side, and a favourites view is
 * typically a handful of rows out of a history that is not. */
G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_favourites (const GPasteDaemonMethods *self)
{
    const GPtrArray *items = g_paste_history_get_history (self->history);
    g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_PASTE_ITEMS_VARIANT_TYPE);

    for (guint i = 0; i < items->len; ++i)
    {
        GPasteItem *item = g_ptr_array_index (items, i);

        if (g_paste_item_is_favourite (item))
            g_variant_builder_add_value (&builder, g_paste_daemon_methods_item_variant (item));
    }

    return g_variant_builder_end (&builder);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_history (const GPasteDaemonMethods *self)
{
    return g_paste_daemon_methods_items_variant (g_paste_history_get_history (self->history));
}

G_PASTE_VISIBLE guint64
g_paste_daemon_methods_get_history_size (const GPasteDaemonMethods *self,
                                         const gchar               *name)
{
    if (g_paste_str_equal (name, g_paste_history_get_current (self->history)))
        return g_paste_history_get_length (self->history);

    g_autoptr (GPasteHistory) history = g_paste_history_new (self->settings);

    g_paste_history_load (history, name);

    return g_paste_history_get_length (history);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_image (const GPasteDaemonMethods *self,
                                  const gchar               *uuid,
                                  GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

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

G_PASTE_VISIBLE GStrv
g_paste_daemon_methods_list_histories (const GPasteDaemonMethods *self,
                                       GError                   **error)
{
    g_auto (GStrv) names = g_paste_history_list (self->history, error);

    if (!names)
        return NULL;

    /* The backends answer with the names on disk, which is what everything that
     * opens those files needs, but this answer travels back over the bus as an
     * "as", where a name that is not valid UTF-8 has no place. Such a history
     * could not be switched to either, since that call names it the same way,
     * so leave it out rather than hand back a name nothing can act on. */
    g_autoptr (GStrvBuilder) histories = g_strv_builder_new ();

    for (GStrv name = names; *name; ++name)
    {
        if (!g_utf8_validate (*name, -1, NULL))
            continue;

        g_strv_builder_add (histories, *name);
    }

    return g_strv_builder_end (histories);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_merge (const GPasteDaemonMethods *self,
                              const gchar               *decoration,
                              const gchar               *separator,
                              const gchar * const       *uuids,
                              GError                   **error)
{
    gsize length = (uuids) ? g_strv_length ((GStrv) uuids) : 0;

    G_PASTE_DBUS_ASSERT (length, G_PASTE_ERROR_INVALID_ARGUMENT, "nothing to merge");

    GPasteHistory *history = self->history;
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

    g_paste_daemon_methods_do_add (self, str->str, str->len, error);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_set_active (const GPasteDaemonMethods *self,
                                   gboolean                   active)
{
    g_paste_settings_set_track_changes (self->settings, active);
}

/* The policy itself, in one place: whether the extension coming or going should
 * drive the tracking is the "track-extension-state" key's business. Reached both
 * from the D-Bus method and, without a bus round trip, from a host running the
 * daemon in its own process (see g_paste_daemon_extension_state_changed()). */
G_PASTE_VISIBLE void
g_paste_daemon_methods_extension_state_changed (const GPasteDaemonMethods *self,
                                                gboolean                   state)
{
    if (g_paste_settings_get_track_extension_state (self->settings))
        g_paste_settings_set_track_changes (self->settings, state);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_rename_password (const GPasteDaemonMethods *self,
                                        const gchar               *old_name,
                                        const gchar               *new_name,
                                        GError                   **error)
{
    G_PASTE_DBUS_ASSERT (old_name && *old_name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to rename");

    g_paste_history_rename_password (self->history, old_name, new_name);
}

/* The matches themselves, not their uuids: a caller wanting to show them would
 * only have to ask for every one of them straight back. */
G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_search (const GPasteDaemonMethods *self,
                               const gchar               *query,
                               GError                   **error)
{
    GPasteHistory *history = self->history;
    g_auto (GStrv) results = g_paste_history_search (history, query);

    G_PASTE_DBUS_ASSERT_FULL (results, G_PASTE_ERROR_FAILED, "Error while performing search", NULL);

    g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_PASTE_ITEMS_VARIANT_TYPE);

    for (GStrv uuid = results; *uuid; ++uuid)
    {
        GPasteItem *item = g_paste_history_get_by_uuid (history, *uuid);

        /* The search read the very history we are reading, under the same lock,
         * so a uuid it returned is still there. Skipped rather than asserted
         * on: a missing match is nothing for the caller to act on. */
        if (item)
            g_variant_builder_add_value (&builder, g_paste_daemon_methods_item_variant (item));
    }

    return g_variant_builder_end (&builder);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_set_favourite (const GPasteDaemonMethods *self,
                                      const gchar               *uuid,
                                      gboolean                   favourite,
                                      GError                   **error)
{
    G_PASTE_DBUS_ASSERT (g_paste_history_set_favourite (self->history, uuid, favourite), G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_select (const GPasteDaemonMethods *self,
                               const gchar               *uuid,
                               GError                   **error)
{
    G_PASTE_DBUS_ASSERT (g_paste_history_select (self->history, uuid), G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_replace (const GPasteDaemonMethods *self,
                                const gchar               *uuid,
                                const gchar               *contents,
                                GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
    G_PASTE_DBUS_ASSERT (G_PASTE_IS_TEXT_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "attempted to replace an item other than GPasteTextItem");
    G_PASTE_DBUS_ASSERT (contents && *contents, G_PASTE_ERROR_INVALID_ARGUMENT, "no contents given");

    g_paste_history_replace (self->history, uuid, contents);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_set_password (const GPasteDaemonMethods *self,
                                     const gchar               *uuid,
                                     const gchar               *name,
                                     GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
    G_PASTE_DBUS_ASSERT (G_PASTE_IS_TEXT_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "attempted to replace an item other than GPasteTextItem");
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password name given");
    G_PASTE_DBUS_ASSERT (!g_paste_history_get_password (self->history, name), G_PASTE_ERROR_ALREADY_EXISTS, "a password with that name already exists");

    g_paste_history_set_password (self->history, uuid, name);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_switch_history (const GPasteDaemonMethods *self,
                                       const gchar               *name,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to switch to");

    g_paste_history_switch (self->history, name);
}
