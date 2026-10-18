// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-util.h>

#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-file-backend.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-item.h>
#include <gpaste-daemon/gpaste-passphrase.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-storage-backend-private.h>
#include <gpaste-daemon/gpaste-storage-migration.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <glib/gstdio.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef G_PASTE_ENABLE_ENCRYPTION
#include <gpaste-daemon/gpaste-secret-stream-converter.h>
#endif

typedef struct
{
    /* When set, the history is encrypted on disk: the file streams are wrapped
     * with a secretstream converter, the ".xmls" extension is used, and password
     * entries are persisted (encrypted) rather than skipped. NULL means a plain
     * ".xml" history. */
    GPastePassphrase *passphrase;
#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* The SHA-256 of the PNG each encrypted side file holds, by the side file's
     * path, for the ones this backend wrote, or read and found to be the image
     * their history names: what a document names a side file by (see
     * _g_paste_file_backend_write_image_metadata ()). The saver's worker and the
     * main thread both read and write histories. */
    GMutex            png_checksums_lock;
    GHashTable       *png_checksums;
#endif
} GPasteFileBackendPrivate;

G_PASTE_DEFINE_TYPE_WITH_PRIVATE (FileBackend, file_backend, G_PASTE_TYPE_STORAGE_BACKEND)

/* The passphrase, or NULL for a plain history. Safe to call whether or not
 * encryption was built in (it is always NULL without it). */
static const gchar *
g_paste_file_backend_get_passphrase (GPasteStorageBackend *self)
{
    const GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (G_PASTE_FILE_BACKEND (self));

    return g_paste_passphrase_peek (priv->passphrase);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* That @side_file holds the PNG whose SHA-256 is @png_checksum. */
static void
_g_paste_file_backend_remember_png (GPasteStorageBackend *self,
                                    const gchar          *side_file,
                                    const gchar          *png_checksum)
{
    GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (G_PASTE_FILE_BACKEND (self));
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->png_checksums_lock);

    g_hash_table_replace (priv->png_checksums, g_strdup (side_file), g_strdup (png_checksum));
}

/* The SHA-256 of the PNG @side_file holds, when this backend knows it. */
static gchar *
_g_paste_file_backend_png_checksum (GPasteStorageBackend *self,
                                    const gchar          *side_file)
{
    GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (G_PASTE_FILE_BACKEND (self));
    g_autoptr (GMutexLocker) locker = g_mutex_locker_new (&priv->png_checksums_lock);

    return g_strdup (g_hash_table_lookup (priv->png_checksums, side_file));
}
#endif

static gboolean g_paste_file_backend_load_contents (GPasteStorageBackend *self,
                                                    const gchar          *history_file_path,
                                                    GFile                *history_file,
                                                    gboolean              side_file,
                                                    gchar               **text,
                                                    gsize                *text_length,
                                                    gboolean             *opened,
                                                    GError              **error);

static gboolean
_g_paste_file_backend_write_password_name (GOutputStream      *stream,
                                           GPastePasswordItem *item,
                                           GError            **error)
{
    /* An attribute value, not a CDATA payload: g_paste_util_xml_encode() escapes
     * "&" and ">", which is what keeps a "]]>" out of a CDATA section, and
     * leaves the quote that delimits an attribute alone. A password name is
     * whatever MakePassword was handed, and a quote in one would close the
     * attribute early -- forging the ones that follow, or breaking the document
     * outright so the whole history stops being readable.
     * g_markup_escape_text() is the attribute's escaping, and the entities it
     * writes are the ones GMarkup gives back on read -- bar the tab, newline
     * and carriage return it leaves as they are, which a read normalizes to
     * spaces, as XML has it: a name holding one would come back another, or
     * the same as another password's. Those go as character references, which
     * a read keeps. */
    g_autofree gchar *escaped = g_markup_escape_text (g_paste_password_item_get_name (item), -1);
    g_autoptr (GString) name = g_string_new_take (g_steal_pointer (&escaped));

    g_string_replace (name, "\t", "&#9;", 0);
    g_string_replace (name, "\n", "&#10;", 0);
    g_string_replace (name, "\r", "&#13;", 0);

    guint timeout = g_paste_password_item_get_timeout (item);
    /* Written only when set, as the favourite attribute is: a password with no
     * timeout, which is every one an older GPaste wrote, reads back the same. */
    g_autofree gchar *timeout_attr = (timeout) ? g_strdup_printf ("\" timeout=\"%u", timeout) : NULL;

    return g_output_stream_write_all (stream, "\" name=\"", 8, NULL, NULL /* cancellable */, error) &&
           g_output_stream_write_all (stream, name->str, name->len, NULL, NULL /* cancellable */, error) &&
           (!timeout_attr || g_output_stream_write_all (stream, timeout_attr, strlen (timeout_attr), NULL, NULL /* cancellable */, error));
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

/* Have what was written to @file reach the disk before it is renamed into
 * place. A crash after a rename the filesystem committed first leaves the
 * renamed file there but empty -- no rename heuristic on XFS, nor on ext4
 * mounted with noauto_da_alloc -- and an empty history reads as an empty one,
 * which the next save makes for good. g_file_set_contents () syncs for the same
 * reason; the streams here are synced once closed, the encryption converter
 * writing its last chunk on close. */
static gboolean
_g_paste_file_backend_sync (GFile   *file,
                            GError **error)
{
    g_autofree gchar *path = g_file_get_path (file);
    gint fd = g_open (path, O_RDONLY | O_CLOEXEC, 0);
    gint saved_errno = errno;

    if (fd >= 0)
    {
        gboolean synced = !fsync (fd);

        saved_errno = errno;
        g_close (fd, NULL);
        if (synced)
            return TRUE;
    }

    g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno), "Could not sync %s: %s", path, g_strerror (saved_errno));

    return FALSE;
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* The largest encrypted file this flavour stores: twice the largest
 * max-memory-usage (16383 MiB), which nothing the model keeps weighs past, room
 * for what base64 and markup add to it and for the stream's few bytes a chunk. */
#define G_PASTE_FILE_BACKEND_MAX_STREAM_SIZE (G_GUINT64_CONSTANT (32768) * 1024 * 1024)
/* How much ciphertext an encrypted file is read
 * by, many of the stream's frames. */
#define G_PASTE_FILE_BACKEND_READ_SIZE (64 * 1024)
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* Sync @path, a directory: what was renamed into it is on the disk then. Best
 * effort, a filesystem that cannot sync a directory having nothing more to
 * offer, and a change it holds being made either way. */
static void
_g_paste_file_backend_sync_directory (const gchar *path)
{
    gint fd = g_open (path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);

    if (fd < 0 || (fsync (fd) && errno != EINVAL))
        g_warning ("Could not sync %s: %s", path, g_strerror (errno));
    if (fd >= 0)
        g_close (fd, NULL);
}
#endif

/* The XML history references images by their canonical <checksum>.png path
 * (@reference, derived from the item's checksum under the history being
 * written, which is the backup's own when writing under another name); writing
 * the image data itself is this backend's job -- items do not touch the disk at
 * capture. The plain
 * flavor writes that file as-is; the encrypted one writes a "<checksum>.pngs"
 * sibling through the same stream converter as the history, so no pixel ever
 * reaches the disk in clear.
 *
 * Returns: whether the file is there, written now or before */
static gboolean
_g_paste_file_backend_ensure_image_file (GPasteFileBackend *self,
                                         GPasteImageItem   *item,
                                         const gchar       *reference)
{
    gboolean encrypted = g_paste_storage_backend_is_encrypted (G_PASTE_STORAGE_BACKEND (self));
    g_autofree gchar *target = (encrypted) ? g_paste_file_backend_encrypted_path (reference) : g_strdup (reference);

    if (g_file_test (target, G_FILE_TEST_EXISTS))
        return TRUE;

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

        if (!cache_path || !g_file_get_contents (cache_path, &data, &length, NULL))
            return FALSE;

        png = read_back = g_bytes_new_take (data, length);
    }

    g_autofree gchar *images_dir = g_path_get_dirname (target);
    g_autoptr (GError) error = NULL;

    /* 0700 like the history directory above it: what lands here is the
     * clipboard's screenshots, and the plain flavour writes them unencrypted. */
    if (g_mkdir_with_parents (images_dir, 0700) < 0)
    {
        g_warning ("Failed to create images directory: %s", g_strerror (errno));
        return FALSE;
    }

    /* get_output_stream wraps the file with the encryption converter exactly
     * when this backend has a passphrase, matching the target chosen above.
     *
     * Written beside the target and renamed over it, as the history is: the
     * check above takes any file there for a complete one, so a write cut
     * short at its final name -- a full disk, a key derivation that failed, a
     * crash -- would have every later save skip the image and the next read
     * drop it. */
    g_autoptr (GFile) target_file = g_file_new_for_path (target);
    g_autofree gchar *tmp = g_strconcat (target, ".tmp", NULL);
    g_autoptr (GFile) tmp_file = g_file_new_for_path (tmp);
    g_autoptr (GOutputStream) stream = G_PASTE_FILE_BACKEND_GET_CLASS (self)->get_output_stream (self, tmp_file);

    if (!stream)
        return FALSE;

    gsize length;
    gconstpointer data = g_bytes_get_data (png, &length);

    if (g_output_stream_write_all (stream, data, length, NULL, NULL /* cancellable */, &error) &&
        g_output_stream_close (stream, NULL /* cancellable */, &error) &&
        _g_paste_file_backend_sync (tmp_file, &error) &&
        g_file_move (tmp_file, target_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error))
    {
#ifdef G_PASTE_ENABLE_ENCRYPTION
        if (encrypted)
        {
            g_autofree gchar *png_checksum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA256, png);

            _g_paste_file_backend_remember_png (G_PASTE_STORAGE_BACKEND ((gpointer) self), target, png_checksum);
        }
#endif
        return TRUE;
    }

    g_warning ("Failed to materialize image to %s: %s", target, error->message);
    g_file_delete (tmp_file, NULL, NULL);

    return FALSE;
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

    if (!g_paste_file_backend_load_contents (self, encrypted_path, encrypted_file, TRUE, &data, &length, NULL, &error))
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

/* An image's date and checksum and, in an encrypted document, @png_checksum:
 * the SHA-256 of the PNG its side file holds, which is what binds that file to
 * the authenticated document -- anyone able to write the images directory could
 * otherwise put another of the user's own side files in its place. Checking it
 * costs a hash of the PNG, where the image's own checksum is one of its pixels,
 * decoded and downloaded first, and one a round trip through PNG does not always
 * reproduce. %NULL when this backend does not know what the side file holds,
 * which a read checks by its pixels instead. */
