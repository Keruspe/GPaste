// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#ifdef G_PASTE_ENABLE_ENCRYPTION
#define GCR_API_SUBJECT_TO_CHANGE
#include <gcr/gcr.h>

#include <gpaste-daemon/gpaste-secret-stream-converter.h>
#endif

typedef struct
{
    /* When set (in gcr secure memory), the history is encrypted on disk: the
     * file streams are wrapped with a secretstream converter, the ".xmls"
     * extension is used, and password entries are persisted (encrypted) rather
     * than skipped. NULL means a plain ".xml" history. */
    gchar *passphrase;
} GPasteFileBackendPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (FileBackend, file_backend, G_PASTE_TYPE_STORAGE_BACKEND)

/* The passphrase, or NULL for a plain history. Safe to call whether or not
 * encryption was built in (it is always NULL without it). */
static const gchar *
g_paste_file_backend_get_passphrase (GPasteStorageBackend *self)
{
    const GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (G_PASTE_FILE_BACKEND (self));

    return priv->passphrase;
}

static gboolean g_paste_file_backend_load_contents (GPasteStorageBackend *self,
                                                    const gchar          *history_file_path,
                                                    GFile                *history_file,
                                                    gchar                **text,
                                                    gsize                *text_length,
                                                    GError               **error);

static gboolean
_g_paste_file_backend_write_password_name (GOutputStream      *stream,
                                           GPastePasswordItem *item,
                                           GError            **error)
{
    /* An attribute value, not a CDATA payload: g_paste_util_xml_encode() escapes
     * "&" and ">", which is what keeps a "]]>" out of a CDATA section, and
     * leaves the quote that delimits an attribute alone. A password name is
     * whatever MakePassword was handed, so a quote in one closed the attribute
     * early -- forging the ones that follow, or breaking the document outright
     * so the whole history stopped being readable. g_markup_escape_text() is
     * the attribute's escaping, and the entities it writes are the ones
     * GMarkup gives back on read. */
    g_autofree gchar *name = g_markup_escape_text (g_paste_password_item_get_name (item), -1);

    return g_output_stream_write_all (stream, "\" name=\"", 8, NULL, NULL /* cancellable */, error) &&
           g_output_stream_write_all (stream, name, strlen (name), NULL, NULL /* cancellable */, error);
}

/**
 * g_paste_file_backend_images_dir:
 * @history_name: the name of a history
 *
 * Get the directory @history_name's image files live in. This is the one owner
 * of the images/<history_name>/ layout: per history, so the same image copied
 * in several histories gets one file each and evicting it from one never
 * breaks the others.
 *
 * Returns: the images directory path
 */
G_PASTE_VISIBLE gchar *
g_paste_file_backend_images_dir (const gchar *history_name)
{
    /* The name is a path component here just as it is in the history file's own
     * name: one that traverses out would have the images sweep delete the
     * contents of whichever directory it landed in. */
    g_return_val_if_fail (g_paste_util_history_name_is_valid (history_name), NULL);

    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();

    return g_build_filename (history_dir, "images", history_name, NULL);
}

/**
 * g_paste_file_backend_image_path:
 * @history_name: the name of a history
 * @checksum: the image's SHA256 checksum, which is the image item's value
 *
 * Get the file this backend materializes an image in for @history_name:
 * <history-dir>/images/<history_name>/<checksum>.png. Derived rather than
 * remembered, so an item written under another history's name (a backup) lands
 * in that history's own directory without the item itself moving.
 *
 * Returns: the canonical cache path
 */
G_PASTE_VISIBLE gchar *
g_paste_file_backend_image_path (const gchar *history_name,
                                 const gchar *checksum)
{
    g_return_val_if_fail (history_name, NULL);
    g_return_val_if_fail (checksum, NULL);

    g_autofree gchar *images_dir = g_paste_file_backend_images_dir (history_name);
    g_autofree gchar *filename = g_strconcat (checksum, ".png", NULL);

    return g_build_filename (images_dir, filename, NULL);
}

/**
 * g_paste_file_backend_encrypted_path:
 * @path: the canonical (plain) cache path of an image
 *
 * Get the path of the encrypted side file the encrypted flavour materializes
 * for @path (".pngs", mirroring ".xml"/".xmls"). This is the one owner of that
 * naming scheme.
 *
 * Returns: the encrypted side file path
 */
G_PASTE_VISIBLE gchar *
g_paste_file_backend_encrypted_path (const gchar *path)
{
    g_return_val_if_fail (path, NULL);

    return g_strconcat (path, "s", NULL);
}

/**
 * g_paste_file_backend_delete_image:
 * @path: the canonical (plain) cache path of an image
 *
 * Delete an image's materialized data: the cache file at @path and its
 * encrypted side file. Only one of the two was ever written -- which one is the
 * flavour's business -- so the absent one is fine.
 */
