// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <string.h>

/* A history is stored in a file its name goes straight into, and in an images
 * directory of the same name, so a name that is a path names something outside
 * the history directory: a backup writes its file there, and a delete sweeps
 * every file of the directory it landed in. Checked here, at the bus, where a
 * caller still gets an error instead of the path builders' critical. */
#define G_PASTE_DBUS_ASSERT_HISTORY_NAME(name)                                    \
    G_PASTE_DBUS_ASSERT (g_paste_util_history_name_is_valid (name),               \
                         G_PASTE_ERROR_INVALID_ARGUMENT,                          \
                         "a history is named, not a path")

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

/* An add answers the uuid of what the history holds the content under, so a
 * refusal has to be an error rather than a quiet success: the daemon's own
 * policy refusing the content is %G_PASTE_ERROR_REJECTED, which says the request
 * was well formed and nothing went wrong -- it just kept nothing. */
static gchar *
g_paste_daemon_methods_do_add_item (const GPasteDaemonMethods *self,
                                    GPasteItem                *item,
                                    GError                   **error)
{
    /* Every item constructor can refuse its input and hand back %NULL. Each
     * caller validates what it passes (that is where a bad input becomes an
     * INVALID_ARGUMENT rather than this), and a constructor that refuses anyway
     * has already logged the reason itself. */
    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_REJECTED, "the content could not be made into an item", NULL);

    /* g_paste_history_add takes ownership; keep our own ref for the checks below */
    g_autoptr (GPasteItem) owned = item;

    /* Content the current selection already says keeps the history as it is,
     * which is what was asked for rather than a refusal: it is in the history
     * already, under a uuid the add can answer with. */
    GPasteItem *current = g_paste_history_get (self->history, 0);

    if (current && g_paste_item_equals (current, item))
    {
        /* Putting the content on the clipboard is the other half of what an add
         * does, and the clipboard is not necessarily on it already: tracking may
         * be off, or the copy that made @current may have been refused a
         * selection. Nothing of ours to take back out if that fails -- @current
         * was in the history before the call and stays as it was. */
        G_PASTE_DBUS_ASSERT_FULL (g_paste_clipboards_manager_select (self->clipboards_manager, current),
                                  G_PASTE_ERROR_FAILED,
                                  "the item could not be put on the clipboard",
                                  NULL);

        return g_strdup (g_paste_item_get_uuid (current));
    }

    g_paste_history_add (self->history, g_object_ref (item));

    /* An item over the memory cap never lands, so @item is not what sits at the
     * front, there is no uuid to answer with -- and nothing of ours to take back
     * out either. */
    G_PASTE_DBUS_ASSERT_FULL (g_paste_history_get (self->history, 0) == item,
                              G_PASTE_ERROR_REJECTED,
                              "the history kept nothing: the content is larger than max-memory-usage",
                              NULL);

    if (!g_paste_clipboards_manager_select (self->clipboards_manager, item))
    {
        g_paste_history_remove (self->history, 0);
        G_PASTE_DBUS_ASSERT_FULL (FALSE, G_PASTE_ERROR_FAILED, "the item could not be put on the clipboard", NULL);
    }

    return g_strdup (g_paste_item_get_uuid (item));
}

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_do_add (const GPasteDaemonMethods *self,
                               const gchar               *text,
                               guint64                    length,
                               GError                   **error)
{
    G_PASTE_DBUS_ASSERT_FULL (text && length, G_PASTE_ERROR_INVALID_ARGUMENT, "no content to add", NULL);

    GPasteSettings *settings = self->settings;
    gboolean trim_items = g_paste_settings_get_trim_items (settings);
    g_autofree gchar *stripped = trim_items ? g_strstrip (g_strdup (text)) : NULL;
    const gchar *to_add = trim_items ? stripped : text;
    /* Measured on what is about to be stored, not on what came in: with trimming
     * on, the two differ by the whitespace, and the caps are a statement about
     * the item the history ends up holding. A line of indentation was enough to
     * carry a too-short paste past min-text-item-size, and to have a paste that
     * fits be refused for exceeding the maximum. */
    guint64 to_add_length = (trim_items) ? strlen (to_add) : length;

    G_PASTE_DBUS_ASSERT_FULL (to_add_length >= g_paste_settings_get_min_text_item_size (settings),
                              G_PASTE_ERROR_REJECTED, "the content is shorter than min-text-item-size", NULL);
    G_PASTE_DBUS_ASSERT_FULL (to_add_length <= g_paste_settings_get_max_text_item_size (settings),
                              G_PASTE_ERROR_REJECTED, "the content is longer than max-text-item-size", NULL);
    G_PASTE_DBUS_ASSERT_FULL (*to_add, G_PASTE_ERROR_REJECTED, "the content is blank once trimmed", NULL);

    return g_paste_daemon_methods_do_add_item (self, g_paste_text_item_new (to_add), error);
}

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_add_text (const GPasteDaemonMethods *self,
                                 const gchar               *text,
                                 GError                   **error)
{
    return g_paste_daemon_methods_do_add (self, text, (text) ? strlen (text) : 0, error);
}

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_add_file (const GPasteDaemonMethods *self,
                                 const gchar               *path,
                                 GError                   **error)
{
    g_autofree gchar *content = NULL;
    gsize length;

    G_PASTE_DBUS_ASSERT_FULL (path && *path, G_PASTE_ERROR_INVALID_ARGUMENT, "no file to add", NULL);

    if (!g_file_get_contents (path, &content, &length, error))
        return NULL;

    if (g_utf8_validate (content, length, NULL))
        return g_paste_daemon_methods_do_add (self, content, length, error);

    g_autoptr (GError) img_error = NULL;
    g_autoptr (GdkTexture) img = gdk_texture_new_from_filename (path, &img_error);

    /* Neither text nor a loadable image: there is nothing to add, and building
     * an item out of a NULL texture would only yield a NULL item the add path
     * then dereferences. */
    if (!img)
    {
        g_warning ("Failed to load image from %s: %s", path, (img_error) ? img_error->message : "unknown error");
        G_PASTE_DBUS_ASSERT_FULL (FALSE, G_PASTE_ERROR_WRONG_ITEM_KIND, "the file is neither text nor an image", NULL);
    }

    return g_paste_daemon_methods_do_add_item (self, g_paste_image_item_new (img), error);
}

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_add_password (const GPasteDaemonMethods *self,
                                     const gchar               *name,
                                     const gchar               *password,
                                     GError                   **error)
{
    G_PASTE_DBUS_ASSERT_FULL (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no name for the password to add", NULL);
    G_PASTE_DBUS_ASSERT_FULL (password && *password, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to add", NULL);

    /* A password's name is what identifies it, so adding one under a name
     * already taken means replacing it rather than ending up with two. The old
     * one goes only once the new one is in: the add is refusable -- the history
     * can keep nothing, and the clipboard can turn the item down -- so dropping
     * it first would answer an error having left neither. By uuid rather than by
     * name, the new item carrying that name by then. */
    GPastePasswordItem *previous = g_paste_history_get_password (self->history, name);
    g_autofree gchar *previous_uuid = (previous) ? g_strdup (g_paste_item_get_uuid (G_PASTE_ITEM (previous))) : NULL;
    g_autofree gchar *uuid = g_paste_daemon_methods_do_add_item (self, g_paste_password_item_new (name, password), error);

    if (!uuid)
        return NULL;

    if (previous_uuid)
        g_paste_history_remove_by_uuid (self->history, previous_uuid);

    return g_steal_pointer (&uuid);
}

/* Whether a history called @name is one the store already holds, in @exists.
 * Asked of the backend rather than tracked, since a history exists exactly as
 * long as its entry in the store does.
 *
 * Returns %FALSE with @error set when the store could not be enumerated at all,
 * which is not the same answer as "no": a caller that must not guess -- a backup,
 * which would otherwise overwrite -- refuses, while one only deciding whether to
 * raise HistoriesChanged carries on and raises it. */
static gboolean
g_paste_daemon_methods_history_exists (const GPasteDaemonMethods *self,
                                       const gchar               *name,
                                       gboolean                  *exists,
                                       GError                   **error)
{
    g_auto (GStrv) histories = g_paste_history_list (self->history, error);

    if (!histories)
        return FALSE;

    *exists = g_strv_contains ((const gchar * const *) histories, name);

    return TRUE;
}

/* The same for the two callers that only want to know whether to announce a new
 * history: a store we could not read is no reason to refuse the operation, and
 * announcing one history too many costs a listener a re-list. */
static gboolean
g_paste_daemon_methods_history_is_new (const GPasteDaemonMethods *self,
                                       const gchar               *name)
{
    g_autoptr (GError) error = NULL;
    gboolean exists = FALSE;

    if (!g_paste_daemon_methods_history_exists (self, name, &exists, &error))
    {
        g_warning ("Could not list the histories: %s", error->message);
        return TRUE;
    }

    return !exists;
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_backup_history (const GPasteDaemonMethods *self,
                                       const gchar               *history,
                                       const gchar               *backup,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT (history && *history, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to backup");
    G_PASTE_DBUS_ASSERT (backup && *backup, G_PASTE_ERROR_INVALID_ARGUMENT, "no name to back the history up under");
    G_PASTE_DBUS_ASSERT_HISTORY_NAME (history);
    G_PASTE_DBUS_ASSERT_HISTORY_NAME (backup);

    /* Refused rather than overwritten: a backup quietly destroying an older one
     * of the same name is a loss noticed far too late to undo. A store we cannot
     * enumerate is refused too -- not knowing is not the same as knowing there
     * is none. */
    gboolean exists;

    if (!g_paste_daemon_methods_history_exists (self, backup, &exists, error))
        return;

    G_PASTE_DBUS_ASSERT (!exists, G_PASTE_ERROR_ALREADY_EXISTS, "a history already goes by that name");

    GPasteSettings *settings = self->settings;

    /* A history of its own, so the backup never disturbs the current one: the
     * current history is not switched away from, and no UI has to be told it
     * was. What did change is the set of histories, which has a signal of its
     * own precisely because a new one appearing is not a switch. */
    g_autoptr (GPasteHistory) _history = g_paste_history_new (settings);

    g_paste_history_load (_history, history);

    /* A history that did not read back leaves an empty model behind, which
     * saved as-is is a valid, empty backup answering OK -- the one answer that
     * makes the user stop worrying about data that is in fact still locked
     * away. Refused instead: the original is untouched either way. */
    G_PASTE_DBUS_ASSERT (!g_paste_history_is_unreadable (_history),
                         G_PASTE_ERROR_FAILED, "the history could not be read, so there is nothing to back up");

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
    G_PASTE_DBUS_ASSERT_HISTORY_NAME (name);

    GPasteHistory *history = self->history;

    /* Nothing was deleted if this fails (the store is being handed over), so
     * neither announce the deletion nor switch away from a history that is
     * still there; the caller turns @error into the method's reply. */
    if (!g_paste_history_delete (history, name, error))
        return;

    g_paste_daemon3_emit_raw_history_deleted (self->skeleton, name);
    /* The set of histories is one shorter: anything merely listing them has no
     * use for the name, and would otherwise have to know about this signal. */
    g_paste_daemon3_emit_raw_histories_changed (self->skeleton);

    if (g_paste_str_equal (name, g_paste_history_get_current (self->history)))
        g_paste_history_switch (history, G_PASTE_DEFAULT_HISTORY);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_password (const GPasteDaemonMethods *self,
                                        const gchar               *name,
                                        GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to delete");

    G_PASTE_DBUS_ASSERT (g_paste_history_delete_password (self->history, name),
                         G_PASTE_ERROR_NOT_FOUND, "no password goes by that name");
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_empty_history (const GPasteDaemonMethods *self,
                                      const gchar               *name,
                                      GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to empty");
    G_PASTE_DBUS_ASSERT_HISTORY_NAME (name);

    /* Emptying a history there is none of writes one, empty: that is the set of
     * histories growing, which anything listing them has to hear about. Asked
     * before the write, since afterwards it always exists -- and the current
     * history is no exception, since emptying it saves, which brings its file
     * into existence just the same. */
    gboolean is_new = g_paste_daemon_methods_history_is_new (self, name);

    if (g_paste_str_equal (name, g_paste_history_get_current (self->history)))
        g_paste_history_empty (self->history);
    else
    {
        g_autoptr (GPasteHistory) history = g_paste_history_new (self->settings);

        g_paste_history_save (history, name);

        /* Emptying the current history drops each item's backing file with it;
         * writing an empty one over another history has to sweep them by hand,
         * or emptying would mean two different things depending on which history
         * was named -- and leave the clipboard's screenshots, unencrypted under
         * the plain flavour, on disk for good. Best effort, as in the delete
         * path: the history itself is already empty. */
        g_autoptr (GError) images_error = NULL;

        if (!g_paste_storage_backend_delete_history_images (name, &images_error))
            g_warning ("Could not delete the images of \"%s\": %s", name, images_error->message);
    }

    if (is_new)
        g_paste_daemon3_emit_raw_histories_changed (self->skeleton);

    g_paste_daemon3_emit_raw_history_emptied (self->skeleton, name);
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

        G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);

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

/* The current history's own length, and nothing else's: the sizes of the other
 * histories ride along with ListHistories, which answers all of them at once
 * rather than making a sidebar pay one load per row. */
G_PASTE_VISIBLE guint64
g_paste_daemon_methods_get_history_size (const GPasteDaemonMethods *self)
{
    return g_paste_history_get_length (self->history);
}

G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_get_image (const GPasteDaemonMethods *self,
                                  const gchar               *uuid,
                                  GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);
    G_PASTE_DBUS_ASSERT_FULL (G_PASTE_IS_IMAGE_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "Provided uuid doesn't match an image item.", NULL);

    /* Hand the bytes over so clients never go looking for a file themselves:
     * how and where the image is stored stays the daemon's business. The item
     * carries its PNG when it came from a blob-storing backend (or a fresh
     * capture); one read off a cache file reads that file. */
    GPasteImageItem *image = G_PASTE_IMAGE_ITEM (item);
    GBytes *png = g_paste_image_item_get_png_bytes (image);
    g_autoptr (GBytes) bytes = NULL;

    if (png)
        bytes = g_bytes_ref (png);
    else
    {
        const gchar *cache_path = g_paste_image_item_get_cache_path (image);
        gchar *data = NULL;
        gsize length = 0;

        G_PASTE_DBUS_ASSERT_FULL (cache_path, G_PASTE_ERROR_FAILED, "This item's image is nowhere to be read from.", NULL);

        if (!g_file_get_contents (cache_path, &data, &length, error))
            return NULL;

        bytes = g_bytes_new_take (data, length);
    }

    return g_variant_new_from_bytes (G_VARIANT_TYPE ("ay"), bytes, TRUE);
}

G_PASTE_VISIBLE GStrv
g_paste_daemon_methods_get_uris (const GPasteDaemonMethods *self,
                                 const gchar               *uuid,
                                 GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);
    G_PASTE_DBUS_ASSERT_FULL (G_PASTE_IS_URIS_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "Provided uuid doesn't match a uris item.", NULL);

    return g_paste_uris_item_get_uris (G_PASTE_URIS_ITEM (item));
}

/* Every history with how many items it holds, in the order the store lists them,
 * which is the order such a listing is drawn in. The size rides along because
 * everything listing histories draws their sizes beside them, and asking one at
 * a time meant one full load of one history apiece. */
G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_list_histories (const GPasteDaemonMethods *self,
                                       GError                   **error)
{
    g_auto (GStrv) names = g_paste_history_list (self->history, error);

    if (!names)
        return NULL;

    g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (G_PASTE_HISTORIES_VARIANT_TYPE);

    for (GStrv name = names; *name; ++name)
    {
        /* The backends answer with the names on disk, which is what everything
         * that opens those files needs, but this answer travels back over the
         * bus, where a name that is not valid UTF-8 has no place. Such a history
         * could not be switched to either, since that call names it the same
         * way, so leave it out rather than hand back a name nothing can act
         * on. */
        if (!g_utf8_validate (*name, -1, NULL))
            continue;

        g_variant_builder_add (&builder, G_PASTE_HISTORY_VARIANT_STRING,
                               *name,
                               g_paste_history_get_size (self->history, *name));
    }

    return g_variant_builder_end (&builder);
}

/* Daemon-side because it has to be: what gets joined is each item's real value,
 * and the only string an item travels over the bus with is the one a user is
 * shown. */
G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_merge (const GPasteDaemonMethods *self,
                              const gchar               *decoration,
                              const gchar               *separator,
                              const gchar * const       *uuids,
                              GError                   **error)
{
    gsize length = (uuids) ? g_strv_length ((GStrv) uuids) : 0;

    G_PASTE_DBUS_ASSERT_FULL (length, G_PASTE_ERROR_INVALID_ARGUMENT, "nothing to merge", NULL);

    GPasteHistory *history = self->history;
    g_autoptr (GString) str = g_string_new (NULL);

    for (gsize i = 0; i < length; ++i)
    {
        GPasteItem *item = g_paste_history_get_by_uuid (history, uuids[i]);

        G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "no item matching this uuid", NULL);

        g_string_append_printf (str, "%s%s%s%s",
                                (i) ? separator : "",
                                decoration,
                                g_paste_item_get_value (item),
                                decoration);
    }

    return g_paste_daemon_methods_do_add (self, str->str, str->len, error);
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
    G_PASTE_DBUS_ASSERT (new_name && *new_name, G_PASTE_ERROR_INVALID_ARGUMENT, "no name to rename the password to");

    G_PASTE_DBUS_ASSERT (g_paste_history_rename_password (self->history, old_name, new_name),
                         G_PASTE_ERROR_NOT_FOUND, "no password goes by that name");
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

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_replace (const GPasteDaemonMethods *self,
                                const gchar               *uuid,
                                const gchar               *contents,
                                GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);
    G_PASTE_DBUS_ASSERT_FULL (G_PASTE_IS_TEXT_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "attempted to replace an item other than GPasteTextItem", NULL);
    G_PASTE_DBUS_ASSERT_FULL (contents && *contents, G_PASTE_ERROR_INVALID_ARGUMENT, "no contents given", NULL);

    /* The checks above make this unreachable, but the reply is built from what
     * comes back and a %NULL there is not a reply at all. */
    gchar *new_uuid = g_paste_history_replace (self->history, uuid, contents);

    G_PASTE_DBUS_ASSERT_FULL (new_uuid, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);

    return new_uuid;
}

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_make_password (const GPasteDaemonMethods *self,
                                      const gchar               *uuid,
                                      const gchar               *name,
                                      GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);
    G_PASTE_DBUS_ASSERT_FULL (G_PASTE_IS_TEXT_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "attempted to replace an item other than GPasteTextItem", NULL);
    G_PASTE_DBUS_ASSERT_FULL (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password name given", NULL);
    G_PASTE_DBUS_ASSERT_FULL (!g_paste_history_get_password (self->history, name), G_PASTE_ERROR_ALREADY_EXISTS, "a password with that name already exists", NULL);

    /* As in g_paste_daemon_methods_replace(): unreachable, and not a reply. */
    gchar *new_uuid = g_paste_history_set_password (self->history, uuid, name);

    G_PASTE_DBUS_ASSERT_FULL (new_uuid, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);

    return new_uuid;
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_switch_history (const GPasteDaemonMethods *self,
                                       const gchar               *name,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to switch to");
    G_PASTE_DBUS_ASSERT_HISTORY_NAME (name);

    /* Switching to a name no history goes by creates it: that is how a new
     * history comes into being, there being no CreateHistory of its own, and it
     * is the set of histories growing just as much as a backup is. */
    gboolean is_new = g_paste_daemon_methods_history_is_new (self, name);

    g_paste_history_switch (self->history, name);

    if (is_new)
        g_paste_daemon3_emit_raw_histories_changed (self->skeleton);
}