static gboolean
_g_paste_file_backend_write_image_metadata (GOutputStream   *stream,
                                            GPasteImageItem *item,
                                            const gchar     *png_checksum,
                                            GError         **error)
{
    g_autofree gchar *date_str = g_date_time_format ((GDateTime *) g_paste_image_item_get_date (item), "%s");
    const gchar *checksum = g_paste_image_item_get_checksum (item);

    if (!g_output_stream_write_all (stream, "\" date=\"", 8, NULL, NULL /* cancellable */, error) ||
        !g_output_stream_write_all (stream, date_str, strlen (date_str), NULL, NULL /* cancellable */, error))
        return FALSE;

    /* The checksums (hex SHA256) need no XML escaping */
    if (checksum &&
        (!g_output_stream_write_all (stream, "\" checksum=\"", 12, NULL, NULL /* cancellable */, error) ||
         !g_output_stream_write_all (stream, checksum, strlen (checksum), NULL, NULL /* cancellable */, error)))
        return FALSE;

    if (png_checksum &&
        (!g_output_stream_write_all (stream, "\" png=\"", 7, NULL, NULL /* cancellable */, error) ||
         !g_output_stream_write_all (stream, png_checksum, strlen (png_checksum), NULL, NULL /* cancellable */, error)))
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
        GEnumValue *gev = g_enum_get_value (g_type_class_get (G_PASTE_TYPE_SPECIAL_MIME), g_paste_binary_data_get_mime (value));

        /* Skip a value carrying an unknown mime rather than dereferencing NULL,
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
            return FALSE;
    }

    return TRUE;
}

/* The name of the file this backend materializes an image in, under its
 * history's images directory: its checksum, a SHA-256 in hex, and the flavour's
 * suffix. %NULL for an item without a checksum, which no file names. */
static gchar *
_g_paste_file_backend_image_file_name (GPasteImageItem *image,
                                       gboolean         encrypted)
{
    const gchar *checksum = g_paste_image_item_get_checksum (image);

    if (!checksum)
        return NULL;

    return g_strconcat (checksum, (encrypted) ? ".pngs" : ".png", NULL);
}

/* Whether @file_name is one _g_paste_file_backend_image_file_name() could have
 * produced for this flavour. */
static gboolean
_g_paste_file_backend_is_image_file_name (const gchar *file_name,
                                          gboolean     encrypted)
{
    const gchar *suffix = (encrypted) ? ".pngs" : ".png";

    if (!g_str_has_suffix (file_name, suffix))
        return FALSE;

    g_autofree gchar *checksum = g_strndup (file_name, strlen (file_name) - strlen (suffix));

    return g_paste_image_item_is_checksum (checksum);
}

/* Whether @file_name is what writing an image of either flavour leaves when it
 * is cut short between the write and the move into place: an image's name with
 * ".tmp" after it (_g_paste_file_backend_ensure_image_file ()). */
static gboolean
_g_paste_file_backend_is_image_temp_name (const gchar *file_name)
{
    if (!g_str_has_suffix (file_name, ".tmp"))
        return FALSE;

    g_autofree gchar *image = g_strndup (file_name, strlen (file_name) - strlen (".tmp"));

    return _g_paste_file_backend_is_image_file_name (image, TRUE) || _g_paste_file_backend_is_image_file_name (image, FALSE);
}

/* Whether @file_name is what a re-key leaves beside an encrypted image: its
 * replacement or its original kept aside, named from the image's own name. */
static gboolean
_g_paste_file_backend_is_image_rekey_name (const gchar *file_name)
{
    static const gchar *suffixes[] = { G_PASTE_FILE_BACKEND_REKEY_NEW, G_PASTE_FILE_BACKEND_REKEY_OLD };

    for (gsize i = 0; i < G_N_ELEMENTS (suffixes); ++i)
    {
        if (!g_str_has_suffix (file_name, suffixes[i]))
            continue;

        g_autofree gchar *image = g_strndup (file_name, strlen (file_name) - strlen (suffixes[i]));

        if (_g_paste_file_backend_is_image_file_name (image, TRUE))
            return TRUE;
    }

    return FALSE;
}

/* Delete the image files under images/@history_name/ that no history of that
 * name can read any more, once the store of this flavour has been written in
 * full (@referenced, the file names its snapshot references) or deleted
 * (@referenced %NULL): what an item leaving the history left, whichever way it
 * left. Here rather than when the item leaves the model, for two reasons. A
 * write still out with an earlier snapshot, in which the item was, would bring
 * a file deleted then back, and nothing would delete it again: the written
 * snapshot is the one point in the saver's order where what the store needs
 * is known. And the store may still need the file when the model no longer has
 * the item: a history that cannot be read, or one handed over to another
 * daemon, is never written, so it is never swept.
 *
 * The two file flavours share the directory. Each sweeps its own files, and a
 * plaintext ".png" is both the plain flavour's image and the encrypted one's
 * fallback for an entry whose ".pngs" is missing (add_item ()), so it stays
 * while either may read it: while a plain history of that name is stored and
 * references it -- or may, for an encrypted sweep, which cannot see that
 * history's references -- or while an encrypted one is stored and the ".pngs"
 * beside it is not. Past that it is the secret the encrypted flavour exists to
 * keep off the disk: a screenshot a migration to it left in clear.
 *
 * A temporary an image's write left, cut short by a crash before its move into
 * place, is nothing any history reads: either sweep takes it, a plain one
 * being a screenshot in clear.
 *
 * What is never touched: anything outside images/<history_name>/, and in it
 * anything but files named after a checksum the way this backend names its
 * own, or their temporaries. */
static void
_g_paste_file_backend_sweep_images (const gchar *history_name,
                                    gboolean     encrypted,
                                    GHashTable  *referenced)
{
    g_autofree gchar *images_dir = g_paste_file_backend_images_dir (history_name);
    g_autoptr (GFile) dir = g_file_new_for_path (images_dir);
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) files = g_paste_util_list_directory (dir, G_FILE_ATTRIBUTE_STANDARD_NAME, &error);

    if (!files)
    {
        g_warning ("Could not list the images of \"%s\" to remove the ones it no longer holds: %s", history_name, error->message);
        return;
    }

    gboolean plain_stored = encrypted && g_paste_util_history_is_stored (history_name, G_PASTE_STORAGE_FILE);
    gboolean encrypted_stored = !encrypted && g_paste_util_history_is_stored (history_name, G_PASTE_STORAGE_ENCRYPTED_FILE);

    for (GStrv file = files; *file; ++file)
    {
        g_autofree gchar *path = g_build_filename (images_dir, *file, NULL);

        if (_g_paste_file_backend_is_image_temp_name (*file))
        {
            if (g_unlink (path) && errno != ENOENT)
                g_warning ("Could not remove %s, which a cut-short write left: %s", path, g_strerror (errno));
            continue;
        }

        /* What a re-key left beside an encrypted image goes with the encrypted
         * history being deleted: the settle that would otherwise take it finds
         * a history by its document alone, gone now, and the directory itself
         * stays while a plain history of that name stores it. A write leaves
         * it to that settle. */
        if (_g_paste_file_backend_is_image_rekey_name (*file))
        {
            if (encrypted && !referenced && g_unlink (path) && errno != ENOENT)
                g_warning ("Could not remove %s, which a change of passphrase left: %s", path, g_strerror (errno));
            continue;
        }

        if (_g_paste_file_backend_is_image_file_name (*file, encrypted))
        {
            if (referenced && g_hash_table_contains (referenced, *file))
                continue;
        }
        else if (!encrypted || plain_stored || !_g_paste_file_backend_is_image_file_name (*file, FALSE))
            continue;

        /* A ".png", in either flavour's sweep: kept while an encrypted entry
         * may fall back on it. */
        if (!_g_paste_file_backend_is_image_file_name (*file, TRUE))
        {
            g_autofree gchar *side_file = g_paste_file_backend_encrypted_path (path);
            g_autofree gchar *side_name = g_path_get_basename (side_file);
            gboolean may_fall_back = (encrypted) ? referenced && g_hash_table_contains (referenced, side_name) : encrypted_stored;

            if (may_fall_back && !g_file_test (side_file, G_FILE_TEST_EXISTS))
                continue;
        }

        if (g_unlink (path) && errno != ENOENT)
            g_warning ("Could not remove the image %s, which its history no longer holds: %s", path, g_strerror (errno));
    }
}

/**
 * g_paste_file_backend_sweep_legacy_images:
 *
 * Delete the images GPaste 50 kept in the one directory every history shared
 * (images/<checksum>.png) that no history still references.
 *
 * GPaste 50 referenced them by absolute path, from plain histories alone, and
 * a history read from that layout copies each into its own images directory on
 * its next write, referencing the copy from then on: a shared file stays for
 * as long as a history GPaste 50 wrote has yet to be written again. To be run
 * before any history is read, since an image read from a shared file reloads
 * its pixels from it for as long as it is in memory.
 *
 * Nothing goes when a history file cannot be read: what it references is then
 * unknown.
 */
G_PASTE_VISIBLE void
g_paste_file_backend_sweep_legacy_images (void)
{
    g_autofree gchar *history_dir = g_paste_util_get_history_dir_path ();
    g_autofree gchar *images_dir = g_build_filename (history_dir, "images", NULL);
    g_autoptr (GFile) dir = g_file_new_for_path (images_dir);
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) images = g_paste_util_list_directory (dir, G_FILE_ATTRIBUTE_STANDARD_NAME, &error);

    if (!images)
    {
        g_warning ("Could not list the images GPaste 50 left: %s", error->message);
        return;
    }

    g_auto (GStrv) histories = NULL;
    g_autoptr (GPtrArray) contents = g_ptr_array_new_with_free_func (g_free);
    g_autofree gchar *suffix = g_strconcat (".", g_paste_storage_get_extension (G_PASTE_STORAGE_FILE), NULL);

    for (GStrv image = images; *image; ++image)
    {
        if (!_g_paste_file_backend_is_image_file_name (*image, FALSE))
            continue;

        /* Read the first time there is an image to look for, and only then:
         * once nothing is left in the shared
         * directory, a start costs a listing. */
        if (!histories)
        {
            g_autoptr (GFile) history_dir_file = g_file_new_for_path (history_dir);

            if (!(histories = g_paste_util_list_directory (history_dir_file, G_FILE_ATTRIBUTE_STANDARD_NAME, &error)))
            {
                g_warning ("Could not list the histories to know which images GPaste 50 left they reference: %s", error->message);
                return;
            }

            for (GStrv history = histories; *history; ++history)
            {
                if (!g_str_has_suffix (*history, suffix))
                    continue;

                g_autofree gchar *path = g_build_filename (history_dir, *history, NULL);
                gchar *text = NULL;

                if (!g_file_get_contents (path, &text, NULL, &error))
                {
                    g_warning ("Could not read %s to know which images GPaste 50 left it references: %s", path, error->message);
                    return;
                }
                g_ptr_array_add (contents, text);
            }
        }

        g_autofree gchar *path = g_build_filename (images_dir, *image, NULL);
        /* The path's tail alone: GPaste 50 spelt the data directory as it was
         * reached then, which a symlinked home or another XDG_DATA_HOME may no
         * longer match. A checksum name needs no encoding, and a match a
         * per-history path or a text item happens
         * to contain only keeps a file. */
        g_autofree gchar *reference = g_strconcat (G_DIR_SEPARATOR_S "images" G_DIR_SEPARATOR_S, *image, NULL);
        gboolean referenced = FALSE;

        for (guint i = 0; !referenced && i < contents->len; ++i)
            referenced = strstr (g_ptr_array_index (contents, i), reference) != NULL;

        if (!referenced && g_unlink (path) && errno != ENOENT)
            g_warning ("Could not remove the image %s, which no history references any more: %s", path, g_strerror (errno));
    }
}

