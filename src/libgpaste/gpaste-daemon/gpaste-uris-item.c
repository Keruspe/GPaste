// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-uris-item.h>

struct _GPasteUrisItem
{
    GPasteItem parent_instance;

    GdkFileList *file_list;
};

G_PASTE_DEFINE_TYPE (UrisItem, uris_item, G_PASTE_TYPE_ITEM)

/**
 * g_paste_uris_item_get_file_list:
 * @self: a #GPasteUrisItem instance
 *
 * Get the file list contained in the #GPasteUrisItem
 *
 * Returns: (transfer none): read-only #GdkFileList
 */
G_PASTE_VISIBLE GdkFileList *
g_paste_uris_item_get_file_list (GPasteUrisItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_URIS_ITEM (self), NULL);

    return self->file_list;
}

/**
 * g_paste_uris_item_get_uris:
 * @self: a #GPasteUrisItem instance
 *
 * Get the uris contained in the #GPasteUrisItem, one per file.
 *
 * Read off the #GdkFileList rather than split back out of the item's value: the
 * two say the same thing today (one is built from the other), but the files are
 * what the item holds and the string is only how it is written down. Any scheme
 * can appear here, not just file:, so a caller wanting a path of its own has to
 * ask #GFile for one and be told there is none.
 *
 * Returns: (transfer full): a newly allocated %NULL-terminated array of strings
 */
G_PASTE_VISIBLE GStrv
g_paste_uris_item_get_uris (GPasteUrisItem *self)
{
    g_return_val_if_fail (G_PASTE_IS_URIS_ITEM (self), NULL);

    return g_paste_clipboard_file_list_get_uris (self->file_list);
}

static GPasteItemKind
g_paste_uris_item_get_kind (GPasteItem *self G_GNUC_UNUSED)
{
    return G_PASTE_ITEM_KIND_URIS;
}

static void
g_paste_uris_item_dispose (GObject *object)
{
    GPasteUrisItem *self = G_PASTE_URIS_ITEM (object);

    if (self->file_list)
        g_boxed_free (GDK_TYPE_FILE_LIST, g_steal_pointer (&self->file_list));

    G_OBJECT_CLASS (g_paste_uris_item_parent_class)->dispose (object);
}

static void
g_paste_uris_item_class_init (GPasteUrisItemClass *klass)
{
    GPasteItemClass *item_class = G_PASTE_ITEM_CLASS (klass);

    item_class->get_kind = g_paste_uris_item_get_kind;

    G_OBJECT_CLASS (klass)->dispose = g_paste_uris_item_dispose;
}

static void
g_paste_uris_item_init (GPasteUrisItem *self G_GNUC_UNUSED)
{
}

static GPasteItem *
_g_paste_uris_item_new (const gchar *uris_joined,
                        GdkFileList *file_list)
{
    GPasteItem *item = g_paste_item_new (G_PASTE_TYPE_URIS_ITEM, uris_joined);
    GPasteUrisItem *self = G_PASTE_URIS_ITEM (item);

    /* No display string: a uris item is its uris, and every bit of turning those
     * into a line a user reads -- dropping the file: scheme, shortening $HOME to
     * "~", putting them on one line, saying "[Files]" in front -- belongs to
     * whichever client draws the row. Left here, the shortening ran over the uri
     * rather than over a path and spelled a home file "file://~/a".
     * g_paste_item_get_display_string () falls back to the value. */

    /* (transfer container): the container is ours, the GFiles are not. */
    g_autoptr (GSList) files = gdk_file_list_get_files (file_list);
    guint64 n_uris = g_slist_length (files);
    g_paste_item_add_size (item, strlen (uris_joined) + 1 + n_uris);

    self->file_list = file_list;

    return item;
}

/**
 * g_paste_uris_item_new_from_str:
 * @str: a string containing newline-separated file URIs
 *
 * Create a new instance of #GPasteUrisItem from its string representation
 *
 * Returns: a newly allocated #GPasteUrisItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_uris_item_new_from_str (const gchar *str)
{
    g_return_val_if_fail (str != NULL, NULL);
    g_return_val_if_fail (g_utf8_validate (str, -1, NULL), NULL);

    g_auto (GStrv) uris = g_strsplit (str, "\n", 0);

    if (!*uris)
        return NULL;

    return _g_paste_uris_item_new (str, g_paste_clipboard_file_list_new ((const gchar * const *) uris));
}

/**
 * g_paste_uris_item_new:
 * @file_list: (transfer none): a #GdkFileList from the clipboard
 *
 * Create a new instance of #GPasteUrisItem
 *
 * Returns: a newly allocated #GPasteUrisItem
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteItem *
g_paste_uris_item_new (GdkFileList *file_list)
{
    g_return_val_if_fail (file_list != NULL, NULL);

    g_auto (GStrv) uris = g_paste_clipboard_file_list_get_uris (file_list);

    if (!*uris)
        return NULL;

    g_autofree gchar *uris_joined = g_strjoinv ("\n", uris);

    if (!g_utf8_validate (uris_joined, -1, NULL))
        return NULL;

    return _g_paste_uris_item_new (uris_joined, g_boxed_copy (GDK_TYPE_FILE_LIST, file_list));
}