G_PASTE_VISIBLE void
g_paste_file_backend_delete_image (const gchar *path)
{
    g_return_if_fail (path);

    g_autofree gchar *encrypted_path = g_paste_file_backend_encrypted_path (path);
    const gchar *paths[] = { path, encrypted_path };

    for (guint64 i = 0; i < G_N_ELEMENTS (paths); ++i)
    {
        g_autoptr (GFile) image = g_file_new_for_path (paths[i]);
        g_autoptr (GError) error = NULL;

        if (!g_file_delete (image, NULL, &error) &&
            !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            g_warning ("Failed to delete image file: %s", (error) ? error->message : "unknown error");
    }
}

/* The XML history references images by their canonical <checksum>.png path
 * (@reference, derived from the item's checksum under the history being
 * written, which is the backup's own when writing under another name); writing
 * the image data itself is this backend's job -- items do not touch the disk at
 * capture. The plain
 * flavor writes that file as-is; the encrypted one writes a "<checksum>.pngs"
 * sibling through the same stream converter as the history, so no pixel ever
 * reaches the disk in clear. Best effort: a failure only costs this image, not
 * the write. */
static void
_g_paste_file_backend_ensure_image_file (GPasteFileBackend *self,
                                         GPasteImageItem   *item,
                                         const gchar       *reference)
{
    gboolean encrypted = g_paste_storage_backend_is_encrypted (G_PASTE_STORAGE_BACKEND ((gpointer) self));
    g_autofree gchar *target = (encrypted) ? g_paste_file_backend_encrypted_path (reference) : g_strdup (reference);

    if (g_file_test (target, G_FILE_TEST_EXISTS))
        return;

    GBytes *png = g_paste_image_item_get_png_bytes (item);
    g_autoptr (GBytes) read_back = NULL;

    /* An item read by path carries no bytes (e.g. imported from the plain
     * flavor into the encrypted one): take them from the file it was read
     * from. An item with neither has nothing to write here. */
    if (!png)
    {
        const gchar *cache_path = g_paste_image_item_get_cache_path (item);
        gchar *data = NULL;
        gsize length = 0;

        /* Unreadable cache file: nothing to write. Best effort, as documented
         * above -- it costs this one image, not the history write. */
        if (!cache_path || !g_file_get_contents (cache_path, &data, &length, NULL))
            return;

        png = read_back = g_bytes_new_take (data, length);
    }

    g_autofree gchar *images_dir = g_path_get_dirname (target);
    g_autoptr (GFile) dir = g_file_new_for_path (images_dir);
    g_autoptr (GError) error = NULL;

    if (!g_file_make_directory_with_parents (dir, NULL, &error) &&
        !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    {
        g_warning ("Failed to create images directory: %s", error->message);
        return;
    }

    g_clear_error (&error);

    /* get_output_stream wraps the file with the encryption converter exactly
     * when this backend has a passphrase, matching the target chosen above. */
    g_autoptr (GFile) target_file = g_file_new_for_path (target);
    g_autoptr (GOutputStream) stream = G_PASTE_FILE_BACKEND_GET_CLASS (self)->get_output_stream (self, target_file);

    if (!stream)
        return;

    gsize length;
    gconstpointer data = g_bytes_get_data (png, &length);

    if (!g_output_stream_write_all (stream, data, length, NULL, NULL /* cancellable */, &error) ||
        !g_output_stream_close (stream, NULL /* cancellable */, &error))
        g_warning ("Failed to materialize image to %s: %s", target, error->message);
}

/* For the encrypted flavor, load and decrypt an image's "<checksum>.pngs"
 * side file. NULL when this backend reads images by path (plain flavor) or
 * there is no side file (yet) for @path. */
static GBytes *
_g_paste_file_backend_load_image_bytes (GPasteStorageBackend *self,
                                        const gchar          *path)
{
#ifdef G_PASTE_ENABLE_ENCRYPTION
    if (!g_paste_file_backend_get_passphrase (self))
        return NULL;

    g_autofree gchar *encrypted_path = g_paste_file_backend_encrypted_path (path);
    g_autoptr (GFile) encrypted_file = g_file_new_for_path (encrypted_path);

    if (!g_file_query_exists (encrypted_file,
                              NULL)) /* cancellable */
        return NULL;

    g_autofree gchar *data = NULL;
    gsize length = 0;
    g_autoptr (GError) error = NULL;

    if (!g_paste_file_backend_load_contents (self, encrypted_path, encrypted_file, &data, &length, &error))
    {
        g_warning ("Failed to load image from %s: %s", encrypted_path, error->message);
        return NULL;
    }

    return g_bytes_new_take (g_steal_pointer (&data), length);
#else
    (void) self;
    (void) path;

    return NULL;
#endif
}

static gboolean
_g_paste_file_backend_write_image_metadata (GOutputStream   *stream,
                                            GPasteImageItem *item,
                                            GError         **error)
{
    g_autofree gchar *date_str = g_date_time_format ((GDateTime *) g_paste_image_item_get_date (item), "%s");
    const gchar *checksum = g_paste_image_item_get_checksum (item);

    if (!g_output_stream_write_all (stream, "\" date=\"", 8, NULL, NULL /* cancellable */, error) ||
        !g_output_stream_write_all (stream, date_str, strlen (date_str), NULL, NULL /* cancellable */, error))
        return FALSE;

    /* The checksum (hex SHA256) needs no XML escaping */
    if (checksum &&
        (!g_output_stream_write_all (stream, "\" checksum=\"", 12, NULL, NULL /* cancellable */, error) ||
         !g_output_stream_write_all (stream, checksum, strlen (checksum), NULL, NULL /* cancellable */, error)))
        return FALSE;

    return TRUE;
}

static gboolean
_g_paste_file_backend_write_special_values (GOutputStream *stream,
                                            const GSList  *special_values,
                                            GError       **error)
{
    for (const GSList *val = special_values; val; val = val->next)
    {
        GPasteBinaryData *value = val->data;
        GEnumValue *gev = g_enum_get_value (g_type_class_peek (G_PASTE_TYPE_SPECIAL_ATOM), g_paste_binary_data_get_mime (value));

        /* Skip a value carrying an unknown atom rather than dereferencing NULL,
         * as the sqlite backend does — writing the item is worth more than the
         * one representation we cannot name. */
        if (!gev)
        {
            g_warning ("Unknown mime: %d", g_paste_binary_data_get_mime (value));
            continue;
        }

        const gchar *mime = gev->value_nick;
        g_autofree gchar *b64 = g_paste_binary_data_to_base64 (value);
        g_autofree gchar *text = g_paste_util_xml_encode (b64);

        if (!g_output_stream_write_all (stream, "    <value mime=\"", 17, NULL, NULL /* cancellable */, error) ||
            !g_output_stream_write_all (stream, mime, strlen (mime), NULL, NULL /* cancellable */, error) ||
            !g_output_stream_write_all (stream, "\"><![CDATA[", 11, NULL, NULL /* cancellable */, error) ||
            !g_output_stream_write_all (stream, text, strlen (text), NULL, NULL /* cancellable */, error) ||
            !g_output_stream_write_all (stream, "]]></value>\n", 12, NULL, NULL /* cancellable */, error))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static void
g_paste_file_backend_write_history_file (GPasteStorageBackend *self,
                                         const gchar          *history_name,
                                         const GList          *history)
{
    if (!g_paste_util_ensure_history_dir_exists ())
        return;

    /* Images are referenced (and materialized) under the history being
     * written, which may not be the one the items belong to (a backup). */
    g_autofree gchar *history_file_path = g_paste_storage_backend_get_history_file_path (self, history_name);
    g_autoptr (GFile) history_file = g_file_new_for_path (history_file_path);

    GPasteFileBackend *real_self = G_PASTE_FILE_BACKEND (self);
    /* An encrypted history keeps password entries (the file is unreadable
     * without the passphrase) and persists their real value, not the mask. */
    gboolean encrypted = g_paste_storage_backend_is_encrypted (self);

    g_autofree gchar *tmp_path = g_strconcat (history_file_path, ".tmp", NULL);
    g_autoptr (GFile) tmp_file = g_file_new_for_path (tmp_path);
    g_autoptr (GOutputStream) stream = G_PASTE_FILE_BACKEND_GET_CLASS (real_self)->get_output_stream (real_self, tmp_file);

    if (!stream)
        return;

    gboolean success = TRUE;
    g_autoptr (GError) error = NULL;

    if (!g_output_stream_write_all (stream, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", 39, NULL, NULL /* cancellable */, &error) ||
        !g_output_stream_write_all (stream, "<history version=\"2.0\">\n", 24, NULL, NULL /* cancellable */, &error))
    {
        g_warning ("Failed to write history header: %s", error->message);
        g_clear_error (&error);
        success = FALSE;
    }

    for (const GList *h = history; success && h; h = g_list_next (h))
    {
        GPasteItem *item = h->data;
        GPasteItemKind kind = g_paste_item_get_kind (item);
        const gchar *uuid = g_paste_item_get_uuid (item);

        if (!encrypted && kind == G_PASTE_ITEM_KIND_PASSWORD)
            continue;

        const gchar *kind_str = g_paste_item_kind_to_string (kind);

        g_autofree gchar *image_reference = NULL;

        if (G_PASTE_IS_IMAGE_ITEM (item))
        {
            GPasteImageItem *image = G_PASTE_IMAGE_ITEM (item);

            /* Reference the image under the history being written, wherever
             * the (possibly shared, possibly live) item was read from: a backup
             * owns its images instead of pointing into its source's directory,
             * and legacy shared-directory entries migrate to their history's
             * own on the next save. Derived from the checksum, which every
             * image item has -- building one is what computes it. */
            image_reference = g_paste_file_backend_image_path (history_name, g_paste_image_item_get_checksum (image));
            _g_paste_file_backend_ensure_image_file (real_self, image, image_reference);
        }

        const GSList *special_values = g_paste_item_get_special_values (item);
        /* An image is written as the file this backend materializes it in --
         * its value, the checksum, rides along as an attribute -- so a history
         * file keeps naming the files beside it. For everything else the item's
         * own content is the text: get_value only differs from get_real_value
         * for passwords (it masks them), and those are skipped above unless
         * encrypted, so the real value is always what we want to persist. */
        g_autofree gchar *text = g_paste_util_xml_encode ((image_reference) ? image_reference : g_paste_item_get_real_value (item));

        if (!g_output_stream_write_all (stream, "  <item kind=\"", 14, NULL, NULL /* cancellable */, &error) ||
            !g_output_stream_write_all (stream, kind_str, strlen (kind_str), NULL, NULL /* cancellable */, &error) ||
            !g_output_stream_write_all (stream, "\" uuid=\"", 8, NULL, NULL /* cancellable */, &error) ||
            !g_output_stream_write_all (stream, uuid, strlen (uuid), NULL, NULL /* cancellable */, &error) ||
            (G_PASTE_IS_PASSWORD_ITEM (item) && !_g_paste_file_backend_write_password_name (stream, G_PASTE_PASSWORD_ITEM (item), &error)) ||
            (G_PASTE_IS_IMAGE_ITEM (item) && !_g_paste_file_backend_write_image_metadata (stream, G_PASTE_IMAGE_ITEM (item), &error)) ||
            /* Written only when set, so an ordinary history's file is unchanged
             * by the attribute's existence. */
            (g_paste_item_is_favourite (item) && !g_output_stream_write_all (stream, "\" favourite=\"true", 17, NULL, NULL /* cancellable */, &error)) ||
            !g_output_stream_write_all (stream, "\">\n    <value><![CDATA[", 23, NULL, NULL /* cancellable */, &error) ||
            !g_output_stream_write_all (stream, text, strlen (text), NULL, NULL /* cancellable */, &error) ||
            !g_output_stream_write_all (stream, "]]></value>\n", 12, NULL, NULL /* cancellable */, &error) ||
            (special_values && !_g_paste_file_backend_write_special_values (stream, special_values, &error)) ||
            !g_output_stream_write_all (stream, "  </item>\n", 10, NULL, NULL /* cancellable */, &error))
        {
            g_warning ("Failed to write an item to history: %s", error->message);
            g_clear_error (&error);
            success = FALSE;
        }
    }

    if (success &&
        !g_output_stream_write_all (stream, "</history>\n", 11, NULL, NULL /* cancellable */, &error))
    {
        g_warning ("Failed to write history footer: %s", error->message);
        g_clear_error (&error);
        success = FALSE;
    }

    if (success && !g_output_stream_close (stream, NULL /* cancellable */, &error))
    {
        g_warning ("Failed to close history temp file: %s", error->message);
        g_clear_error (&error);
        success = FALSE;
    }

    if (success && !g_file_move (tmp_file, history_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error))
    {
        g_warning ("Failed to install history file: %s", error->message);
        g_clear_error (&error);
        success = FALSE;
    }

    if (!success)
    {
        if (!g_file_delete (tmp_file, NULL, &error) &&
            !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
            g_warning ("Failed to delete history temp file: %s", error->message);
        }
    }
}

/********************/
/* Begin XML Parser */
/********************/

typedef enum
{
    BEGIN,
    IN_HISTORY,
    IN_ITEM,
    IN_VALUE,
    IN_VALUE_WITH_TEXT,
    END
} State;

/* The "kind" attribute is the #GPasteItemKind nick, so parsing it back is
 * g_paste_item_kind_from_string(). %G_PASTE_ITEM_KIND_INVALID covers the item
 * with no usable kind — missing, or naming one this version does not know — and
 * is reset per item, so such an item never makes the next one, which may have
 * no kind of its own, inherit its type. */

/* 2.0 is the only format read. 1.0 -- which held an item's value as text
 * directly inside <item> rather than in a <value> child -- was dropped: it is
 * refused like any other unreadable history, which leaves the file untouched
 * rather than overwriting it, so an ancient history can still be recovered with
 * an older GPaste. */
typedef enum
{
    HISTORY_2_0,
    HISTORY_INVALID = -1
} HistoryVersion;

typedef struct
{
    GPasteStorageBackend *backend;
    const gchar          *history_file_path;
    GList                *history;
    gsize                 mem_size;
    State                 state;
    GPasteItemKind        type;
    guint64               current_size;
    guint64               max_size;
    gboolean              images_support;
    /* Counting only: no item is built, no image is materialized (nor deleted),
     * and @count carries the answer instead of @history. Same parser and same
     * gates as a read, so the two cannot drift apart. */
    gboolean              count_only;
    gsize                 count;
    gboolean              favourite;
    gchar                *uuid;
    gchar                *date;
    gchar                *checksum;
    gchar                *name;
    gchar                *text;
    GSList               *special_values;
    HistoryVersion        version;
    GPasteSpecialAtom     mime;
} Data;

/* Where the parser currently is, for a diagnostic. An encrypted history is
 * decrypted into memory before being parsed, so the file on disk has neither
 * these lines nor these offsets; the element we are inside and where it opened
 * are what identify the offending item whichever buffer we ended up parsing. */
static gchar *
parse_location (GMarkupParseContext *context,
                const Data          *data)
{
    gint line_number, char_number;
    const gchar *element = g_markup_parse_context_get_element (context);

    g_markup_parse_context_get_position (context, &line_number, &char_number);

    /* Where we are, said once; being inside an element only adds to it. */
    g_autofree gchar *where = g_strdup_printf ("in file “%s” at line %" G_GINT32_FORMAT
                                               ", column %" G_GINT32_FORMAT " (byte %" G_GSIZE_FORMAT ")",
                                               data->history_file_path, line_number, char_number,
                                               g_markup_parse_context_get_offset (context));

    if (!element)
        return g_steal_pointer (&where);

    gsize tag_line, tag_char, tag_offset;

    g_markup_parse_context_get_tag_start (context, &tag_line, &tag_char, &tag_offset);

    return g_strdup_printf ("%s, inside <%s> opened at line %" G_GSIZE_FORMAT " (byte %" G_GSIZE_FORMAT ")",
                            where, element, tag_line, tag_offset);
}

/* g_warning() with the parse location appended. Needs @context and @data in
 * scope, which every parser callback has. */
#define WARN_AT(fmt, ...)                                            \
    do {                                                             \
        g_autofree gchar *location = parse_location (context, data); \
        g_warning (fmt " %s.", ##__VA_ARGS__, location);             \
    } while (0)

#define ASSERT_STATE_FULL(cond, x)                                                       \
    if (!(cond))                                                                         \
    {                                                                                    \
        WARN_AT ("Expected state %" G_GINT32_FORMAT ", but got %" G_GINT32_FORMAT,       \
                 x, data->state);                                                        \
        return;                                                                          \
    }
#define ASSERT_STATE(x) ASSERT_STATE_FULL (data->state == (x), x)
#define SWITCH_STATE(x, y) \
    do {                   \
        ASSERT_STATE (x);  \
        data->state = y;   \
    } while (0)
/* Only a non-empty text run moves an element to its ..._WITH_TEXT state, so an
 * element that holds nothing (or only whitespace) is still in @empty when its
 * end tag arrives. Accept both: refusing the empty one would leave the state
 * machine stuck inside that element and silently drop every remaining item. */
#define SWITCH_STATE_OR_EMPTY(x, empty, y)                                        \
    do {                                                                          \
        ASSERT_STATE_FULL (data->state == (x) || data->state == (empty), x);      \
        data->state = y;                                                          \
    } while (0)

static gboolean
history_contains_uuid (const GList *history,
                       const gchar *uuid)
{
    for (; history; history = g_list_next (history))
    {
        GPasteItem *item = history->data;

        if (g_paste_str_equal (g_paste_item_get_uuid (item), uuid))
            return TRUE;
    }

    return FALSE;
}

static void
start_tag (GMarkupParseContext *context,
           const gchar         *element_name,
           const gchar        **attribute_names,
           const gchar        **attribute_values,
           gpointer             user_data,
           GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    if (g_paste_str_equal (element_name, "history"))
    {
        SWITCH_STATE (BEGIN, IN_HISTORY);
        for (const gchar **a = attribute_names, **v = attribute_values; *a && *v; ++a, ++v)
        {
            if (g_paste_str_equal (*a, "version"))
            {
                if (g_paste_str_equal (*v, "2.0"))
                    data->version = HISTORY_2_0;
                else
                {
                    /* Name 1.0 rather than calling it unknown: it is a history
                     * we once wrote, and the file is left intact, so say what
                     * would get it back. */
                    if (g_paste_str_equal (*v, "1.0"))
                        WARN_AT ("History version 1.0 is no longer supported; the file is left untouched, load it with GPaste 2 to convert it");
                    else
                        WARN_AT ("Unknown history version: %s", *v);
                    data->version = HISTORY_INVALID;
                }
            }
        }
    }
    else if (g_paste_str_equal (element_name, "item"))
    {
        SWITCH_STATE (IN_HISTORY, IN_ITEM);
        data->type = G_PASTE_ITEM_KIND_INVALID;
        data->favourite = FALSE;
        g_clear_pointer (&data->uuid, g_free);
        g_clear_pointer (&data->date, g_free);
        g_clear_pointer (&data->checksum, g_free);
        g_clear_pointer (&data->name, g_free);
        g_clear_pointer (&data->text, g_free);
        g_clear_slist (&data->special_values, g_object_unref);
        for (const gchar **a = attribute_names, **v = attribute_values; *a && *v; ++a, ++v)
        {
            if (g_paste_str_equal (*a, "kind"))
            {
                data->type = g_paste_item_kind_from_string (*v);
                if (data->type == G_PASTE_ITEM_KIND_INVALID)
                    WARN_AT ("Unknown item kind: %s", *v);
            }
            else if (g_paste_str_equal (*a, "uuid"))
            {
                if (g_uuid_string_is_valid (*v) && !history_contains_uuid (data->history, *v))
                    data->uuid = g_strdup (*v);
            }
            /* An attribute that does not belong to this kind is skipped, not a
             * reason to abandon the item: returning here would also drop the
             * attributes that follow it (the uuid among them). */
            else if (g_paste_str_equal (*a, "date"))
            {
                if (data->type != G_PASTE_ITEM_KIND_IMAGE)
                {
                    WARN_AT ("Expected an Image item, but got a %s one", g_paste_item_kind_to_string (data->type));
                    continue;
                }
                data->date = g_strdup (*v);
            }
            else if (g_paste_str_equal (*a, "checksum"))
            {
                if (data->type != G_PASTE_ITEM_KIND_IMAGE)
                {
                    WARN_AT ("Expected an Image item, but got a %s one", g_paste_item_kind_to_string (data->type));
                    continue;
                }
                data->checksum = g_strdup (*v);
            }
            else if (g_paste_str_equal (*a, "name"))
            {
                if (data->type != G_PASTE_ITEM_KIND_PASSWORD)
                {
                    WARN_AT ("Expected a Password item, but got a %s one", g_paste_item_kind_to_string (data->type));
                    continue;
                }
                data->name = g_strdup (*v);
            }
            else if (g_paste_str_equal (*a, "favourite"))
                data->favourite = g_paste_str_equal (*v, "true");
            else
                WARN_AT ("Unknown item attribute: %s", *a);
        }
    }
    else if (g_paste_str_equal (element_name, "value"))
    {
        SWITCH_STATE (IN_ITEM, IN_VALUE);
        data->mime = G_PASTE_SPECIAL_ATOM_INVALID;
        for (const gchar **a = attribute_names, **v = attribute_values; *a && *v; ++a, ++v)
        {
            if (g_paste_str_equal (*a, "mime"))
            {
                GEnumValue *gev = g_enum_get_value_by_nick (g_type_class_peek (G_PASTE_TYPE_SPECIAL_ATOM), *v);
                if (gev)
                    data->mime = gev->value;
                else
                    WARN_AT ("Unknown mime: %s", *v);
            }
        }
    }
    else
        WARN_AT ("Unknown element: %s", element_name);
}

static void
add_item (Data *data)
{
    GPasteItem *item = NULL;

    if (data->count_only)
    {
        /* What the switch below would have built, decided rather than done: an
         * item needs a value and a usable kind, and an image needs images turned
         * on and a date to restore. The one thing not foreseen here is a value a
         * constructor rejects on inspection (a colour that does not parse), so a
         * corrupt history can count one high -- the read warns about it. */
        gboolean countable = data->text && data->type != G_PASTE_ITEM_KIND_INVALID &&
                             (data->type != G_PASTE_ITEM_KIND_IMAGE || (data->images_support && data->date));

        if (countable)
        {
            ++data->count;

            if (!data->favourite)
                ++data->current_size;
        }

        g_slist_free_full (g_steal_pointer (&data->special_values), g_object_unref);

        return;
    }

    /* Every kind is built from the item's value, and every constructor rejects a
     * %NULL one with a critical: an <item> with no usable kind, or with no
     * <value> at all (absent, empty or whitespace-only), carries nothing we can
     * restore and is skipped — along with its special values, released below. */
    GPasteItemKind type = (data->text) ? data->type : G_PASTE_ITEM_KIND_INVALID;

    if (type == G_PASTE_ITEM_KIND_INVALID)
        g_warning ("Ignoring an item with no usable kind or value in file “%s”", data->history_file_path);

    switch (type)
    {
    case G_PASTE_ITEM_KIND_TEXT:
        item = g_paste_text_item_new (data->text);
        break;
    case G_PASTE_ITEM_KIND_URIS:
        item = g_paste_uris_item_new_from_str (data->text);
        break;
    case G_PASTE_ITEM_KIND_PASSWORD:
        item = g_paste_password_item_new (data->name, data->text);
        break;
    case G_PASTE_ITEM_KIND_COLOR:
        item = g_paste_color_item_new_from_str (data->text);
        break;
    case G_PASTE_ITEM_KIND_IMAGE:
        if (data->images_support && data->date)
        {
            g_autoptr (GDateTime) date_time = g_date_time_new_from_unix_local (g_ascii_strtoll (data->date,
                                                                                                NULL, /* end */
                                                                                                0)); /* base */
            /* The encrypted flavor stores the image in an encrypted side file
             * rather than at the referenced (plaintext) path. Either way the
             * item keeps the path it was stored with: that is where its
             * materialized data actually lives. */
            g_autoptr (GBytes) png = _g_paste_file_backend_load_image_bytes (data->backend, data->text);

            item = (png) ? g_paste_image_item_new_from_bytes_at_path (data->text, png, date_time, data->checksum)
                         : g_paste_image_item_new_from_file (data->text, date_time, data->checksum);
        }
        else
            g_paste_file_backend_delete_image (data->text);
        break;
    case G_PASTE_ITEM_KIND_INVALID:
        break;
    }

    if (item)
    {
        if (!data->uuid)
            data->uuid = g_uuid_string_random ();

        g_paste_item_set_uuid (item, data->uuid);
        g_paste_item_set_favourite (item, data->favourite);
        /* Prepended and reversed once at the end of the parse: appending walks
         * the list per item, which is quadratic over a history that goes up to
         * max-history-size items. */
        data->history = g_list_prepend (data->history, item);

        /* Only the items the cap can actually evict are counted against it: a
         * favourite is read back whatever it costs, or one that had sunk past
         * the cap would be lost on the very next save. */
        if (!data->favourite)
            ++data->current_size;
    }

    for (GSList *d = data->special_values; d; d = d->next)
    {
        GPasteBinaryData *v = d->data;

        if (item)
            g_paste_item_add_special_value (item, v);
        else
            g_object_unref (v);
    }

    if (item)
        data->mem_size += g_paste_item_get_size (item);

    g_clear_pointer (&data->special_values, g_slist_free);
}

static void
end_tag (GMarkupParseContext *context,
         const gchar         *element_name,
         gpointer             user_data,
         GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    if (g_paste_str_equal (element_name, "history"))
        SWITCH_STATE (IN_HISTORY, END);
    else if (g_paste_str_equal (element_name, "item"))
    {
        /* An unknown (or missing) version collects neither the item's value nor
         * its special values (see on_text), so there is nothing to restore.
         * A favourite is always taken: the cap does not apply to it, and
         * current_size counts only the items it does apply to. */
        if (data->version != HISTORY_INVALID && (data->favourite || data->current_size < data->max_size))
            add_item (data);
        /* Leave the item even when the version is unknown: staying inside it
         * would make every element that follows fail its assert — including the
         * next <item>, whose scratch reset would then be skipped, so the items
         * of an unreadable history would bleed into each other. IN_ITEM is the
         * only state we can be in here: no format carries text inside <item>. */
        SWITCH_STATE (IN_ITEM, IN_HISTORY);
    }
    else if (g_paste_str_equal (element_name, "value"))
        SWITCH_STATE_OR_EMPTY (IN_VALUE_WITH_TEXT, IN_VALUE, IN_ITEM);
    else
        WARN_AT ("Unknown element: %s", element_name);
}

static void
on_text (GMarkupParseContext *context,
         const gchar         *text,
         gsize                text_len,
         gpointer             user_data,
         GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    g_autofree gchar *txt = g_strndup (text, text_len);
    switch (data->state)
    {
    case IN_HISTORY:
    case IN_VALUE_WITH_TEXT:
        if (*g_strstrip (txt))
        {
            WARN_AT ("Unexpected text: %s", txt);
            return;
        }
        break;
    case IN_ITEM:
        /* An item's value lives in a <value> child; text directly inside <item>
         * was the 1.0 layout, which is no longer read. */
        if (*g_strstrip (txt))
            WARN_AT ("Unexpected text in item: %s", txt);
        break;
    case IN_VALUE:
        if (data->version == HISTORY_2_0)
        {
            g_autofree gchar *value = g_paste_util_xml_decode (txt);
            if (*g_strstrip (txt))
            {
                SWITCH_STATE (IN_VALUE, IN_VALUE_WITH_TEXT);
                if (data->mime == G_PASTE_SPECIAL_ATOM_INVALID)
                    g_set_str_take (&data->text, g_steal_pointer (&value));
                else
                {
                    gsize raw_length;
                    guchar *raw = (guchar *) g_base64_decode (value, &raw_length);
                    GPasteBinaryData *sv = g_paste_binary_data_new (data->mime, g_bytes_new_take (raw, raw_length));
                    data->special_values = g_slist_prepend (data->special_values, sv);
                }
            }
        }
        else
            WARN_AT ("Unexpected value for history version != 2.0");
        break;
    default:
        WARN_AT ("Unexpected state: %" G_GINT32_FORMAT, data->state);
        break;
    }
}

static void
on_error (GMarkupParseContext *context,
          GError              *error,
          gpointer             user_data)
{
    const Data *data = user_data;

    WARN_AT ("error: %s", error->message);
}

/******************/
/* End XML Parser */
/******************/

/* Load the raw history document, transparently decrypting it for an encrypted
 * backend. Returns the (caller-owned) bytes through @text / @text_length. */
static gboolean
g_paste_file_backend_load_contents (GPasteStorageBackend *self,
                                    const gchar          *history_file_path,
                                    GFile                *history_file,
                                    gchar               **text,
                                    gsize                *text_length,
                                    GError              **error)
{
#ifdef G_PASTE_ENABLE_ENCRYPTION
    const gchar *passphrase = g_paste_file_backend_get_passphrase (self);

    if (passphrase)
    {
        g_autoptr (GFileInputStream) file_in = g_file_read (history_file, NULL, error);

        if (!file_in)
            return FALSE;

        g_autoptr (GConverter) converter = g_paste_secret_stream_converter_new (G_PASTE_SECRET_STREAM_DECRYPT, passphrase);
        g_autoptr (GInputStream) decrypted = g_converter_input_stream_new (G_INPUT_STREAM (file_in), converter);
        g_autoptr (GOutputStream) buffer = g_memory_output_stream_new_resizable ();

        if (g_output_stream_splice (buffer, decrypted,
                                    G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                    NULL, error) < 0)
            return FALSE;

        *text_length = g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (buffer));
        *text = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (buffer));

        return TRUE;
    }
#else
    (void) self;
    (void) history_file;
#endif

    return g_file_get_contents (history_file_path, text, text_length, error);
}

/* One walk of the document, for both of the questions asked of it: @history and
 * @size for a read, @count for a listing that only wants how many items there
 * are. Counting skips the item building and the images entirely (see add_item),
 * but goes through the same parser, the same version gate and the same size cap,
 * so what a listing reports is what switching to that history hands back.
 *
 * Only a read creates the placeholder file for a history that is not there:
 * counting must not bring a history into existence. */
static gboolean
_g_paste_file_backend_read_or_count (GPasteStorageBackend *self,
                                     const gchar          *name,
                                     gboolean              count_only,
                                     GList               **history,
                                     gsize                *size,
                                     gsize                *count)
{
    GPasteSettings *settings = g_paste_storage_backend_get_settings (self);
    g_autofree gchar *history_file_path = g_paste_storage_backend_get_history_file_path (self, name);
    g_autoptr (GFile) history_file = g_file_new_for_path (history_file_path);
    g_autofree gchar *text = NULL;

    if (g_file_query_exists (history_file,
                             NULL)) /* cancellable */
    {
        GMarkupParser parser = {
            start_tag,
            end_tag,
            on_text,
            NULL,
            on_error
        };
        Data data = {
            self,
            history_file_path,
            NULL,
            0,
            BEGIN,
            G_PASTE_ITEM_KIND_INVALID, /* set per item from its "kind" attribute */
            0,
            g_paste_settings_get_max_history_size (settings),
            g_paste_settings_get_images_support (settings),
            count_only,
            0, /* count */
            FALSE, /* favourite: set per item from its "favourite" attribute */
            NULL, /* uuid */
            NULL, /* date */
            NULL, /* checksum */
            NULL, /* name */
            NULL, /* text */
            NULL, /* special_values */
            HISTORY_INVALID,
            G_PASTE_SPECIAL_ATOM_INVALID
        };
        g_autoptr (GMarkupParseContext) ctx = g_markup_parse_context_new (&parser,
                                                                          G_MARKUP_TREAT_CDATA_AS_TEXT,
                                                                          &data,
                                                                          NULL);
        gsize text_length;
        g_autoptr (GError) error = NULL;

        if (!g_paste_file_backend_load_contents (self, history_file_path, history_file, &text, &text_length, &error))
        {
            /* Present but unreadable (e.g. a wrong passphrase failing the
             * authenticated decryption, or an I/O error): report the failure so
             * a caller never mistakes it for a genuinely empty history. */
            g_warning ("Failed to read history file: %s", error->message);
            return FALSE;
        }

        /* A zero-length file is the placeholder the else branch below creates for
         * a history that exists but was never written to: an authoritative empty
         * history, not a failure. GMarkup would reject it (G_MARKUP_ERROR_EMPTY),
         * and reporting that as unreadable would abort a whole storage migration
         * over one unused history. */
        if (!text_length)
            return TRUE;

        gboolean parsed = g_markup_parse_context_parse (ctx, text, text_length, &error) &&
                          g_markup_parse_context_end_parse (ctx, &error);

        /* The context is still alive here (it is freed at scope exit), so a
         * truncated file can say where it stopped rather than just how. */
        if (!parsed || data.state != END)
        {
            g_autofree gchar *location = parse_location (ctx, &data);

            if (!parsed)
                g_warning ("Failed to parse history file %s: %s", location, error->message);

            if (data.state != END)
                g_warning ("Unexpected state after parsing history %s: %" G_GINT32_FORMAT, location, data.state);
        }

        if (count_only)
            *count = data.count;
        else
        {
            *history = g_list_reverse (data.history);
            *size = data.mem_size;
        }

        g_clear_pointer (&data.uuid, g_free);
        g_clear_pointer (&data.date, g_free);
        g_clear_pointer (&data.checksum, g_free);
        g_clear_pointer (&data.name, g_free);
        g_clear_pointer (&data.text, g_free);
        /* add_item() consumes these at every </item>; a document that ends inside
         * one (truncated file, failed parse) leaves the last item's behind. */
        g_clear_slist (&data.special_values, g_object_unref);

        /* An unknown version is reported as a read failure, not as an empty
         * history: its items were all skipped (we know neither where their value
         * lives nor how to decode it), so letting it pass as readable would have
         * the caller persist that empty model over the file on the very next
         * clipboard change — destroying exactly what the untouched-file rule
         * above just protected. */
        return parsed && data.version != HISTORY_INVALID;
    }
    else if (!count_only)
    {
        /* Create the empty file to be listed as an available history */
        if (g_paste_util_ensure_history_dir_exists ())
        {
            g_autoptr (GError) error = NULL;
            g_autoptr (GFileOutputStream) created = g_file_create (history_file, G_FILE_CREATE_NONE, NULL, &error);
            if (!created && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
                g_warning ("Failed to create history file: %s", error->message);
        }
    }

    /* An absent history is a legitimately empty one, not a read failure. */
    return TRUE;
}

static gboolean
g_paste_file_backend_read_history_file (GPasteStorageBackend *self,
                                        const gchar          *name,
                                        GList               **history,
                                        gsize                *size)
{
    return _g_paste_file_backend_read_or_count (self, name, FALSE, history, size, NULL);
}

/* Parsing is what a count costs here -- the document has to be walked (and, for
 * the encrypted flavour, decrypted) to know how many items survive the cap. What
 * it does not cost is the items themselves: no value is copied out, no image is
 * loaded from its side file, and nothing is added to a list only to be dropped
 * again, which is the whole of what a listing used to pay per history. */
static gsize
g_paste_file_backend_count_history (GPasteStorageBackend *self,
                                    const gchar          *name)
{
    gsize count = 0;

    if (!_g_paste_file_backend_read_or_count (self, name, TRUE, NULL, NULL, &count))
        return 0;

    return count;
}

static void
g_paste_file_backend_delete_history (GPasteStorageBackend *self,
                                     const gchar          *name,
                                     GError              **error)
{
    g_autoptr (GFile) history_file = g_paste_util_get_history_file (name, g_paste_storage_backend_get_extension (self));

    g_file_delete (history_file, NULL, error);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* Re-encrypt one file's bytes from the key @self holds to the one @rekeyed does,
 * into a sibling temporary file whose path is returned through @tmp_path. The
 * bytes are copied verbatim — this is a key change, not a rewrite — and read
 * back through the new key before the caller is told it worked, so a bad write
 * is caught while the original is still the only thing in place. */
static gboolean
_g_paste_file_backend_reencrypt_file (GPasteStorageBackend *self,
                                      GPasteStorageBackend *rekeyed,
                                      const gchar          *path,
                                      gchar               **tmp_path)
{
    g_autoptr (GFile) file = g_file_new_for_path (path);
    g_autofree gchar *text = NULL;
    gsize length = 0;
    g_autoptr (GError) error = NULL;

    if (!g_paste_file_backend_load_contents (self, path, file, &text, &length, &error))
    {
        g_warning ("Failed to read %s with the current passphrase: %s", path, error->message);
        return FALSE;
    }

    g_autofree gchar *tmp = g_strconcat (path, ".rekey", NULL);
    g_autoptr (GFile) tmp_file = g_file_new_for_path (tmp);
    g_autoptr (GOutputStream) stream = G_PASTE_FILE_BACKEND_GET_CLASS (rekeyed)->get_output_stream (G_PASTE_FILE_BACKEND ((gpointer) rekeyed),
                                                                                                     tmp_file);

    if (!stream)
        return FALSE;

    gboolean ok = g_output_stream_write_all (stream, text, length, NULL, NULL /* cancellable */, &error) &&
                  g_output_stream_close (stream, NULL /* cancellable */, &error);

    if (ok)
    {
        g_autofree gchar *check = NULL;
        gsize check_length = 0;

        ok = g_paste_file_backend_load_contents (rekeyed, tmp, tmp_file, &check, &check_length, &error) &&
             check_length == length && !memcmp (check, text, length);
    }

    if (!ok)
    {
        g_warning ("Failed to re-encrypt %s: %s", path, (error) ? error->message : "it did not read back identical");
        /* Best effort: the failure is already reported, and a leftover temporary
         * beside the untouched original is the harmless outcome. */
        g_file_delete (tmp_file, NULL, NULL);

        return FALSE;
    }

    *tmp_path = g_steal_pointer (&tmp);

    return TRUE;
}

/* Every file of @name that is encrypted: the history itself and the image side
 * files it references, which live in the history's own images directory.
 *
 * %NULL (never an incomplete list) if the directory is there but cannot be
 * listed: the caller re-keys exactly what this returns, so a short list would
 * leave the images it missed encrypted under the old passphrase, unreadable for
 * good once the new one is in force. */
static GStrv
_g_paste_file_backend_encrypted_files (GPasteStorageBackend *self,
                                       const gchar          *name)
{
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_autofree gchar *history_path = g_paste_storage_backend_get_history_file_path (self, name);

    if (g_file_test (history_path, G_FILE_TEST_EXISTS))
        g_strv_builder_take (builder, g_steal_pointer (&history_path));

    g_autofree gchar *images_dir = g_paste_file_backend_images_dir (name);
    g_autoptr (GFile) dir = g_file_new_for_path (images_dir);
    g_autoptr (GError) error = NULL;
    /* A history that never held an image has no directory, and lists nothing. */
    g_auto (GStrv) images = g_paste_util_list_directory (dir, G_FILE_ATTRIBUTE_STANDARD_NAME, &error);

    if (!images)
    {
        g_warning ("Could not list the images of \"%s\": %s", name, error->message);

        return NULL;
    }

    for (GStrv image = images; *image; ++image)
    {
        /* The encrypted side files only; a leftover plain ".png" holds
         * nothing secret and has no key to change. */
        if (g_str_has_suffix (*image, ".pngs"))
            g_strv_builder_take (builder, g_build_filename (images_dir, *image, NULL));
    }

    return g_strv_builder_end (builder);
}

/* Re-encrypt @name under @new_passphrase. Everything encrypted under the old key
 * — the history and its images — is rewritten beside itself and only then moved
 * into place, so a failure anywhere leaves the whole history exactly as it was.
 *
 * Deliberately byte-for-byte: reading the history into items and writing them
 * back would put it through the history-size limit and the images-support
 * filter, so changing a passphrase would quietly drop items the user still has.
 * A key change must change nothing but the key. */
static gboolean
g_paste_file_backend_rekey (GPasteStorageBackend *self,
                            const gchar          *name,
                            const gchar          *new_passphrase)
{
    if (!g_paste_file_backend_get_passphrase (self))
    {
        g_warning ("This history is not encrypted: it has no passphrase to change");
        return FALSE;
    }

    GPasteSettings *settings = g_paste_storage_backend_get_settings (self);
    g_autoptr (GPasteStorageBackend) rekeyed = g_paste_file_backend_new_encrypted ((GPasteSettings *) settings,
                                                                                  new_passphrase);
    g_auto (GStrv) paths = _g_paste_file_backend_encrypted_files (self, name);

    /* Re-keying part of a history is worse than not re-keying it at all — and
     * re-keying none of it is the same thing said of the whole: a history whose
     * files we could not list, or could not find, must not come back as one that
     * now speaks the new passphrase. */
    if (!paths || !*paths)
        return FALSE;

    g_autoptr (GStrvBuilder) written = g_strv_builder_new ();
    gboolean ok = TRUE;

    for (GStrv path = paths; ok && *path; ++path)
    {
        gchar *tmp = NULL;

        if ((ok = _g_paste_file_backend_reencrypt_file (self, rekeyed, *path, &tmp)))
            g_strv_builder_take (written, tmp);
    }

    g_auto (GStrv) tmps = g_strv_builder_end (written);

    if (!ok)
    {
        /* Nothing has moved yet, so dropping the copies puts us back exactly
         * where we started. Best effort: a temporary we fail to remove is
         * clutter, and the originals are intact either way. */
        for (GStrv tmp = tmps; *tmp; ++tmp)
        {
            g_autoptr (GFile) file = g_file_new_for_path (*tmp);

            g_file_delete (file, NULL, NULL);
        }

        return FALSE;
    }

    for (guint i = 0; tmps[i]; ++i)
    {
        g_autoptr (GFile) tmp_file = g_file_new_for_path (tmps[i]);
        g_autoptr (GFile) file = g_file_new_for_path (paths[i]);
        g_autoptr (GError) error = NULL;

        /* Each of these is a rename over a file whose replacement is already
         * written and verified, so the only way to fail here is the filesystem
         * itself giving out. */
        if (!g_file_move (tmp_file, file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error))
        {
            g_warning ("Failed to install the re-encrypted %s: %s", paths[i], error->message);
            ok = FALSE;
        }
    }

    return ok;
}
#endif

static GPasteStorage
g_paste_file_backend_get_kind (GPasteStorageBackend *self)
{
    return g_paste_file_backend_get_passphrase (self) ? G_PASTE_STORAGE_ENCRYPTED_FILE : G_PASTE_STORAGE_FILE;
}

static GOutputStream *
g_paste_file_backend_get_output_stream (GPasteFileBackend *self G_GNUC_UNUSED,
                                        GFile             *output_file)
{
    g_autoptr (GError) error = NULL;
    GOutputStream *stream = G_OUTPUT_STREAM (g_file_replace (output_file,
                                                              NULL,
                                                              FALSE,
                                                              G_FILE_CREATE_REPLACE_DESTINATION,
                                                              NULL, /* cancellable */
                                                              &error));
    if (!stream)
    {
        g_warning ("Failed to open history temp file for writing: %s", error->message);
        return NULL;
    }

#ifdef G_PASTE_ENABLE_ENCRYPTION
    const GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (self);

    if (priv->passphrase)
    {
        g_autoptr (GConverter) converter = g_paste_secret_stream_converter_new (G_PASTE_SECRET_STREAM_ENCRYPT, priv->passphrase);
        GOutputStream *encrypted = g_converter_output_stream_new (stream, converter);

        /* The converter stream now owns the file stream (and flushes the FINAL
         * tag when closed). */
        g_object_unref (stream);
        stream = encrypted;
    }
#endif

    return stream;
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
static void
g_paste_file_backend_finalize (GObject *object)
{
    GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (G_PASTE_FILE_BACKEND (object));

    gcr_secure_memory_strfree (priv->passphrase);

    G_OBJECT_CLASS (g_paste_file_backend_parent_class)->finalize (object);
}
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* A history whose authenticated decryption fails with INVALID_DATA is the one
 * case that proves the passphrase wrong. An empty placeholder, a truncation or
 * an I/O error carries no recoverable data, so it says nothing either way. */
static gboolean
g_paste_file_backend_history_refutes_passphrase (GPasteStorageBackend *self,
                                                 const gchar          *name)
{
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (self, name);
    g_autoptr (GFile) file = g_file_new_for_path (path);
    g_autofree gchar *text = NULL;
    gsize text_length;
    g_autoptr (GError) error = NULL;

    if (g_paste_file_backend_load_contents (self, path, file, &text, &text_length, &error))
        return FALSE;

    return g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
}
#endif

/* What this backend wrote for @item beside its history file: an image's cache
 * file, and nothing else. Named from the checksum under the history the item is
 * being dropped from, plus wherever the item was read from when that is
 * somewhere else -- a legacy shared-directory entry is still this item's file.
 * A history whose images were never materialized (another flavour wrote it)
 * simply has nothing to delete. */
static void
g_paste_file_backend_drop_item_data (GPasteStorageBackend *self G_GNUC_UNUSED,
                                     const gchar          *name,
                                     GPasteItem           *item)
{
    if (!G_PASTE_IS_IMAGE_ITEM (item))
        return;

    GPasteImageItem *image = G_PASTE_IMAGE_ITEM (item);
    const gchar *checksum = g_paste_image_item_get_checksum (image);
    const gchar *cache_path = g_paste_image_item_get_cache_path (image);
    g_autofree gchar *path = (checksum) ? g_paste_file_backend_image_path (name, checksum) : NULL;

    if (path)
        g_paste_file_backend_delete_image (path);

    if (cache_path && !g_paste_str_equal (cache_path, path))
        g_paste_file_backend_delete_image (cache_path);
}

static void
g_paste_file_backend_class_init (GPasteFileBackendClass *klass)
{
    GPasteStorageBackendClass *storage_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    storage_class->read_history_file = g_paste_file_backend_read_history_file;
    storage_class->write_history_file = g_paste_file_backend_write_history_file;
    storage_class->get_kind = g_paste_file_backend_get_kind;
    storage_class->delete_history = g_paste_file_backend_delete_history;
    storage_class->count_history = g_paste_file_backend_count_history;
    storage_class->drop_item_data = g_paste_file_backend_drop_item_data;

    klass->get_output_stream = g_paste_file_backend_get_output_stream;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    storage_class->rekey = g_paste_file_backend_rekey;
    storage_class->history_refutes_passphrase = g_paste_file_backend_history_refutes_passphrase;

    G_OBJECT_CLASS (klass)->finalize = g_paste_file_backend_finalize;
#endif
}

static void
g_paste_file_backend_init (GPasteFileBackend *self G_GNUC_UNUSED)
{
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/**
 * g_paste_file_backend_new_encrypted:
 * @settings: a #GPasteSettings instance
 * @passphrase: the passphrase the encryption key is derived from
 *
 * Create a file storage backend that encrypts the history on disk (with the
 * ".xmls" extension) using @passphrase. Unlike the plain backend it persists
 * password entries, since the file is unreadable without the passphrase.
 *
 * Returns: (transfer full): a newly allocated #GPasteStorageBackend
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteStorageBackend *
g_paste_file_backend_new_encrypted (GPasteSettings *settings,
                                    const gchar    *passphrase)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);
    g_return_val_if_fail (passphrase && *passphrase, NULL);

    GPasteStorageBackend *self = g_paste_storage_backend_new (G_PASTE_STORAGE_FILE, settings);
    GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (G_PASTE_FILE_BACKEND (self));

    priv->passphrase = gcr_secure_memory_strdup (passphrase);

    return self;
}

#endif