static gboolean
g_paste_file_backend_write_history_file (GPasteStorageBackend *self,
                                         const gchar          *history_name,
                                         const GList          *history)
{
    if (!g_paste_util_ensure_history_dir_exists ())
        return FALSE;

    /* Images are referenced (and materialized) under the history being
     * written, which may not be the one the items belong to (a backup). */
    g_autofree gchar *history_file_path = g_paste_storage_backend_get_history_file_path (self, history_name);
    g_autoptr (GFile) history_file = g_file_new_for_path (history_file_path);

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* Nor written over while a change of passphrase waits to be settled, as
     * the read refuses it: the settle would put the original back over this
     * write, the history emptied coming back at the next start and whatever
     * was written since lost, with this write answered as held. */
    if (g_paste_file_backend_get_passphrase (self))
    {
        g_autofree gchar *marker = g_strconcat (history_file_path, G_PASTE_FILE_BACKEND_REKEY_NEW, NULL);

        if (g_file_test (marker, G_FILE_TEST_EXISTS))
        {
            g_warning ("The change of passphrase of %s did not finish; not writing over it until it is put back", history_file_path);
            return FALSE;
        }
    }
#endif

    /* A history this write would bring into being, which one that fails must
     * not leave behind: neither a document nor images no listing shows. */
    gboolean existed = g_file_test (history_file_path, G_FILE_TEST_IS_REGULAR);

    GPasteFileBackend *real_self = G_PASTE_FILE_BACKEND (self);
    /* Where the images go, and which of them the sweep compares against. */
    gboolean encrypted = g_paste_storage_backend_is_encrypted (self);

    g_autofree gchar *tmp_path = g_strconcat (history_file_path, ".tmp", NULL);
    g_autoptr (GFile) tmp_file = g_file_new_for_path (tmp_path);
    g_autoptr (GOutputStream) stream = G_PASTE_FILE_BACKEND_GET_CLASS (real_self)->get_output_stream (real_self, tmp_file);

    if (!stream)
        return FALSE;

    gboolean success = TRUE;
    /* Whether every image this snapshot references is on the disk: a history
     * written without one of them does not hold @history, whatever its document
     * says, and the write says so -- a backup missing its screenshots would
     * otherwise be answered as made. The saver's own writes try a missing one
     * again at the next, from the item's bytes. */
    gboolean images_written = TRUE;
    g_autoptr (GError) error = NULL;
    /* The image files this snapshot references, for the sweep once it is in. */
    g_autoptr (GHashTable) referenced = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

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

        if (!g_paste_storage_backend_stores_item (self, item))
            continue;

        const gchar *kind_str = g_paste_item_kind_to_string (kind);

        g_autofree gchar *image_reference = NULL;
        g_autofree gchar *image_value = NULL;
        g_autofree gchar *png_checksum = NULL;

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

            if (!_g_paste_file_backend_ensure_image_file (real_self, image, image_reference))
                images_written = FALSE;

            gchar *file_name = _g_paste_file_backend_image_file_name (image, encrypted);

#ifdef G_PASTE_ENABLE_ENCRYPTION
            if (encrypted)
            {
                g_autofree gchar *side_file = g_paste_file_backend_encrypted_path (image_reference);

                png_checksum = _g_paste_file_backend_png_checksum (self, side_file);
            }
#endif

            if (file_name)
                g_hash_table_add (referenced, file_name);

            /* A document holds UTF-8 alone, and the data directory and a
             * history name read off the disk need not be: a home directory in
             * a legacy 8-bit encoding. Such an image is named by its file
             * name alone, which a read finds under the history's images
             * directory as it finds every other (add_item ()), where writing
             * the whole path would end on a NULL encoding -- and take the
             * daemon, or gnome-shell, with it. GPaste 51.0, which read the
             * path alone, finds no image named so. */
            image_value = (g_utf8_validate (image_reference, -1, NULL)) ? g_strdup (image_reference) : g_path_get_basename (image_reference);
        }

        const GSList *special_values = g_paste_item_get_special_values (item);
        /* An image is written as the file this backend materializes it in --
         * its value, the checksum, rides along as an attribute -- so a history
         * file keeps naming the files beside it. For everything else the item's
         * own content is the text: get_value only differs from get_real_value
         * for passwords (it masks them), and those are skipped above unless
         * encrypted, so the real value is always what we want to persist. */
        g_autoptr (GPasteCleartext) text = g_paste_util_xml_encode ((image_value) ? image_value : g_paste_item_get_real_value (item));

        if (!g_output_stream_write_all (stream, "  <item kind=\"", 14, NULL, NULL /* cancellable */, &error) ||
            !g_output_stream_write_all (stream, kind_str, strlen (kind_str), NULL, NULL /* cancellable */, &error) ||
            !g_output_stream_write_all (stream, "\" uuid=\"", 8, NULL, NULL /* cancellable */, &error) ||
            !g_output_stream_write_all (stream, uuid, strlen (uuid), NULL, NULL /* cancellable */, &error) ||
            (G_PASTE_IS_PASSWORD_ITEM (item) && !_g_paste_file_backend_write_password_name (stream, G_PASTE_PASSWORD_ITEM (item), &error)) ||
            (G_PASTE_IS_IMAGE_ITEM (item) && !_g_paste_file_backend_write_image_metadata (stream, G_PASTE_IMAGE_ITEM (item), png_checksum, &error)) ||
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

    if (success && (!g_output_stream_close (stream, NULL /* cancellable */, &error) ||
                    !_g_paste_file_backend_sync (tmp_file, &error)))
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

    if (success)
        _g_paste_file_backend_sweep_images (history_name, encrypted, referenced);
    else if (!g_file_delete (tmp_file, NULL, &error) &&
             !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to delete history temp file: %s", error->message);

    if (success && images_written)
        return TRUE;

    /* What a failed write of a history that was not there made goes again: no
     * listing shows images under a name no history has, and nothing would ever
     * reach those screenshots -- left in clear by the plain flavour. */
    if (!existed)
    {
        if (success && g_unlink (history_file_path) && errno != ENOENT)
            g_warning ("Could not remove %s, which a failed write made: %s", history_file_path, g_strerror (errno));
        _g_paste_file_backend_sweep_images (history_name, encrypted, NULL);

        g_autofree gchar *images_dir = g_paste_file_backend_images_dir (history_name);

        g_rmdir (images_dir);
    }

    return FALSE;
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

/* 2.0 is the only format read. A 1.0 history -- which held an item's value as
 * text directly inside <item> rather than in a <value> child -- is refused like
 * any other unreadable one, which leaves the file untouched rather than
 * overwriting it, so an ancient history can still be recovered with an older
 * GPaste. */
typedef enum
{
    HISTORY_2_0,
    HISTORY_INVALID = -1
} HistoryVersion;

typedef struct
{
    GPasteStorageBackend *backend;
    /* The history being read, whose images directory an image is looked up in. */
    const gchar          *history_name;
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
    guint                 timeout;
    gchar                *uuid;
    /* The image date the attribute was checked as, kept rather than parsed a
     * second time. */
    GDateTime            *date;
    gchar                *checksum;
    /* The SHA-256 of the PNG an encrypted image's side file holds, when the
     * document says (_g_paste_file_backend_write_image_metadata ()). */
    gchar                *png_checksum;
    gchar                *name;
    gchar                *text;
    GSList               *special_values;
    HistoryVersion        version;
    GPasteSpecialMime     mime;
    /* A <value> whose mime this build does not know, which is skipped rather
     * than taken for the plain value @mime INVALID otherwise stands for. */
    gboolean              unknown_mime;
    /* The uuids of the items read so far, borrowed from them, so that one
     * repeated is given a new one in a single lookup. */
    GHashTable           *uuids;
    /* An element was found where the format has none, or GMarkup reported an
     * error, which only a document we did not write has: read as unreadable,
     * so that what the parse made of it is never saved over it. */
    gboolean              malformed;
    /* How deep inside an element this version does not know the parse is,
     * which is read around whole: what it holds, known elements included, is
     * a newer GPaste's or no GPaste's, and not the item's around it. */
    guint                 unknown_depth;
} Data;

/* Where the parser currently is, for a diagnostic. An encrypted history is
 * decrypted into memory before being parsed, so the file on disk has neither
 * these lines nor these offsets; the element we are inside and where it opened
 * are what identify the offending item whichever buffer we ended up parsing.
 *
 * @attr is the index, in the arrays the callback was handed, of the attribute
 * the diagnostic is about, or -1 for one that is about no attribute in
 * particular. It is worth naming because the parser's own position is of no
 * use for an attribute: a callback runs once the whole start tag is read, so
 * every attribute of one item reports the tag's end and they all report the
 * same place. Only start_tag () may pass one, being the one callback in which
 * g_markup_parse_context_get_attribute_position () is defined. */
static gchar *
parse_location (GMarkupParseContext *context,
                const Data          *data,
                gssize               attr)
{
    const gchar *element = g_markup_parse_context_get_element (context);
    g_autofree gchar *where = NULL;

    /* Where we are, said once; being inside an element only adds to it. */
    if (attr < 0)
    {
        gint line_number, char_number;

        g_markup_parse_context_get_position (context, &line_number, &char_number);
        where = g_strdup_printf ("in file “%s” at line %" G_GINT32_FORMAT
                                 ", column %" G_GINT32_FORMAT " (byte %" G_GSIZE_FORMAT ")",
                                 data->history_file_path, line_number, char_number,
                                 g_markup_parse_context_get_offset (context));
    }
    else
    {
        /* Zeroed: an index past the attributes the callback was handed is one
         * get_attribute_position () refuses, leaving these untouched -- and the
         * warning would then print four uninitialized numbers as a place in a
         * file. Only WARN_AT_ATTR passes an index at all, and only from the
         * loop over those very arrays, so nothing reaches that today. */
        gsize attr_line = 0, attr_char = 0, attr_start = 0, attr_end = 0;

        /* The span rather than a point: a name="value" assignment is what the
         * warning is about, and its end is where the value stops. */
        g_markup_parse_context_get_attribute_position (context, (guint) attr,
                                                       &attr_line, &attr_char, &attr_start,
                                                       NULL, NULL, &attr_end);
        where = g_strdup_printf ("in file “%s” at line %" G_GSIZE_FORMAT
                                 ", column %" G_GSIZE_FORMAT
                                 " (bytes %" G_GSIZE_FORMAT "-%" G_GSIZE_FORMAT ")",
                                 data->history_file_path, attr_line, attr_char,
                                 attr_start, attr_end);
    }

    if (!element)
        return g_steal_pointer (&where);

    gsize tag_line, tag_char, tag_offset;

    g_markup_parse_context_get_tag_start (context, &tag_line, &tag_char, &tag_offset);

    return g_strdup_printf ("%s, inside <%s> opened at line %" G_GSIZE_FORMAT " (byte %" G_GSIZE_FORMAT ")",
                            where, element, tag_line, tag_offset);
}

/* A kind to name in a diagnostic. g_paste_item_kind_to_string () answers NULL
 * for G_PASTE_ITEM_KIND_INVALID, deliberately -- it is the serialised form, and
 * INVALID is registered with no nick precisely so that nothing writes one to a
 * store or hands one to a binding. Here, where the answer is prose rather than
 * a value, INVALID has a name like any other. */
static const gchar *
parse_kind_name (GPasteItemKind kind)
{
    const gchar *name = g_paste_item_kind_to_string (kind);

    return (name) ? name : "Invalid";
}

/* g_warning() with the parse location appended. Needs @context and @data in
 * scope, which every parser callback has. */
#define WARN_AT(fmt, ...)                                                \
    do {                                                                 \
        g_autofree gchar *location = parse_location (context, data, -1); \
        g_warning (fmt " %s.", ##__VA_ARGS__, location);                 \
    } while (0)

/* WARN_AT for something one attribute says, pointing at that assignment rather
 * than at the end of the start tag. @a is the cursor the attribute loops walk
 * @attribute_names with, so it also needs that array in scope: start_tag () is
 * the only caller, and the only one allowed to be. */
#define WARN_AT_ATTR(a, fmt, ...)                                        \
    do {                                                                 \
        g_autofree gchar *location = parse_location (context, data,      \
                                                     (a) - attribute_names); \
        g_warning (fmt " %s.", ##__VA_ARGS__, location);                 \
    } while (0)

#define ASSERT_STATE_FULL(cond, x)                                                       \
    if (!(cond))                                                                         \
    {                                                                                    \
        WARN_AT ("Expected state %" G_GINT32_FORMAT ", but got %" G_GINT32_FORMAT,       \
                 x, data->state);                                                        \
        data->malformed = TRUE;                                                          \
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

static void
start_tag (GMarkupParseContext *context,
           const gchar         *element_name,
           const gchar        **attribute_names,
           const gchar        **attribute_values,
           gpointer             user_data,
           GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    if (data->unknown_depth)
    {
        ++data->unknown_depth;
        return;
    }

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
                        WARN_AT_ATTR (a, "History version 1.0 is no longer supported; the file is left untouched, load it with GPaste 2 to convert it");
                    else
                        WARN_AT_ATTR (a, "Unknown history version: %s", *v);
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
        data->timeout = 0;
        g_clear_pointer (&data->uuid, g_free);
        g_clear_pointer (&data->date, g_date_time_unref);
        g_clear_pointer (&data->checksum, g_free);
        g_clear_pointer (&data->png_checksum, g_free);
        g_clear_pointer (&data->name, g_paste_util_free_cleartext);
        g_clear_pointer (&data->text, g_paste_util_free_cleartext);
        g_clear_slist (&data->special_values, g_object_unref);
        for (const gchar **a = attribute_names, **v = attribute_values; *a && *v; ++a, ++v)
        {
            if (g_paste_str_equal (*a, "kind"))
            {
                data->type = g_paste_item_kind_from_string (*v);
                if (data->type == G_PASTE_ITEM_KIND_INVALID)
                    WARN_AT_ATTR (a, "Unknown item kind: %s", *v);
            }
            /* Each attribute may be given twice, which GMarkup lets through:
             * the last one is what holds. */
            else if (g_paste_str_equal (*a, "uuid"))
            {
                if (g_uuid_string_is_valid (*v) && !g_hash_table_contains (data->uuids, *v))
                    g_set_str (&data->uuid, *v);
            }
            /* An attribute that does not belong to this kind is skipped, not a
             * reason to abandon the item: returning here would also drop the
             * attributes that follow it (the uuid among them). */
            else if (g_paste_str_equal (*a, "date"))
            {
                if (data->type != G_PASTE_ITEM_KIND_IMAGE)
                {
                    WARN_AT_ATTR (a, "Expected an Image item, but the kind is %s", parse_kind_name (data->type));
                    continue;
                }

                gint64 date = 0;
                g_autoptr (GDateTime) date_time = NULL;

                /* Checked here rather than when the item is built, so that a
                 * count skips what a read would: a date GDateTime cannot hold is
                 * as good as none. */
                if (!g_ascii_string_to_signed (*v, 10, G_MININT64, G_MAXINT64, &date, NULL) ||
                    !(date_time = g_date_time_new_from_unix_local (date)))
                {
                    WARN_AT_ATTR (a, "Invalid image date: %s", *v);
                    continue;
                }
                g_clear_pointer (&data->date, g_date_time_unref);
                data->date = g_steal_pointer (&date_time);
            }
            else if (g_paste_str_equal (*a, "checksum"))
            {
                if (data->type != G_PASTE_ITEM_KIND_IMAGE)
                {
                    WARN_AT_ATTR (a, "Expected an Image item, but the kind is %s", parse_kind_name (data->type));
                    continue;
                }
                /* Recomputed from the image when it is not one, which the item
                 * would do anyway: said here, where the document is. */
                if (!g_paste_image_item_is_checksum (*v))
                {
                    WARN_AT_ATTR (a, "Invalid image checksum");
                    continue;
                }
                g_set_str (&data->checksum, *v);
            }
            else if (g_paste_str_equal (*a, "png"))
            {
                if (data->type != G_PASTE_ITEM_KIND_IMAGE)
                {
                    WARN_AT_ATTR (a, "Expected an Image item, but the kind is %s", parse_kind_name (data->type));
                    continue;
                }
                /* A malformed one names no file: the image is then checked by
                 * its pixels, as one the document says nothing of is. */
                if (!g_paste_image_item_is_checksum (*v))
                {
                    WARN_AT_ATTR (a, "Invalid image PNG checksum");
                    continue;
                }
                g_set_str (&data->png_checksum, *v);
            }
            else if (g_paste_str_equal (*a, "name"))
            {
                if (data->type != G_PASTE_ITEM_KIND_PASSWORD)
                {
                    WARN_AT_ATTR (a, "Expected a Password item, but the kind is %s", parse_kind_name (data->type));
                    continue;
                }
                g_clear_pointer (&data->name, g_paste_util_free_cleartext);
                data->name = g_strdup (*v);
            }
            else if (g_paste_str_equal (*a, "timeout"))
            {
                if (data->type != G_PASTE_ITEM_KIND_PASSWORD)
                {
                    WARN_AT_ATTR (a, "Expected a Password item, but the kind is %s", parse_kind_name (data->type));
                    continue;
                }
                /* Read wide and clamped, never cast: a stored value past
                 * G_MAXUINT truncates, and 2^32 truncates to 0 -- the one value
                 * that means the password never comes off the clipboard. Nor
                 * read leniently: what is no number would read as that 0, and
                 * no GPaste wrote it. */
                guint64 timeout = 0;

                if (!g_ascii_string_to_unsigned (*v, 10, 0, G_MAXUINT64, &timeout, NULL))
                {
                    WARN_AT_ATTR (a, "Invalid password timeout: %s", *v);
                    data->malformed = TRUE;
                    continue;
                }

                if (timeout > G_PASTE_PASSWORD_TIMEOUT_MAX)
                {
                    WARN_AT_ATTR (a, "Password timeout out of range: %s", *v);
                    timeout = G_PASTE_PASSWORD_TIMEOUT_MAX;
                }

                data->timeout = (guint) timeout;
            }
            else if (g_paste_str_equal (*a, "favourite"))
                data->favourite = g_paste_str_equal (*v, "true");
            else
                WARN_AT_ATTR (a, "Unknown item attribute: %s", *a);
        }
    }
    else if (g_paste_str_equal (element_name, "value"))
    {
        SWITCH_STATE (IN_ITEM, IN_VALUE);
        data->mime = G_PASTE_SPECIAL_MIME_INVALID;
        data->unknown_mime = FALSE;
        for (const gchar **a = attribute_names, **v = attribute_values; *a && *v; ++a, ++v)
        {
            if (g_paste_str_equal (*a, "mime"))
            {
                GEnumValue *gev = g_enum_get_value_by_nick (g_type_class_get (G_PASTE_TYPE_SPECIAL_MIME), *v);

                data->unknown_mime = !gev;
                if (gev)
                    data->mime = gev->value;
                else
                    WARN_AT_ATTR (a, "Unknown mime: %s", *v);
            }
        }
    }
    else
    {
        WARN_AT ("Unknown element: %s", element_name);
        /* A value is text: markup in one makes it no value GPaste wrote. */
        if (data->state == IN_VALUE || data->state == IN_VALUE_WITH_TEXT)
            data->malformed = TRUE;
        data->unknown_depth = 1;
    }
}

/* An encrypted history's image, read only when it is the one the history names.
 * The history is authenticated, but nothing binds an image to it: not the
 * plaintext file at its reference, which anyone able to write the images
 * directory could put any pixels in for the next save to encrypt as the user's
 * own, nor the encrypted side file, which carries nothing of its name and could
 * be another of the user's own put in its place. The document names the PNG
 * its side file holds, by @png_checksum, when it was written knowing it; else
 * the image is checked by its pixels, against its @checksum.
 *
 * @side_file is where @png was decrypted from, which this backend then knows
 * the PNG of, or %NULL for the plaintext fallback. */
static GPasteItem *
_g_paste_file_backend_named_image (GPasteStorageBackend *self,
                                   const gchar          *path,
                                   const gchar          *side_file,
                                   GBytes               *png,
                                   GDateTime            *date,
                                   const gchar          *checksum,
                                   const gchar          *png_checksum)
{
    g_autofree gchar *computed = g_compute_checksum_for_bytes (G_CHECKSUM_SHA256, png);
    g_autoptr (GPasteItem) item = NULL;

    if (png_checksum)
    {
        if (g_paste_str_equal (computed, png_checksum))
            item = g_paste_image_item_new_from_bytes_at_path (path, png, date, checksum);
    }
    else
    {
        item = g_paste_image_item_new_from_bytes_at_path (path, png, date, NULL /* computed */);

        if (item && !g_paste_str_equal (g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item)), checksum))
            g_clear_object (&item);
    }

    if (!item)
    {
        g_warning ("Ignoring the image at %s, which is not the one its encrypted history names", path);
        return NULL;
    }

#ifdef G_PASTE_ENABLE_ENCRYPTION
    if (side_file)
        _g_paste_file_backend_remember_png (self, side_file, computed);
#else
    (void) self;
    (void) side_file;
#endif

    return g_steal_pointer (&item);
}

/* The encrypted flavour's fallback for an image whose side file is missing:
 * the plaintext file at its reference, read as
 * _g_paste_file_backend_named_image () says, by its
 * pixels: @png_checksum names a side file's PNG. */
static GPasteItem *
_g_paste_file_backend_read_fallback_image (GPasteStorageBackend *self,
                                           const gchar          *path,
                                           GDateTime            *date,
                                           const gchar          *checksum)
{
    gchar *contents = NULL;
    gsize length = 0;

    if (!checksum || !g_file_get_contents (path, &contents, &length, NULL))
        return NULL;

    g_autoptr (GBytes) png = g_bytes_new_take (contents, length);

    return _g_paste_file_backend_named_image (self, path, NULL, png, date, checksum, NULL);
}

/* Where an image of the history read is: in its images directory, under the
 * name its checksum gives it, whatever path the document stored -- one spelling
 * the data directory as it was reached then, which a moved or renamed home, or
 * another XDG_DATA_HOME, no longer matches. The stored path is read only when
 * nothing is there: an image of GPaste 50's shared directory, which the next
 * save moves into the history's own. */
static gchar *
_g_paste_file_backend_image_location (const Data *data)
{
    if (!data->checksum)
        return g_strdup (data->text);

    g_autofree gchar *derived = g_paste_file_backend_image_path (data->history_name, data->checksum);

    if (g_file_test (derived, G_FILE_TEST_EXISTS))
        return g_steal_pointer (&derived);

    if (g_paste_storage_backend_is_encrypted (data->backend))
    {
        g_autofree gchar *side_file = g_paste_file_backend_encrypted_path (derived);

        if (g_file_test (side_file, G_FILE_TEST_EXISTS))
            return g_steal_pointer (&derived);
    }

    return g_strdup (data->text);
}

static void
add_item (Data *data)
{
    GPasteItem *item = NULL;

    if (data->count_only)
    {
        /* What the switch below would have built, decided rather than done: an
         * item needs a value and a usable kind, and an image needs images turned
         * on and a date to restore. Not foreseen here is what only building the
         * item finds -- a value a constructor rejects (a colour that does not
         * parse), an image whose file is missing, will not decode or decrypt --
         * so a damaged history can count high by those, and by what the cap
         * then let in in their place; the read warns about each. */
        gboolean countable = data->text && data->type != G_PASTE_ITEM_KIND_INVALID &&
                             (data->type != G_PASTE_ITEM_KIND_IMAGE || (data->images_support && data->date));

        if (countable)
        {
            ++data->count;

            if (!data->favourite)
                ++data->current_size;
        }

        g_clear_slist (&data->special_values, g_object_unref);

        return;
    }

    /* Every kind is built from the item's value, and every constructor rejects a
     * %NULL one with a critical: an <item> with no usable kind, or with no
     * <value> at all (absent or empty), carries nothing we can restore and is
     * skipped — along with its special values, released below. */
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
        item = g_paste_password_item_new (data->name, data->text, data->timeout);
        break;
    case G_PASTE_ITEM_KIND_COLOR:
        item = g_paste_color_item_new_from_str (data->text);
        break;
    case G_PASTE_ITEM_KIND_IMAGE:
        if (data->images_support && data->date)
        {
            GDateTime *date_time = data->date;
            /* The encrypted flavor stores the image in an encrypted side file
             * rather than at the referenced (plaintext) path. Either way the
             * item keeps the path it was read at: that is where its
             * materialized data actually lives. */
            g_autofree gchar *path = _g_paste_file_backend_image_location (data);
            g_autoptr (GBytes) png = _g_paste_file_backend_load_image_bytes (data->backend, path);

            if (png && data->checksum && g_paste_storage_backend_is_encrypted (data->backend))
            {
                g_autofree gchar *side_file = g_paste_file_backend_encrypted_path (path);

                item = _g_paste_file_backend_named_image (data->backend, path, side_file, png, date_time, data->checksum, data->png_checksum);
            }
            else if (png)
                item = g_paste_image_item_new_from_bytes_at_path (path, png, date_time, data->checksum);
            else if (g_paste_storage_backend_is_encrypted (data->backend))
                item = _g_paste_file_backend_read_fallback_image (data->backend, path, date_time, data->checksum);
            else if (g_utf8_validate (path, -1, NULL))
                item = g_paste_image_item_new_from_file (path, date_time, data->checksum);
            /* g_paste_image_item_new_from_file () takes a UTF-8 path alone, which
             * a data directory in a legacy 8-bit encoding does not give: such an
             * image is read into bytes instead. */
            else
            {
                gchar *contents = NULL;
                gsize length = 0;

                if (g_file_get_contents (path, &contents, &length, NULL))
                {
                    g_autoptr (GBytes) bytes = g_bytes_new_take (contents, length);

                    item = g_paste_image_item_new_from_bytes_at_path (path, bytes, date_time, data->checksum);
                }
            }
        }
        /* An image read with images turned off, or without the date to restore
         * it with, is skipped and its file left where it is: this may be no read
         * the history is saved from (another history sized up, an import, a
         * document cut short), and the next full write of it sweeps what it no
         * longer references. */
        break;
    case G_PASTE_ITEM_KIND_INVALID:
        break;
    }

    if (item)
    {
        if (!data->uuid)
            data->uuid = g_uuid_string_random ();

        g_paste_item_set_uuid (item, data->uuid);
        g_hash_table_add (data->uuids, (gpointer) g_paste_item_get_uuid (item));
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

    if (data->unknown_depth)
    {
        --data->unknown_depth;
        return;
    }

    if (g_paste_str_equal (element_name, "history"))
        SWITCH_STATE (IN_HISTORY, END);
    else if (g_paste_str_equal (element_name, "item"))
    {
        /* Leave the item even when the version is unknown: staying inside it
         * would make every element that follows fail its assert — including the
         * next <item>, whose scratch reset would then be skipped, so the items
         * of an unreadable history would bleed into each other. And add nothing
         * from any other state: an </item> closing no item of ours closes one
         * whose start was refused, and what is in scratch is the item before. */
        ASSERT_STATE (IN_ITEM);

        /* An unknown (or missing) version collects neither the item's value nor
         * its special values (see on_text), so there is nothing to restore.
         * A favourite is always taken: the cap does not apply to it, and
         * current_size counts only the items it does apply to. */
        if (data->version != HISTORY_INVALID && (data->favourite || data->current_size < data->max_size))
            add_item (data);
        data->state = IN_HISTORY;
    }
    else if (g_paste_str_equal (element_name, "value"))
        SWITCH_STATE_OR_EMPTY (IN_VALUE_WITH_TEXT, IN_VALUE, IN_ITEM);
}

/* Whether @text is whitespace alone, by g_strstrip ()'s measure. */
static gboolean
_g_paste_file_backend_is_blank (const gchar *text)
{
    for (; *text; ++text)
    {
        if (!g_ascii_isspace (*text))
            return FALSE;
    }

    return TRUE;
}

static void
on_text (GMarkupParseContext *context,
         const gchar         *text,
         gsize                text_len,
         gpointer             user_data,
         GError             **error G_GNUC_UNUSED)
{
    Data *data = user_data;

    if (data->unknown_depth)
        return;

    /* A count asks whether an item has a value, not what it is (add_item ()):
     * a value is neither copied, decoded nor wiped then, and a special one,
     * which the count drops, not decoded at all -- the states moving as they
     * do below, so that a document a read refuses is one a count refuses too.
     * A placeholder stands for the value, a count being held to what a read
     * finds (/history/file_count_matches_read). */
    if (data->count_only && data->state == IN_VALUE && data->version == HISTORY_2_0)
    {
        gboolean plain = data->mime == G_PASTE_SPECIAL_MIME_INVALID && !data->unknown_mime;
        gboolean blank = TRUE;

        for (gsize i = 0; blank && i < text_len; ++i)
            blank = g_ascii_isspace (text[i]);

        if (blank)
        {
            if (plain && text_len && !data->text)
                data->text = g_strdup (" ");
            return;
        }

        SWITCH_STATE (IN_VALUE, IN_VALUE_WITH_TEXT);
        if (plain && !data->text)
            data->text = g_strdup (" ");
        return;
    }

    /* A decrypted password, as likely as not, as is everything decoded from it
     * below: wiped on its way out (see #GPasteCleartext), and never stripped in
     * place, which would move its bytes past where a wipe by its length ends. */
    g_autoptr (GPasteCleartext) txt = g_strndup (text, text_len);
    switch (data->state)
    {
    case IN_HISTORY:
    /* Text out of place is still the history's, a 1.0 document's values among
     * it -- passwords included, when an old file kept any -- and the journal
     * outlives the history it would be copied from, with readers of its own:
     * its length is all that is said of it. */
    case IN_VALUE_WITH_TEXT:
        if (!_g_paste_file_backend_is_blank (txt))
        {
            WARN_AT ("Unexpected text (%" G_GSIZE_FORMAT " bytes)", text_len);
            return;
        }
        break;
    case IN_ITEM:
        /* An item's value lives in a <value> child; text directly inside <item>
         * is the 1.0 layout, which is refused (see HistoryVersion). */
        if (!_g_paste_file_backend_is_blank (txt))
            WARN_AT ("Unexpected text in item (%" G_GSIZE_FORMAT " bytes)", text_len);
        break;
    case IN_VALUE:
        if (data->version == HISTORY_2_0)
        {
            g_autoptr (GPasteCleartext) value = g_paste_util_xml_decode (txt);
            gboolean plain = data->mime == G_PASTE_SPECIAL_MIME_INVALID && !data->unknown_mime;

            /* GMarkup hands the runs either side of a CDATA section over apart
             * from it, so whitespace alone may be the indentation around the
             * value rather than the value: it stands only until a run that is
             * not whitespace comes. A plain value of whitespace alone -- blank
             * lines copied -- is written verbatim and kept, where a special
             * value is base64, which whitespace alone is none of. */
            if (_g_paste_file_backend_is_blank (txt))
            {
                if (plain && *value && !data->text)
                    data->text = g_steal_pointer (&value);
                break;
            }

            SWITCH_STATE (IN_VALUE, IN_VALUE_WITH_TEXT);
            if (plain)
            {
                g_clear_pointer (&data->text, g_paste_util_free_cleartext);
                data->text = g_steal_pointer (&value);
            }
            else if (!data->unknown_mime)
            {
                gsize raw_length;
                g_autofree guchar *raw = g_base64_decode (value, &raw_length);

                /* Base64 decoding to nothing (a damaged file) is no
                 * representation, which GPasteBinaryData refuses; the item is
                 * still read from its plain value. */
                if (raw_length)
                {
                    GBytes *bytes = g_bytes_new_take (g_steal_pointer (&raw), raw_length);

                    data->special_values = g_slist_prepend (data->special_values, g_paste_binary_data_new (data->mime, bytes));
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

/* What GMarkup found wrong, without its message: that quotes the document -- a
 * value not in UTF-8 whole, an entity in a password's name -- and the journal
 * outlives the history (see on_text ()). Every error the parse reports is
 * GMarkup's own, the callbacks setting none. */
static const gchar *
markup_error_description (const GError *error)
{
    switch (error->code)
    {
    case G_MARKUP_ERROR_BAD_UTF8:
        return "text not in UTF-8";
    case G_MARKUP_ERROR_EMPTY:
        return "an empty document";
    default:
        return "markup that does not parse";
    }
}

static void
on_error (GMarkupParseContext *context,
          GError              *error,
          gpointer             user_data)
{
    Data *data = user_data;

    /* GMarkup reads on past a CDATA section not in UTF-8, the parse still
     * succeeding without the item that held it. */
    data->malformed = TRUE;
    WARN_AT ("History document holding %s", markup_error_description (error));
}

/******************/
/* End XML Parser */
/******************/

/* Load the raw history document, transparently decrypting it for an encrypted
 * backend. Returns the (caller-owned) bytes through @text / @text_length.
 * @opened, when given, says whether an encrypted file opened, so that a failure
 * past that is the stream's own: a file the user may not read fails its open
 * with the very %G_IO_ERROR_PERMISSION_DENIED the converter answers a wrong key
 * with. A @side_file -- an image's, which the checksum its history holds
 * vouches for -- ends at its stream's FINAL chunk, what follows it left
 * (g_paste_secret_stream_converter_end_at_final ()): refused, the image would
 * be dropped by the read and its file deleted by the next save. A history
 * document is refused over it, being read as unreadable and so never written
 * over. */
static gboolean
g_paste_file_backend_load_contents (GPasteStorageBackend *self,
                                    const gchar          *history_file_path,
                                    GFile                *history_file,
                                    gboolean              side_file G_GNUC_UNUSED,
                                    gchar               **text,
                                    gsize                *text_length,
                                    gboolean             *opened,
                                    GError              **error)
{
    if (opened)
        *opened = FALSE;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    const gchar *passphrase = g_paste_file_backend_get_passphrase (self);

    if (passphrase)
    {
        g_autoptr (GFileInputStream) file_in = g_file_read (history_file, NULL, error);

        if (!file_in)
            return FALSE;
        if (opened)
            *opened = TRUE;

        g_autoptr (GFileInfo) info = g_file_input_stream_query_info (file_in, G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                                                     NULL, /* cancellable */
                                                                     error);

        if (!info)
            return FALSE;

        /* The placeholder read_history_file() writes for a history that exists
         * but has never been stored to. An empty file holds no stream at all, so
         * the converter reports it as a truncation -- and a history that reads
         * back as unreadable is one the daemon stops persisting, which is what a
         * brand new encrypted history hit on its first restart. Asked of the
         * stream we already opened, so nothing can swap the file underneath. */
        if (!g_file_info_get_size (info))
        {
            *text = g_strdup ("");
            *text_length = 0;

            return TRUE;
        }

        goffset size = g_file_info_get_size (info);

        /* A file past what this flavour ever stores -- a sparse one, a sync
         * tool's placeholder -- is no stream of ours, and allocating for it
         * would abort the process hosting the daemon, gnome-shell for the
         * in-shell one. */
        if ((guint64) size > G_PASTE_FILE_BACKEND_MAX_STREAM_SIZE)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "%s is larger than anything GPaste stores", history_file_path);
            return FALSE;
        }

        /* Sized once from the file, which the plaintext is shorter than, and the
         * terminator below: a buffer grown as the stream decrypts would leave a
         * copy of what it held so far in every block it was moved out of, for
         * whatever allocates it next (see #GPasteCleartext). Zeroed by the
         * allocator, pages of it untouched until written: the way out of a
         * failure wipes what was written, not the whole of it, which would
         * commit every page of a large file the stream refused at its first
         * bytes.
         *
         * The plain flavour hands back what g_file_get_contents() wrote, which is
         * NUL-terminated; a #GPasteCleartext is wiped by strlen(), and this one
         * is too, zeroed and a byte longer than anything it may hold. */
        gsize capacity = (gsize) size + 1;
        g_autofree gchar *data = g_try_malloc0 (capacity);

        if (!data)
        {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Not enough memory to read %s", history_file_path);
            return FALSE;
        }

        /* Decrypted from the ciphertext read off the file straight into @data,
         * through the converter alone: a GConverterInputStream converts into a
         * buffer of its own -- ordinary heap, freed unwiped -- whenever the room
         * it is handed takes no output, and grows its input buffer to whatever
         * a read asks for, the whole file. Only @data and the converter's own
         * wiped buffers hold the plaintext, and only the ciphertext goes
         * through @cipher. */
        g_autoptr (GConverter) converter = g_paste_secret_stream_converter_new (G_PASTE_SECRET_STREAM_DECRYPT, passphrase);
        g_autofree gchar *cipher = g_malloc (G_PASTE_FILE_BACKEND_READ_SIZE);

        if (side_file)
            g_paste_secret_stream_converter_end_at_final (G_PASTE_SECRET_STREAM_CONVERTER (converter));
        gsize cipher_length = 0;
        gsize length = 0;
        gboolean at_end = FALSE;
        gboolean finished = FALSE;

        while (!finished)
        {
            if (!at_end && cipher_length < G_PASTE_FILE_BACKEND_READ_SIZE)
            {
                gssize got = g_input_stream_read (G_INPUT_STREAM (file_in), cipher + cipher_length,
                                                  G_PASTE_FILE_BACKEND_READ_SIZE - cipher_length, NULL /* cancellable */, error);

                if (got < 0)
                    break;
                at_end = !got;
                cipher_length += (gsize) got;
            }

            gsize consumed = 0;
            gsize produced = 0;
            g_autoptr (GError) convert_error = NULL;
            /* The end of the input is what has the converter check its FINAL
             * tag, a stream cut short failing here. */
            GConverterResult result = g_converter_convert (converter, cipher, cipher_length, data + length, capacity - 1 - length,
                                                           (at_end) ? G_CONVERTER_INPUT_AT_END : G_CONVERTER_NO_FLAGS,
                                                           &consumed, &produced, &convert_error);

            /* More of the file, while there is room for it: a frame never
             * outgrows @cipher. */
            if (result == G_CONVERTER_ERROR && !at_end && cipher_length < G_PASTE_FILE_BACKEND_READ_SIZE &&
                g_error_matches (convert_error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT))
                continue;

            if (result == G_CONVERTER_ERROR || (at_end && result != G_CONVERTER_FINISHED && !consumed && !produced))
            {
                if (convert_error)
                    g_propagate_error (error, g_steal_pointer (&convert_error));
                else
                    g_set_error (error, G_IO_ERROR, G_IO_ERROR_PARTIAL_INPUT, "%s ends before its stream does", history_file_path);
                break;
            }

            memmove (cipher, cipher + consumed, cipher_length - consumed);
            cipher_length -= consumed;
            length += produced;
            finished = (result == G_CONVERTER_FINISHED);
        }

        if (!finished)
        {
            g_paste_util_wipe (data, length);
            return FALSE;
        }

        *text_length = length;
        *text = g_steal_pointer (&data);

        return TRUE;
    }
#else
    (void) self;
    (void) history_file;
#endif

    return g_file_get_contents (history_file_path, text, text_length, error);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
static gchar *
_g_paste_file_backend_rekey_sibling (const gchar *path,
                                     const gchar *suffix)
{
    return g_strconcat (path, suffix, NULL);
}

/* Remove the @suffix sibling of @path, saying so when it is there and will not
 * go: an original kept aside is the history under the passphrase being
 * replaced, which may be why it is being replaced. */
static void
_g_paste_file_backend_remove_rekey_sibling (const gchar *path,
                                            const gchar *suffix)
{
    g_autofree gchar *sibling = _g_paste_file_backend_rekey_sibling (path, suffix);

    if (g_unlink (sibling) && errno != ENOENT)
        g_warning ("Could not remove %s: %s", sibling, g_strerror (errno));
}
#endif

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
                                     GCancellable         *cancellable,
                                     gboolean              count_only,
                                     GList               **history,
                                     gsize                *size,
                                     gsize                *count)
{
    g_autofree gchar *history_file_path = g_paste_storage_backend_get_history_file_path (self, name);
    g_autoptr (GFile) history_file = g_file_new_for_path (history_file_path);
    /* The decrypted document, password values and all, for the encrypted
     * flavour: wiped rather than merely freed once it has been parsed. */
    g_autoptr (GPasteCleartext) text = NULL;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    /* A re-key that never reached its last rename can have left images under
     * the new passphrase beside a history under the old one, which a read
     * would drop for not decrypting, and the next save lose for good: such a
     * history waits for g_paste_file_backend_settle_rekeys(). The history's
     * replacement is what says so, and a re-key under way has one too, so a
     * count only answers nothing rather than warn. */
    if (g_paste_file_backend_get_passphrase (self))
    {
        g_autofree gchar *marker = _g_paste_file_backend_rekey_sibling (history_file_path, G_PASTE_FILE_BACKEND_REKEY_NEW);

        if (g_file_test (marker, G_FILE_TEST_EXISTS))
        {
            if (!count_only)
                g_warning ("The change of passphrase of %s did not finish; not reading it until it is put back", history_file_path);

            return FALSE;
        }
    }
#endif

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
        g_autoptr (GHashTable) uuids = g_hash_table_new (g_str_hash, g_str_equal);
        Data data = {
            self,
            name,
            history_file_path,
            NULL,
            0,
            BEGIN,
            G_PASTE_ITEM_KIND_INVALID, /* set per item from its "kind" attribute */
            0,
            g_paste_storage_backend_get_read_cap (self),
            g_paste_storage_backend_reads_images (self),
            count_only,
            0, /* count */
            FALSE, /* favourite: set per item from its "favourite" attribute */
            0, /* timeout: set per password from its "timeout" attribute */
            NULL, /* uuid */
            NULL, /* date */
            NULL, /* checksum */
            NULL, /* png_checksum */
            NULL, /* name */
            NULL, /* text */
            NULL, /* special_values */
            HISTORY_INVALID,
            G_PASTE_SPECIAL_MIME_INVALID,
            FALSE, /* unknown_mime: set per value from its "mime" attribute */
            uuids,
            FALSE, /* malformed */
            0, /* unknown_depth */
        };

        g_autoptr (GMarkupParseContext) ctx = g_markup_parse_context_new (&parser,
                                                                          G_MARKUP_TREAT_CDATA_AS_TEXT,
                                                                          &data,
                                                                          NULL);
        gsize text_length;
        g_autoptr (GError) error = NULL;

        /* Asked here and nowhere else: the whole document is read (and, for the
         * encrypted flavour, decrypted) in one call below and parsed in one
         * more, neither of which stops halfway, so this is the only point at
         * which giving up saves the work rather than merely discarding it.
         * Answered as unreadable, a history a cancel got in front of being one
         * nothing may persist over. */
        if (g_cancellable_is_cancelled (cancellable))
            return FALSE;

        if (!g_paste_file_backend_load_contents (self, history_file_path, history_file, FALSE, &text, &text_length, NULL, &error))
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

        /* No GError: on_error () reports each, and a second one set over the
         * first, after a CDATA section the parse read on past, would have GLib
         * log its message.
         *
         * GMarkup copies each text it hands over into buffers of its own, which
         * it frees as they are with the context: a password it read stays in
         * freed memory, beyond anything here to wipe. */
        gboolean parsed = g_markup_parse_context_parse (ctx, text, text_length, NULL) &&
                          g_markup_parse_context_end_parse (ctx, NULL);

        /* The context is still alive here (it is freed at scope exit), so a
         * truncated file can say where it stopped rather than just how. */
        if (!parsed || data.state != END)
        {
            g_autofree gchar *location = parse_location (ctx, &data, -1);

            if (!parsed)
                g_warning ("Failed to parse history file %s", location);

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
        g_clear_pointer (&data.date, g_date_time_unref);
        g_clear_pointer (&data.checksum, g_free);
        g_clear_pointer (&data.png_checksum, g_free);
        g_clear_pointer (&data.name, g_paste_util_free_cleartext);
        g_clear_pointer (&data.text, g_paste_util_free_cleartext);
        /* add_item() consumes these at every </item>; a document that ends inside
         * one (truncated file, failed parse) leaves the last item's behind. */
        g_clear_slist (&data.special_values, g_object_unref);

        /* An unknown version is reported as a read failure, not as an empty
         * history: its items were all skipped (we know neither where their value
         * lives nor how to decode it), so letting it pass as readable would have
         * the caller persist that empty model over the file on the very next
         * clipboard change — destroying exactly what the untouched-file rule
         * above just protected. */
        return parsed && data.state == END && !data.malformed && data.version != HISTORY_INVALID;
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
                                        GCancellable         *cancellable,
                                        GList               **history,
                                        gsize                *size)
{
    return _g_paste_file_backend_read_or_count (self, name, cancellable, FALSE, history, size, NULL);
}

/* Parsing is what a count costs here -- the document has to be walked (and, for
 * the encrypted flavour, decrypted) to know how many items survive the cap. What
 * it does not cost is the items themselves: no value is copied out, no image is
 * loaded from its side file, and nothing is added to a list only to be dropped
 * again. */
static gsize
g_paste_file_backend_count_history (GPasteStorageBackend *self,
                                    const gchar          *name)
{
    gsize count = 0;

    if (!_g_paste_file_backend_read_or_count (self, name, NULL /* cancellable */, TRUE, NULL, NULL, &count))
        return 0;

    return count;
}

/**
 * g_paste_file_backend_delete_files:
 * @name: the name of the history to delete
 * @encrypted: whether it is the encrypted flavour's
 * @error: return location for a #GError, or %NULL
 *
 * Delete what a file flavour stores of the history called @name: its document,
 * its images of that flavour, and what a save or a re-key left beside it. A
 * backend's delete_history, and what has no backend to delete with: no key is
 * needed to unlink a file.
 *
 * Errors come from unlinking the document, in %G_IO_ERROR.
 */
void
g_paste_file_backend_delete_files (const gchar *name,
                                   gboolean     encrypted,
                                   GError     **error)
{
    g_autoptr (GFile) history_file = g_paste_util_get_history_file (name, g_paste_storage_get_extension (encrypted ? G_PASTE_STORAGE_ENCRYPTED_FILE : G_PASTE_STORAGE_FILE));
    g_autoptr (GError) delete_error = NULL;

    /* The images go once the history is gone, and the ones this flavour may
     * share with the other go only once neither can read them: see
     * _g_paste_file_backend_sweep_images (). The base class takes the whole
     * directory once no file flavour stores the name. */
    if (!g_file_delete (history_file, NULL, &delete_error) &&
        !g_error_matches (delete_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
        g_propagate_error (error, g_steal_pointer (&delete_error));
        return;
    }

    _g_paste_file_backend_sweep_images (name, encrypted, NULL);

    if (delete_error)
        g_propagate_error (error, g_steal_pointer (&delete_error));

    /* Once the history is gone, and only then, what is left beside it goes:
     * the temporary a save cut short between its write and its move left --
     * the whole history, in clear on the plain flavour -- and what a re-key
     * left, its original under the passphrase being changed most of all. The
     * replacement says a re-key did not finish, which a history still there
     * needs said. */
    g_autofree gchar *path = g_file_get_path (history_file);
    g_autofree gchar *tmp = g_strconcat (path, ".tmp", NULL);

    if (g_unlink (tmp) && errno != ENOENT)
        g_warning ("Could not remove %s, which a cut-short save left: %s", tmp, g_strerror (errno));

#ifdef G_PASTE_ENABLE_ENCRYPTION
    _g_paste_file_backend_remove_rekey_sibling (path, G_PASTE_FILE_BACKEND_REKEY_NEW);
    _g_paste_file_backend_remove_rekey_sibling (path, G_PASTE_FILE_BACKEND_REKEY_OLD);
#endif
}

static void
g_paste_file_backend_delete_history (GPasteStorageBackend *self,
                                     const gchar          *name,
                                     GError              **error)
{
    g_paste_file_backend_delete_files (name, g_paste_storage_backend_is_encrypted (self), error);
}

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* Re-encrypt one file's bytes from the key @self holds to the one @rekeyed does,
 * into its %G_PASTE_FILE_BACKEND_REKEY_NEW sibling. The bytes are copied
 * verbatim — this is a key change, not a rewrite — and read back through the
 * new key before the caller is told it worked, so a bad write is caught while
 * the original is still the only thing in place. */
static gboolean
_g_paste_file_backend_reencrypt_file (GPasteStorageBackend *self,
                                      GPasteStorageBackend *rekeyed,
                                      const gchar          *path)
{
    g_autoptr (GFile) file = g_file_new_for_path (path);
    /* An image's, named by g_paste_file_backend_encrypted_path (). */
    gboolean side_file = g_str_has_suffix (path, ".pngs");
    /* The file's plaintext -- an image's pixels as readily as a history's
     * passwords -- so it is wiped by length rather than as a string, as is the
     * @check read back below: neither is text. */
    g_autofree gchar *text = NULL;
    gsize length = 0;
    g_autoptr (GError) error = NULL;

    if (!g_paste_file_backend_load_contents (self, path, file, side_file, &text, &length, NULL, &error))
    {
        g_warning ("Failed to read %s with the current passphrase: %s", path, error->message);
        return FALSE;
    }

    g_autofree gchar *tmp = _g_paste_file_backend_rekey_sibling (path, G_PASTE_FILE_BACKEND_REKEY_NEW);
    g_autoptr (GFile) tmp_file = g_file_new_for_path (tmp);
    g_autoptr (GOutputStream) stream = G_PASTE_FILE_BACKEND_GET_CLASS (rekeyed)->get_output_stream (G_PASTE_FILE_BACKEND (rekeyed),
                                                                                                     tmp_file);

    if (!stream)
    {
        g_paste_util_wipe (text, length);

        return FALSE;
    }

    gboolean ok = g_output_stream_write_all (stream, text, length, NULL, NULL /* cancellable */, &error) &&
                  g_output_stream_close (stream, NULL /* cancellable */, &error) &&
                  _g_paste_file_backend_sync (tmp_file, &error);

    if (ok)
    {
        g_autofree gchar *check = NULL;
        gsize check_length = 0;

        ok = g_paste_file_backend_load_contents (rekeyed, tmp, tmp_file, side_file, &check, &check_length, NULL, &error) &&
             check_length == length && !memcmp (check, text, length);

        g_paste_util_wipe (check, check_length);
    }

    g_paste_util_wipe (text, length);

    if (!ok)
    {
        g_warning ("Failed to re-encrypt %s: %s", path, (error) ? error->message : "it did not read back identical");
        /* Best effort: the failure is already reported, and a leftover temporary
         * beside the untouched original is the harmless outcome. */
        g_file_delete (tmp_file, NULL, NULL);

        return FALSE;
    }

    return TRUE;
}

/* Every file of @name that is encrypted: the history itself, first, and the
 * image side files it references, which live in the history's own images
 * directory.
 *
 * %NULL (never an incomplete list) if the directory is there but cannot be
 * listed: the caller re-keys exactly what this returns, so a short list would
 * leave the images it missed encrypted under the old passphrase, unreadable for
 * good once the new one is in force. */
static GStrv
_g_paste_file_backend_encrypted_files (const gchar *name)
{
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

    g_strv_builder_take (builder, g_paste_util_get_history_file_path (name, g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_FILE)));

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
        /* The encrypted side files only, named as this backend names them: a
         * leftover plain ".png" holds nothing secret, and a ".pngs" under any
         * other name is no entry's, the sweep leaving it alone too. */
        if (_g_paste_file_backend_is_image_file_name (*image, TRUE))
            g_strv_builder_take (builder, g_build_filename (images_dir, *image, NULL));
    }

    return g_strv_builder_end (builder);
}

/* Whether @a and @b are two names of one file. */
static gboolean
_g_paste_file_backend_same_file (const gchar *a,
                                 const gchar *b)
{
    GStatBuf a_stat;
    GStatBuf b_stat;

    return !g_stat (a, &a_stat) && !g_stat (b, &b_stat) &&
           a_stat.st_dev == b_stat.st_dev && a_stat.st_ino == b_stat.st_ino;
}

/* Settle what a re-key of @name left behind when it did not finish: the
 * process died on the way, or the filesystem refused to put the originals back.
 * The history's replacement still beside it means the re-key never reached its
 * last rename, so every original under its second name goes back over its
 * file. Without it, the re-key went through or never started, and what is left
 * beside the files is dropped. That replacement goes last, once nothing is
 * left to put back, so a settle cut short is taken up by the next one.
 *
 * Without the history itself, what is left is a history deleted -- a delete cut
 * short, which takes the history before what is beside it, or a split one
 * removed by hand -- and is dropped as well: putting its original back would
 * bring back a history the user deleted.
 *
 * Returns: whether nothing is left to put back */
static gboolean
_g_paste_file_backend_settle_rekey (const gchar *name)
{
    g_auto (GStrv) paths = _g_paste_file_backend_encrypted_files (name);

    if (!paths)
        return FALSE;

    g_autofree gchar *marker = _g_paste_file_backend_rekey_sibling (paths[0], G_PASTE_FILE_BACKEND_REKEY_NEW);
    gboolean pending = g_file_test (marker, G_FILE_TEST_EXISTS) && g_file_test (paths[0], G_FILE_TEST_EXISTS);
    gboolean settled = TRUE;

    for (GStrv path = paths; *path; ++path)
    {
        g_autofree gchar *aside_path = _g_paste_file_backend_rekey_sibling (*path, G_PASTE_FILE_BACKEND_REKEY_OLD);

        if (path != paths)
            _g_paste_file_backend_remove_rekey_sibling (*path, G_PASTE_FILE_BACKEND_REKEY_NEW);

        if (!g_file_test (aside_path, G_FILE_TEST_EXISTS))
            continue;

        /* An original that never moved is still at its path. */
        if (!pending || _g_paste_file_backend_same_file (*path, aside_path))
        {
            _g_paste_file_backend_remove_rekey_sibling (*path, G_PASTE_FILE_BACKEND_REKEY_OLD);
            continue;
        }

        g_autoptr (GFile) aside = g_file_new_for_path (aside_path);
        g_autoptr (GFile) file = g_file_new_for_path (*path);
        g_autoptr (GError) error = NULL;

        if (g_file_move (aside, file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error))
            continue;

        g_warning ("Could not put back %s after an unfinished change of passphrase, whose original is left at %s: %s",
                   *path, aside_path, error->message);
        settled = FALSE;
    }

    if (settled)
        _g_paste_file_backend_remove_rekey_sibling (paths[0], G_PASTE_FILE_BACKEND_REKEY_NEW);

    return settled;
}

/**
 * g_paste_file_backend_settle_rekeys:
 *
 * Settle every encrypted file history whose change of passphrase did not
 * finish, putting back the originals of one that had not reached its last
 * rename and dropping what is left beside one that had. Run before anything
 * reads such a history or verifies a passphrase against it: until then, a read
 * refuses it.
 */
G_PASTE_VISIBLE void
g_paste_file_backend_settle_rekeys (void)
{
    /* What is left beside a history is a dead process's only while nothing
     * else can be writing: a daemon handing over -- another process, or this
     * one's previous daemon in gnome-shell -- may still be re-keying on a
     * thread of its own, and settling under it would put back, or drop, what it
     * is in the middle of renaming. Left for the next start then, a read
     * refusing such a history until it is settled. */
    if (g_paste_storage_is_busy () || !g_paste_storage_backend_try_lock ())
    {
        g_message ("Another GPaste is still writing the histories; what a change of passphrase left is settled at the next start");
        return;
    }

    g_autoptr (GFile) dir = g_paste_util_get_history_dir ();
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) files = g_paste_util_list_directory (dir, G_FILE_ATTRIBUTE_STANDARD_NAME, &error);
    g_autofree gchar *suffix = g_strconcat (".", g_paste_storage_get_extension (G_PASTE_STORAGE_ENCRYPTED_FILE), NULL);

    if (!files)
    {
        g_warning ("Could not list the histories to settle their passphrase changes: %s", error->message);
        return;
    }

    g_autofree gchar *marker_suffix = g_strconcat (suffix, G_PASTE_FILE_BACKEND_REKEY_NEW, NULL);
    g_autoptr (GHashTable) names = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    /* By the history and by the replacement that says a re-key did not finish:
     * one left without its history, which a delete cut short or a split
     * history removed by hand leaves, has a read of that name refuse it for as
     * long as it is there. And by valid names alone, which every other file of
     * a history is named from. */
    for (GStrv file = files; *file; ++file)
    {
        const gchar *found = (g_str_has_suffix (*file, suffix)) ? suffix : (g_str_has_suffix (*file, marker_suffix)) ? marker_suffix : NULL;

        if (!found)
            continue;

        g_autofree gchar *name = g_strndup (*file, strlen (*file) - strlen (found));

        if (g_paste_util_history_name_is_valid (name))
            g_hash_table_add (names, g_steal_pointer (&name));
    }

    GHashTableIter iter;
    const gchar *name;

    g_hash_table_iter_init (&iter, names);
    while (g_hash_table_iter_next (&iter, (gpointer *) &name, NULL))
        _g_paste_file_backend_settle_rekey (name);
}

/* Where the @k-th file a re-key installs sits in the list of the history's
 * encrypted files: the images go in first and the history, at 0, last. */
static guint
_g_paste_file_backend_install_index (guint k,
                                     guint n_paths)
{
    return (k + 1) % n_paths;
}

/* Re-encrypt @name under @new_passphrase. Everything encrypted under the old key
 * — the history and its images — is rewritten beside itself, then each original
 * is given a second name, and only then is each replacement renamed over its
 * file: no byte is copied, and no file of the history is ever absent. The
 * images go in first and the history last, its rename being the one that makes
 * the re-key happen, so a failure on the way is undone by renaming the
 * originals back -- and one that cannot be is what @split reports, those
 * originals left under their second name.
 *
 * Deliberately byte-for-byte: reading the history into items and writing them
 * back would put it through the history-size limit and the images-support
 * filter, so changing a passphrase would quietly drop items the user still has.
 * A key change must change nothing but the key. */
static gboolean
g_paste_file_backend_rekey (GPasteStorageBackend *self,
                            const gchar          *name,
                            const gchar          *new_passphrase,
                            gboolean             *split)
{
    if (!g_paste_file_backend_get_passphrase (self))
    {
        g_warning ("This history is not encrypted: it has no passphrase to change");
        return FALSE;
    }

    /* Re-keying none of a history is no success either: one we could not find
     * must not come back as one that now speaks the new passphrase. */
    g_autofree gchar *history = g_paste_storage_backend_get_history_file_path (self, name);

    if (!g_file_test (history, G_FILE_TEST_EXISTS))
        return FALSE;

    /* What an earlier re-key left behind goes first: it would be taken for this
     * one's, and its originals' second names would stop this one's. Failing to
     * leaves @name split only while the history's replacement is still there
     * to say so: without it, nothing was left to put back, and it was the
     * history's images that could not be listed. */
    if (!_g_paste_file_backend_settle_rekey (name))
    {
        g_autofree gchar *marker = _g_paste_file_backend_rekey_sibling (history, G_PASTE_FILE_BACKEND_REKEY_NEW);

        *split = g_file_test (marker, G_FILE_TEST_EXISTS);
        return FALSE;
    }

    GPasteSettings *settings = g_paste_storage_backend_get_settings (self);
    g_autoptr (GPasteStorageBackend) rekeyed = g_paste_file_backend_new_encrypted (settings,
                                                                                  new_passphrase);
    g_auto (GStrv) paths = _g_paste_file_backend_encrypted_files (name);

    /* Re-keying part of a history is worse than not re-keying it at all. */
    if (!paths)
        return FALSE;

    guint n_paths = g_strv_length (paths);

    for (guint i = 0; i < n_paths; ++i)
    {
        if (_g_paste_file_backend_reencrypt_file (self, rekeyed, paths[i]))
            continue;

        /* Nothing has moved yet, so dropping the copies puts us back exactly
         * where we started. */
        for (guint j = 0; j < i; ++j)
            _g_paste_file_backend_remove_rekey_sibling (paths[j], G_PASTE_FILE_BACKEND_REKEY_NEW);

        return FALSE;
    }

    /* A hard link, which is what keeps every original at its path while the
     * renames below happen, and copies nothing. A filesystem without them --
     * vfat, exFAT, most network and FUSE mounts -- has the original copied
     * aside instead, synced as a replacement is: the same bytes under the
     * second name, which is all a settle puts back, the inode only telling it
     * whether the original is still at its path. */
    for (guint i = 0; i < n_paths; ++i)
    {
        g_autofree gchar *aside = _g_paste_file_backend_rekey_sibling (paths[i], G_PASTE_FILE_BACKEND_REKEY_OLD);

        if (!link (paths[i], aside))
            continue;

        gint link_errno = errno;
        g_autoptr (GFile) original = g_file_new_for_path (paths[i]);
        g_autoptr (GFile) copy = g_file_new_for_path (aside);
        g_autoptr (GError) error = NULL;

        if (g_file_copy (original, copy, G_FILE_COPY_NONE, NULL, NULL, NULL, &error) && _g_paste_file_backend_sync (copy, &error))
            continue;

        g_warning ("Could not keep the original of %s aside, by a hard link (%s) nor a copy (%s)", paths[i], g_strerror (link_errno), error->message);

        for (guint j = 0; j < n_paths; ++j)
        {
            if (j < i)
                _g_paste_file_backend_remove_rekey_sibling (paths[j], G_PASTE_FILE_BACKEND_REKEY_OLD);
            _g_paste_file_backend_remove_rekey_sibling (paths[j], G_PASTE_FILE_BACKEND_REKEY_NEW);
        }

        return FALSE;
    }

    guint installed = 0;

    for (; installed < n_paths; ++installed)
    {
        const gchar *path = paths[_g_paste_file_backend_install_index (installed, n_paths)];
        g_autofree gchar *tmp = _g_paste_file_backend_rekey_sibling (path, G_PASTE_FILE_BACKEND_REKEY_NEW);
        g_autoptr (GFile) tmp_file = g_file_new_for_path (tmp);
        g_autoptr (GFile) file = g_file_new_for_path (path);
        g_autoptr (GError) error = NULL;

        /* Each of these is a rename over a file whose replacement is already
         * written and verified, so the only way to fail here is the filesystem
         * itself giving out. */
        if (!g_file_move (tmp_file, file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error))
        {
            g_warning ("Failed to install the re-encrypted %s: %s", path, error->message);
            break;
        }
    }

    if (installed == n_paths)
    {
        /* The renames are what make the re-key happen, and a rename is on the
         * disk once its directory is: synced before the change is answered as
         * done -- the new passphrase then adopted, and remembered -- or a power
         * loss would bring the histories back under the one just retired. */
        g_autofree gchar *history_dir = g_path_get_dirname (paths[0]);
        g_autofree gchar *images_dir = g_paste_file_backend_images_dir (name);

        _g_paste_file_backend_sync_directory (history_dir);
        if (n_paths > 1)
            _g_paste_file_backend_sync_directory (images_dir);

        for (guint i = 0; i < n_paths; ++i)
            _g_paste_file_backend_remove_rekey_sibling (paths[i], G_PASTE_FILE_BACKEND_REKEY_OLD);

        return TRUE;
    }

    /* Put back what moved, the original under its second name being the only
     * copy of it left. Those that never moved keep theirs at their path, and
     * lose only the second name. The history never moved, its rename being the
     * last; its replacement stays until everything else is back, which is what
     * tells _g_paste_file_backend_settle_rekey() there is something to put back
     * when the filesystem refuses here. */
    for (guint k = 0; k < n_paths; ++k)
    {
        const gchar *path = paths[_g_paste_file_backend_install_index (k, n_paths)];

        if (path != paths[0])
            _g_paste_file_backend_remove_rekey_sibling (path, G_PASTE_FILE_BACKEND_REKEY_NEW);

        if (k >= installed)
        {
            _g_paste_file_backend_remove_rekey_sibling (path, G_PASTE_FILE_BACKEND_REKEY_OLD);
            continue;
        }

        g_autofree gchar *aside_path = _g_paste_file_backend_rekey_sibling (path, G_PASTE_FILE_BACKEND_REKEY_OLD);
        g_autoptr (GFile) aside = g_file_new_for_path (aside_path);
        g_autoptr (GFile) file = g_file_new_for_path (path);
        g_autoptr (GError) error = NULL;

        if (!g_file_move (aside, file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error))
        {
            g_warning ("Could not put back %s, whose original is left at %s: %s", path, aside_path, error->message);
            *split = TRUE;
        }
    }

    if (!*split)
        _g_paste_file_backend_remove_rekey_sibling (paths[0], G_PASTE_FILE_BACKEND_REKEY_NEW);

    return FALSE;
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
        g_autoptr (GConverter) converter = g_paste_secret_stream_converter_new (G_PASTE_SECRET_STREAM_ENCRYPT,
                                                                                g_paste_passphrase_peek (priv->passphrase));
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
g_paste_file_backend_dispose (GObject *object)
{
    GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (G_PASTE_FILE_BACKEND (object));

    g_clear_pointer (&priv->png_checksums, g_hash_table_unref);

    G_OBJECT_CLASS (g_paste_file_backend_parent_class)->dispose (object);
}

static void
g_paste_file_backend_finalize (GObject *object)
{
    GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (G_PASTE_FILE_BACKEND (object));

    g_paste_passphrase_free (priv->passphrase);
    g_mutex_clear (&priv->png_checksums_lock);

    G_OBJECT_CLASS (g_paste_file_backend_parent_class)->finalize (object);
}
#endif

#ifdef G_PASTE_ENABLE_ENCRYPTION
/* A history whose first chunk the key does not authenticate is the one case
 * that proves the passphrase wrong, the converter answering it with
 * %G_IO_ERROR_PERMISSION_DENIED. Anything else -- a file that will not open, a
 * foreign file, damage past the first chunk, an empty placeholder -- leaves the
 * passphrase as likely right as before. */
static gboolean
g_paste_file_backend_history_refutes_passphrase (GPasteStorageBackend *self,
                                                 const gchar          *name)
{
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (self, name);
    g_autoptr (GFile) file = g_file_new_for_path (path);
    g_autoptr (GPasteCleartext) text = NULL;
    gsize text_length;
    gboolean opened;
    g_autoptr (GError) error = NULL;

    if (g_paste_file_backend_load_contents (self, path, file, FALSE, &text, &text_length, &opened, &error))
        return FALSE;

    return opened && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
}

/* A history whose document the key decrypts whole holds what it encrypted,
 * bar the empty placeholder a read leaves for one, which says nothing. The
 * document alone: its images, each a stream of its own salted apart, cost a
 * derivation each, which the full read the verification would otherwise fall
 * back on pays on the thread it runs on -- the compositor's, when the unlock
 * happens inside GNOME Shell (/storage/passphrase/verify-cost). */
static gboolean
g_paste_file_backend_history_confirms_passphrase (GPasteStorageBackend *self,
                                                  const gchar          *name)
{
    g_autofree gchar *path = g_paste_storage_backend_get_history_file_path (self, name);
    g_autoptr (GFile) file = g_file_new_for_path (path);
    g_autoptr (GPasteCleartext) text = NULL;
    gsize text_length;
    gboolean opened;
    GStatBuf stat_buf;

    if (g_stat (path, &stat_buf) || !stat_buf.st_size)
        return FALSE;

    return g_paste_file_backend_load_contents (self, path, file, FALSE, &text, &text_length, &opened, NULL);
}
#endif

static void
g_paste_file_backend_class_init (GPasteFileBackendClass *klass)
{
    GPasteStorageBackendClass *storage_class = G_PASTE_STORAGE_BACKEND_CLASS (klass);

    storage_class->read_history_file = g_paste_file_backend_read_history_file;
    storage_class->write_history_file = g_paste_file_backend_write_history_file;
    storage_class->get_kind = g_paste_file_backend_get_kind;
    storage_class->delete_history = g_paste_file_backend_delete_history;
    storage_class->count_history = g_paste_file_backend_count_history;

    klass->get_output_stream = g_paste_file_backend_get_output_stream;

#ifdef G_PASTE_ENABLE_ENCRYPTION
    storage_class->rekey = g_paste_file_backend_rekey;
    storage_class->history_refutes_passphrase = g_paste_file_backend_history_refutes_passphrase;
    storage_class->history_confirms_passphrase = g_paste_file_backend_history_confirms_passphrase;

    G_OBJECT_CLASS (klass)->dispose = g_paste_file_backend_dispose;
    G_OBJECT_CLASS (klass)->finalize = g_paste_file_backend_finalize;
#endif
}

static void
g_paste_file_backend_init (GPasteFileBackend *self G_GNUC_UNUSED)
{
#ifdef G_PASTE_ENABLE_ENCRYPTION
    GPasteFileBackendPrivate *priv = g_paste_file_backend_get_instance_private (self);

    g_mutex_init (&priv->png_checksums_lock);
    priv->png_checksums = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
#endif
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

    priv->passphrase = g_paste_passphrase_new (passphrase);

    return self;
}

#endif
