// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-daemon/gpaste-daemon-methods.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-history-private.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-storage-backend.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <glib/gstdio.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A history is stored in a file its name goes straight into, and in an images
 * directory of the same name, so a name that is a path names something outside
 * the history directory: a backup writes its file there, and a delete sweeps
 * every file of the directory it landed in. Checked here, at the bus, where a
 * caller still gets an error instead of the path builders' critical. */
#define G_PASTE_DBUS_ASSERT_HISTORY_NAME_FULL(name, ret)                          \
    G_PASTE_DBUS_ASSERT_FULL (g_paste_util_history_name_is_valid (name),          \
                              G_PASTE_ERROR_INVALID_ARGUMENT,                     \
                              "not a history name: a path, or too long a name",   \
                              ret)

#define G_PASTE_DBUS_ASSERT_HISTORY_NAME(name) G_PASTE_DBUS_ASSERT_HISTORY_NAME_FULL (name, ;)

/* The one shape an item takes on the wire, and the one value it carries: the
 * string a user is shown. */
static GVariant *
g_paste_daemon_methods_item_variant (GPasteItem *item)
{
    /* The reserved notes slot, empty because nothing attaches notes to an item
     * yet: see G_PASTE_ITEM_VARIANT_STRING for why it travels all the same. */
    static const gchar * const no_notes[] = { NULL };

    return g_variant_new (G_PASTE_ITEM_VARIANT_FORMAT,
                          g_paste_item_get_uuid (item),
                          g_paste_item_get_display_string (item),
                          (guint32) g_paste_item_get_kind (item),
                          g_paste_item_is_favourite (item),
                          no_notes);
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

/* A change made while the store is handed over -- the prompts of a change of
 * passphrase, a check of the store or an in-place migration up, the bus still
 * answered -- is recorded nowhere, and the reload ending the handover undoes
 * it: an item deleted comes back, one moved goes back where it was. Refused
 * with %G_IO_ERROR_BUSY, as the methods reaching the stores themselves are
 * (g_paste_history_sync ()). An add is not: the reload keeps what was added
 * meanwhile, as it keeps a copy (@stopped_adds in gpaste-history.c), so
 * do_add_item () asks nothing of this. */
static gboolean
g_paste_daemon_methods_check_owned (const GPasteDaemonMethods *self,
                                    GError                   **error)
{
    const gchar *current = g_paste_history_get_current (self->history);

    /* A history nobody has named yet has no store to have handed over. */
    return !current || g_paste_history_sync (self->history, current, error);
}

/* Whether a password called @name exists is asked of every password the
 * history holds, which a load out has yet to bring back: looked up then, a name
 * would be checked against what was added during the load alone, and the load
 * would put a stored namesake beside the new one -- two passwords DeletePassword
 * cannot tell apart. Refused with %G_IO_ERROR_BUSY until the load lands, as
 * BackupHistory of a loading history is. The nameless placeholder collides with
 * nothing; a %NULL @name is a lookup by name, which no name lets through. */
static gboolean
g_paste_daemon_methods_check_names_loaded (const GPasteDaemonMethods *self,
                                           const gchar               *name,
                                           GError                   **error)
{
    if ((name && g_paste_str_equal (name, G_PASTE_PASSWORD_ITEM_NO_NAME)) || !g_paste_history_is_loading (self->history))
        return TRUE;

    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_BUSY, "The history is still loading the passwords it holds");
    return FALSE;
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
        /* Handed over all the same rather than simply dropped: what @item
         * carries beyond the value it matched on -- a password's timeout, two
         * nameless ones matching on their value alone -- belongs on the entry
         * that is kept, and g_paste_history_add () is what moves it there (see
         * g_paste_history_private_merge_into_head ()). It keeps @current, so the
         * uuid answered below is still the one the history holds the content
         * under; skipping this would answer the same AddPassword two ways
         * depending on whether it arrived here or off a selection. */
        g_paste_history_add (self->history, g_object_ref (item));

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

/* Where an uploaded address goes for the user to paste it: the history, which
 * puts it on the clipboard, or the clipboard alone when the history refuses it
 * -- UploadAndCopy's doc in the D-Bus XML says why, and why in the daemon. A
 * message rather than a warning, the refusal being an outcome this handles,
 * and one without the address: a paste's can carry the key to it (PrivateBin
 * puts it in the fragment), which the journal has no business keeping, and the
 * daemon never quotes what the upload command wrote (g_paste_daemon_upload ()).
 * /clipboard/copy_uploaded holds it to both. */
G_PASTE_VISIBLE void
g_paste_daemon_methods_copy_uploaded (const GPasteDaemonMethods *self,
                                      const gchar               *url)
{
    g_autoptr (GError) error = NULL;
    g_autofree gchar *uuid = g_paste_daemon_methods_do_add (self, url, strlen (url), &error);

    if (uuid)
        return;

    g_message ("The history did not keep the uploaded address: %s", error->message);
    g_paste_clipboards_manager_select_text (self->clipboards_manager, url);
}

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_add_text (const GPasteDaemonMethods *self,
                                 const gchar               *text,
                                 GError                   **error)
{
    return g_paste_daemon_methods_do_add (self, text, (text) ? strlen (text) : 0, error);
}

/* How many first bytes of a file AddFile judges whether it begins as a text
 * by: an image's do not, PNG, JPEG and TIFF opening with a byte or a NUL that
 * UTF-8 has no place for, and binary data meets one well within this many. */
#define G_PASTE_DAEMON_METHODS_HEAD_SIZE 4096

/* Whether @head could begin a UTF-8 text, a sequence cut short by the end of a
 * head read in full included. */
static gboolean
g_paste_daemon_methods_starts_as_text (const gchar *head,
                                       gsize        length)
{
    const gchar *end;

    if (g_utf8_validate (head, (gssize) length, &end))
        return TRUE;

    /* g_utf8_get_char_validated () answers a sequence holding a NUL as it does
     * one cut short: a TIFF's "II*\0" is no text begun. */
    gsize rest = (gsize) (head + length - end);

    return length == G_PASTE_DAEMON_METHODS_HEAD_SIZE && !memchr (end, '\0', rest) && g_utf8_get_char_validated (end, (gssize) rest) == (gunichar) -2;
}

/* How many bytes of a file AddFile holds at most: "max-memory-usage", short of
 * what a gsize can count with the NUL after them and the one byte more that
 * tells a file at the cap from one past it -- on a 32-bit build, 4096 MiB or
 * more would wrap. */
static gsize
g_paste_daemon_methods_file_cap (const GPasteDaemonMethods *self)
{
    guint64 cap = g_paste_settings_get_max_memory_usage (self->settings) * 1024 * 1024;

    return (gsize) MIN (cap, (guint64) G_MAXSIZE - 2);
}

/* Read whole and on the main loop, so only what is sure to end, at a size the
 * history can keep: a FIFO or a terminal would hold the daemon until something
 * wrote to it, /dev/zero would never stop, and a file over the memory cap would
 * only be refused once read. */
static gboolean
g_paste_daemon_methods_can_read (const GPasteDaemonMethods *self,
                                 gboolean                   regular,
                                 guint64                    size,
                                 GError                   **error)
{
    G_PASTE_DBUS_ASSERT_FULL (regular, G_PASTE_ERROR_INVALID_ARGUMENT, "the path names no regular file", FALSE);
    G_PASTE_DBUS_ASSERT_FULL (size <= g_paste_daemon_methods_file_cap (self),
                              G_PASTE_ERROR_REJECTED, "the file is larger than max-memory-usage", FALSE);

    return TRUE;
}

/* One read () of @fd, into @buffer for at most @length bytes, again where a
 * signal cut it short: how many came, 0 at its end, -1 with @error set -- in
 * %G_FILE_ERROR, as the open () before it reports. Not through a
 * #GUnixInputStream: g_input_stream_read_all () runs the same loop, but hands
 * its failures over in %G_IO_ERROR, and AddFile would then answer for one
 * file in two domains. */
static gssize
g_paste_daemon_methods_read_some (gint     fd,
                                  gchar   *buffer,
                                  gsize    length,
                                  GError **error)
{
    gssize got;

    while ((got = read (fd, buffer, length)) < 0 && errno == EINTR)
        ;

    if (got < 0)
    {
        gint saved_errno = errno;

        g_set_error_literal (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno), g_strerror (saved_errno));
    }

    return got;
}

/* Read @fd on from @length, until its end or until @limit bytes are in, into
 * @buffer, of @capacity bytes -- one always left for the NUL after them. A
 * buffer full is grown only once a byte read past it says the file goes on:
 * sized for what the file reports, which is all of a regular one, it then
 * stays that size. Grown with g_try_realloc (): an allocation refused is an
 * error here, %G_FILE_ERROR_NOMEM as g_file_get_contents () has it, rather
 * than the abort that would take the process hosting the daemon with it --
 * gnome-shell for the in-shell one. */
static gboolean
g_paste_daemon_methods_read (gint     fd,
                             gchar  **buffer,
                             gsize   *capacity,
                             gsize   *length,
                             gsize    limit,
                             GError **error)
{
    while (*length < limit)
    {
        gssize got;

        if (*length < *capacity - 1)
        {
            got = g_paste_daemon_methods_read_some (fd, *buffer + *length, MIN (*capacity - 1, limit) - *length, error);
            if (got <= 0)
                return got == 0;
            *length += (gsize) got;
            continue;
        }

        gchar next;

        got = g_paste_daemon_methods_read_some (fd, &next, 1, error);
        if (got <= 0)
            return got == 0;

        gsize grown = (*capacity > (limit + 1) / 2) ? limit + 1 : MAX (*capacity * 2, G_PASTE_DAEMON_METHODS_HEAD_SIZE + 1);
        gchar *larger = g_try_realloc (*buffer, grown);

        if (!larger)
        {
            g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "could not allocate %" G_GSIZE_FORMAT " bytes for the file", grown);
            return FALSE;
        }

        *buffer = larger;
        *capacity = grown;
        (*buffer)[(*length)++] = next;
    }

    return TRUE;
}

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_add_file (const GPasteDaemonMethods *self,
                                 const gchar               *path,
                                 GError                   **error)
{
    G_PASTE_DBUS_ASSERT_FULL (path && *path, G_PASTE_ERROR_INVALID_ARGUMENT, "no file to add", NULL);

    g_autoptr (GFile) file = g_file_new_for_path (path);
    g_autoptr (GFileInfo) info = g_file_query_info (file,
                                                    G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
                                                    G_FILE_QUERY_INFO_NONE,
                                                    NULL /* cancellable */,
                                                    error);

    if (!info)
        return NULL;

    /* GLib's local backend answers a path it could not stat -- one in a folder
     * the daemon cannot enter -- with an info holding none of what was asked,
     * rather than failing: the stat says why (/history/add_file_unsearchable). */
    if (!g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_TYPE) ||
        !g_file_info_has_attribute (info, G_FILE_ATTRIBUTE_STANDARD_SIZE))
    {
        GStatBuf unread;
        gint saved_errno = (g_stat (path, &unread) < 0) ? errno : EACCES;

        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno), "%s: %s", path, g_strerror (saved_errno));
        return NULL;
    }

    /* Looked at by its path, so that nothing else is so much as opened, then
     * again once opened, the path being free to name something else by then:
     * everything read is the descriptor's, no further than the memory cap,
     * and the open waits on no FIFO (/history/add_file_swapped). */
    if (!g_paste_daemon_methods_can_read (self, g_file_info_get_file_type (info) == G_FILE_TYPE_REGULAR, (guint64) g_file_info_get_size (info), error))
        return NULL;

    g_autofd gint fd = g_open (path, O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK, 0);
    struct stat st;

    if (fd < 0 || fstat (fd, &st) < 0)
    {
        gint saved_errno = errno;

        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (saved_errno), "%s: %s", path, g_strerror (saved_errno));
        return NULL;
    }

    if (!g_paste_daemon_methods_can_read (self, S_ISREG (st.st_mode), (guint64) st.st_size, error))
        return NULL;

    /* Read to its end rather than to the size it was checked at -- procfs
     * reports a regular file of size 0 for /proc/cpuinfo, whatever it holds
     * (/history/add_file_unsized) -- but no further than the memory cap, which
     * a file growing meanwhile is refused at as one past it already was. Into
     * a buffer of the size reported first, the one checked against that cap
     * and so a gsize. A regular file's reads take no notice of the
     * non-blocking flag. */
    gsize size = (gsize) st.st_size;
    gsize capacity = size + 1;
    g_autofree gchar *content = g_try_malloc (capacity);
    gsize length = 0;

    if (!content)
    {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_NOMEM, "could not allocate %" G_GSIZE_FORMAT " bytes for the file", capacity);
        return NULL;
    }

    if (!g_paste_daemon_methods_read (fd, &content, &capacity, &length, G_PASTE_DAEMON_METHODS_HEAD_SIZE, error))
        return NULL;

    /* The text cap, tighter, holds for a text alone, and judges the item stored
     * (g_paste_daemon_methods_do_add ()): with trim-items off that is the whole
     * file, so one past it is refused on its first bytes if it begins as a
     * text, and handed straight to the image loader if not. With trimming on,
     * whatever whitespace is taken off is unbounded, so the file is read to
     * know. */
    gboolean judged = !g_paste_settings_get_trim_items (self->settings) && size > g_paste_settings_get_max_text_item_size (self->settings);

    G_PASTE_DBUS_ASSERT_FULL (!judged || !g_paste_daemon_methods_starts_as_text (content, length),
                              G_PASTE_ERROR_REJECTED, "the content is longer than max-text-item-size", NULL);

    gsize cap = g_paste_daemon_methods_file_cap (self);

    if (!g_paste_daemon_methods_read (fd, &content, &capacity, &length, cap + 1, error))
        return NULL;

    G_PASTE_DBUS_ASSERT_FULL (length <= cap, G_PASTE_ERROR_REJECTED, "the file is larger than max-memory-usage", NULL);
    content[length] = '\0';

    if (!judged && g_utf8_validate (content, (gssize) length, NULL))
        return g_paste_daemon_methods_do_add (self, content, length, error);

    /* A history with images-support off keeps no image: both storage flavours
     * drop one on their next read, the file backend's next write sweeping its
     * file, so one added would be answered and listed only to be gone at the
     * next start. */
    G_PASTE_DBUS_ASSERT_FULL (g_paste_settings_get_images_support (self->settings),
                              G_PASTE_ERROR_REJECTED, "images-support is off, so the history keeps no image", NULL);

    g_autoptr (GBytes) bytes = g_bytes_new_take (g_steal_pointer (&content), length);

    g_autoptr (GError) img_error = NULL;
    g_autoptr (GdkTexture) img = gdk_texture_new_from_bytes (bytes, &img_error);

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
                                     guint                      timeout,
                                     GError                   **error)
{
    G_PASTE_DBUS_ASSERT_FULL (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no name for the password to add", NULL);
    G_PASTE_DBUS_ASSERT_FULL (password && *password, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to add", NULL);

    if (!g_paste_daemon_methods_check_names_loaded (self, name, error))
        return NULL;

    /* As in g_paste_daemon_methods_make_password(): the nameless placeholder
     * is shared by passwords that have no user-chosen name. */
    GPastePasswordItem *namesake = (g_paste_str_equal (name, G_PASTE_PASSWORD_ITEM_NO_NAME)) ? NULL : g_paste_history_get_password (self->history, name);

    G_PASTE_DBUS_ASSERT_FULL (!namesake,
                              G_PASTE_ERROR_ALREADY_EXISTS,
                              "another password with that name already exists",
                              NULL);

    /* As in g_paste_history_make_password (): nothing to write is the default
     * a password being made takes. */
    if (timeout == G_PASTE_PASSWORD_TIMEOUT_UNCHANGED)
        timeout = (guint) g_paste_settings_get_password_timeout (self->settings);

    return g_paste_daemon_methods_do_add_item (self, g_paste_password_item_new (name, password, timeout), error);
}

/* Whether no history called @name is in the store yet, for the callers that
 * only want to know whether to announce a new one. Asked of the backend rather
 * than tracked, since a history exists exactly as long as its entry in the
 * store does. A store we could not read is no reason to refuse the operation,
 * and announcing one history too many costs a listener a re-list. */
static gboolean
g_paste_daemon_methods_history_is_new (const GPasteDaemonMethods *self,
                                       const gchar               *name)
{
    g_autoptr (GError) error = NULL;
    g_auto (GStrv) histories = g_paste_history_list (self->history, &error);

    if (!histories)
    {
        g_warning ("Could not list the histories: %s", error->message);
        return TRUE;
    }

    return !g_strv_contains ((const gchar * const *) histories, name);
}

/* Whether the storage in use could keep a history created under @name: see
 * g_paste_storage_name_fits (). */
static gboolean
g_paste_daemon_methods_name_fits (const GPasteDaemonMethods *self,
                                  const gchar               *name)
{
    return g_paste_storage_name_fits (g_paste_settings_get_storage_backend (self->settings), name);
}

/* Whether BackupHistory may copy @history as @backup, @error set if not. */
static gboolean
g_paste_daemon_methods_backup_allowed (const GPasteDaemonMethods *self,
                                       const gchar               *history,
                                       const gchar               *backup,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT_FULL (history && *history, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to backup", FALSE);
    G_PASTE_DBUS_ASSERT_FULL (backup && *backup, G_PASTE_ERROR_INVALID_ARGUMENT, "no name to back the history up under", FALSE);
    G_PASTE_DBUS_ASSERT_HISTORY_NAME_FULL (history, FALSE);
    G_PASTE_DBUS_ASSERT_HISTORY_NAME_FULL (backup, FALSE);

    gboolean current = g_paste_str_equal (history, g_paste_history_get_current (self->history));

    /* Refused rather than overwritten: a backup quietly destroying an older one
     * of the same name is a loss noticed far too late to undo. A store we cannot
     * enumerate is refused too -- not knowing is not the same as knowing there
     * is none. The one listing answers for the source below as well. A backup
     * to that name still on its way to the store is refused by
     * g_paste_history_copy_async (). */
    g_auto (GStrv) histories = g_paste_history_list (self->history, error);

    if (!histories)
        return FALSE;

    /* The history in use counts as one whether or not its store holds it yet,
     * as ListHistories names it: written under its name, the backup would be
     * erased by the next write of its model. */
    gboolean exists = g_strv_contains ((const gchar * const *) histories, backup) ||
                      g_paste_str_equal (backup, g_paste_history_get_current (self->history));

    G_PASTE_DBUS_ASSERT_FULL (!exists, G_PASTE_ERROR_ALREADY_EXISTS, "a history already goes by that name", FALSE);
    G_PASTE_DBUS_ASSERT_FULL (g_paste_daemon_methods_name_fits (self, backup),
                              G_PASTE_ERROR_INVALID_ARGUMENT, "too long a name for a history of this storage", FALSE);

    /* Nor is a history no store holds one to back up: reading it would create
     * an empty one under that name, and the backup of it would answer OK. */
    G_PASTE_DBUS_ASSERT_FULL (current || g_strv_contains ((const gchar * const *) histories, history),
                              G_PASTE_ERROR_NOT_FOUND, "no history goes by that name", FALSE);

    return TRUE;
}

/* A history being backed up (g_paste_history_copy_async ()), the bus told once
 * it is done. */
typedef struct
{
    GPasteDaemon3 *skeleton;
    GPasteHistory *history;
} BackupHistoryData;

static void
backup_history_data_free (BackupHistoryData *self)
{
    g_clear_object (&self->skeleton);
    g_clear_object (&self->history);
    g_free (self);
}

static void
g_paste_daemon_methods_on_backup_copied (GObject      *source_object G_GNUC_UNUSED,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
    g_autoptr (GTask) task = user_data;
    BackupHistoryData *data = g_task_get_task_data (task);
    GError *error = NULL;

    if (!g_paste_history_copy_finish (data->history, result, &error))
    {
        g_task_return_error (task, error);
        return;
    }

    /* What changed is the set of histories, which has a signal of its own
     * precisely because a new one appearing is not a switch. A copy that failed
     * left no history called @backup, so nothing to announce. */
    g_paste_daemon3_emit_raw_histories_changed (data->skeleton);
    g_task_return_boolean (task, TRUE);
}

/**
 * g_paste_daemon_methods_backup_history_async:
 * @self: the daemon's methods
 * @history: the history to back up
 * @backup: the name to back it up under
 * @callback: called once the backup is written, or refused
 * @user_data: data for @callback
 *
 * BackupHistory: copy @history under @backup, in the background
 * (g_paste_history_copy_async ()), the current history not switched away from
 * and no UI told it was. The D-Bus XML says what it refuses.
 */
G_PASTE_VISIBLE void
g_paste_daemon_methods_backup_history_async (const GPasteDaemonMethods *self,
                                             const gchar               *history,
                                             const gchar               *backup,
                                             GAsyncReadyCallback        callback,
                                             gpointer                   user_data)
{
    g_autoptr (GTask) task = g_task_new (NULL, NULL, callback, user_data);
    GError *error = NULL;

    BackupHistoryData *data = g_new0 (BackupHistoryData, 1);

    data->skeleton = g_object_ref (self->skeleton);
    data->history = g_object_ref (self->history);
    g_task_set_source_tag (task, g_paste_daemon_methods_backup_history_async);
    g_task_set_task_data (task, data, (GDestroyNotify) backup_history_data_free);

    if (!g_paste_daemon_methods_backup_allowed (self, history, backup, &error))
    {
        g_task_return_error (task, error);
        return;
    }

    g_paste_history_copy_async (self->history, history, backup, g_paste_daemon_methods_on_backup_copied, g_steal_pointer (&task));
}

/**
 * g_paste_daemon_methods_backup_history_finish:
 * @result: the #GAsyncResult g_paste_daemon_methods_backup_history_async ()
 *          handed its callback
 * @error: return location for a #GError, or %NULL
 *
 * Fails with the error BackupHistory answers: %G_PASTE_ERROR for what it
 * refuses or fails to write, %G_IO_ERROR_BUSY while the store is handed over or
 * the history still loading, or the store's own when it cannot be listed.
 *
 * Returns: whether the backup was written
 */
G_PASTE_VISIBLE gboolean
g_paste_daemon_methods_backup_history_finish (GAsyncResult *result,
                                              GError      **error)
{
    g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_item (const GPasteDaemonMethods *self,
                                    const gchar               *uuid,
                                    GError                   **error)
{
    if (!g_paste_daemon_methods_check_owned (self, error))
        return;

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

    if (!g_paste_daemon_methods_check_owned (self, error))
        return;

    /* A history no store holds is not found, as a uuid matching nothing is,
     * rather than whatever unlinking a file that is not there reports. The
     * current history is deleted whether or not anything of it was saved yet,
     * and one a store out of the backend's reach keeps, which no listing shows,
     * is g_paste_history_delete ()'s to refuse as such. */
    if (!g_paste_str_equal (name, g_paste_history_get_current (history)))
    {
        g_auto (GStrv) histories = g_paste_history_list (history, error);

        if (!histories)
            return;

        gboolean unreachable = !g_paste_history_reaches_store (history, name);

        G_PASTE_DBUS_ASSERT (unreachable || g_strv_contains ((const gchar * const *) histories, name), G_PASTE_ERROR_NOT_FOUND, "no history goes by that name");
    }

    /* Nothing was deleted if this fails, the current history left only for
     * the time it took (g_paste_history_delete ()), so the deletion is not
     * announced; the caller turns @error into the method's reply. */
    if (!g_paste_history_delete (history, name, error))
        return;

    g_paste_daemon3_emit_raw_history_deleted (self->skeleton, name);
    /* The set of histories is one shorter: anything merely listing them has no
     * use for the name, and would otherwise have to know about this signal. */
    g_paste_daemon3_emit_raw_histories_changed (self->skeleton);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_delete_password (const GPasteDaemonMethods *self,
                                        const gchar               *name,
                                        GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password to delete");

    if (!g_paste_daemon_methods_check_owned (self, error) || !g_paste_daemon_methods_check_names_loaded (self, NULL, error))
        return;

    G_PASTE_DBUS_ASSERT (g_paste_history_delete_password (self->history, name),
                         G_PASTE_ERROR_NOT_FOUND, "no password goes by that name");
}

/* What a history emptied tells the bus. */
static void
g_paste_daemon_methods_history_emptied (GPasteDaemon3 *skeleton,
                                        const gchar   *name,
                                        gboolean       is_new)
{
    if (is_new)
        g_paste_daemon3_emit_raw_histories_changed (skeleton);

    g_paste_daemon3_emit_raw_history_emptied (skeleton, name);
}

/* Another history emptied (g_paste_history_empty_other_async ()), the bus told
 * once it is done. */
typedef struct
{
    GPasteDaemon3 *skeleton;
    GPasteHistory *history;
    gchar         *name;
    gboolean       is_new;
} EmptyHistoryData;

static void
empty_history_data_free (EmptyHistoryData *self)
{
    g_clear_object (&self->skeleton);
    g_clear_object (&self->history);
    g_free (self->name);
    g_free (self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EmptyHistoryData, empty_history_data_free)

/* EmptyHistory up to the write of another history: what it checks, and the
 * current history emptied there and then -- through its saver, which writes it
 * on its own worker, as it writes another one. @data is set when there is
 * another history to write. */
static void
g_paste_daemon_methods_empty_history_start (const GPasteDaemonMethods *self,
                                            const gchar               *name,
                                            EmptyHistoryData         **data,
                                            GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to empty");
    G_PASTE_DBUS_ASSERT_HISTORY_NAME (name);

    /* Another history is written over below: what is still out for it has to
     * land first, or land afterwards and bring back what was emptied. */
    if (!g_paste_history_sync (self->history, name, error))
        return;

    gboolean current = g_paste_str_equal (name, g_paste_history_get_current (self->history));

    /* Emptying a history there is none of writes one, empty: that is the set of
     * histories growing, which anything listing them has to hear about. Asked
     * before the write, since afterwards it always exists -- and the current
     * history is no exception, since emptying it saves, which brings its file
     * into existence just the same. */
    gboolean is_new = g_paste_daemon_methods_history_is_new (self, name);

    G_PASTE_DBUS_ASSERT (!is_new || g_paste_daemon_methods_name_fits (self, name),
                         G_PASTE_ERROR_INVALID_ARGUMENT, "too long a name for a history of this storage");

    if (current)
    {
        if (g_paste_history_empty (self->history, error))
            g_paste_daemon_methods_history_emptied (self->skeleton, name, is_new);
        return;
    }

    *data = g_new0 (EmptyHistoryData, 1);
    (*data)->skeleton = g_object_ref (self->skeleton);
    (*data)->history = g_object_ref (self->history);
    (*data)->name = g_strdup (name);
    (*data)->is_new = is_new;
}

static void
on_history_emptied (GObject      *source_object G_GNUC_UNUSED,
                    GAsyncResult *result,
                    gpointer      user_data)
{
    g_autoptr (GTask) task = user_data;
    EmptyHistoryData *data = g_task_get_task_data (task);
    GError *error = NULL;

    if (!g_paste_history_empty_other_finish (data->history, result, &error))
    {
        g_task_return_error (task, error);
        return;
    }

    g_paste_daemon_methods_history_emptied (data->skeleton, data->name, data->is_new);
    g_task_return_boolean (task, TRUE);
}

/**
 * g_paste_daemon_methods_empty_history_async:
 * @self: the daemon's methods
 * @name: the history to empty
 * @callback: (scope async): called once the emptying is done, or has failed
 * @user_data: data passed to @callback
 *
 * EmptyHistory: the current history emptied in place, another one written over
 * with an empty history in the saver's turn
 * (g_paste_history_empty_other_async ()).
 */
G_PASTE_VISIBLE void
g_paste_daemon_methods_empty_history_async (const GPasteDaemonMethods *self,
                                            const gchar               *name,
                                            GAsyncReadyCallback        callback,
                                            gpointer                   user_data)
{
    g_autoptr (GTask) task = g_task_new (NULL, NULL /* cancellable */, callback, user_data);
    g_autoptr (EmptyHistoryData) data = NULL;
    GError *error = NULL;

    g_task_set_source_tag (task, g_paste_daemon_methods_empty_history_async);
    g_paste_daemon_methods_empty_history_start (self, name, &data, &error);

    if (error)
        g_task_return_error (task, error);
    else if (!data)
        g_task_return_boolean (task, TRUE);
    else
    {
        GPasteHistory *history = data->history;
        const gchar *emptied = data->name;

        g_task_set_task_data (task, g_steal_pointer (&data), (GDestroyNotify) empty_history_data_free);
        g_paste_history_empty_other_async (history, emptied, on_history_emptied, g_steal_pointer (&task));
    }
}

/**
 * g_paste_daemon_methods_empty_history_finish:
 * @result: the #GAsyncResult handed to the callback
 * @error: return location for a #GError, or %NULL
 *
 * Errors are those of EmptyHistory over the bus.
 *
 * Returns: whether the history was emptied
 */
G_PASTE_VISIBLE gboolean
g_paste_daemon_methods_empty_history_finish (GAsyncResult *result,
                                             GError      **error)
{
    g_return_val_if_fail (g_task_is_valid (result, NULL), FALSE);
    g_return_val_if_fail (g_task_get_source_tag (G_TASK (result)) == g_paste_daemon_methods_empty_history_async, FALSE);

    return g_task_propagate_boolean (G_TASK (result), error);
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

/* Asked for rather than carried by every item: see
 * g_paste_client_get_password_timeout_sync (). */
G_PASTE_VISIBLE guint
g_paste_daemon_methods_get_password_timeout (const GPasteDaemonMethods *self,
                                             const gchar               *uuid,
                                             GError                   **error)
{
    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", 0);
    G_PASTE_DBUS_ASSERT_FULL (G_PASTE_IS_PASSWORD_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "Provided uuid doesn't match a password item.", 0);

    return g_paste_password_item_get_timeout (G_PASTE_PASSWORD_ITEM (item));
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

/* Daemon-side because it has to be: what gets joined is each item's whole
 * value, and the only string an item travels over the bus with is the one a
 * user is shown. An image's value is the checksum naming its file and a
 * password's the placeholder standing for it, neither of which is anything to
 * join, so both are refused before anything is. */
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
        G_PASTE_DBUS_ASSERT_FULL (!G_PASTE_IS_IMAGE_ITEM (item) && !G_PASTE_IS_PASSWORD_ITEM (item),
                                  G_PASTE_ERROR_WRONG_ITEM_KIND, "an image or a password has no text to merge", NULL);

        g_string_append_printf (str, "%s%s%s%s",
                                (i) ? separator : "",
                                decoration,
                                g_paste_item_get_value (item),
                                decoration);
    }

    return g_paste_daemon_methods_do_add (self, str->str, str->len, error);
}

/* GSettings refusing the write -- a locked key, a settings file it cannot
 * rewrite -- leaves the tracking as it was, so the setting is read back: the
 * caller showing the state it asked for has to hear that it did not take. */
G_PASTE_VISIBLE void
g_paste_daemon_methods_set_active (const GPasteDaemonMethods *self,
                                   gboolean                   active,
                                   GError                   **error)
{
    g_paste_settings_set_track_changes (self->settings, active);
    g_paste_settings_sync_key (self->settings, G_PASTE_TRACK_CHANGES_SETTING);

    G_PASTE_DBUS_ASSERT (!g_paste_settings_get_track_changes (self->settings) == !active, G_PASTE_ERROR_FAILED, "the track-changes setting cannot be changed");

    /* The user's own say, over what the extension going away did. Cleared
     * only when set, as in g_paste_daemon_tracking (). */
    if (g_paste_settings_get_extension_paused (self->settings))
        g_paste_settings_set_extension_paused (self->settings, FALSE);
}

/* The policy itself, in one place: whether the extension coming or going should
 * drive the tracking is the "track-extension-state" key's business. Reached both
 * from the D-Bus method and, without a bus round trip, from a host running the
 * daemon in its own process (see g_paste_daemon_extension_state_changed()).
 * A write GSettings refuses is reported to nobody: the extension states a fact
 * and has nothing that shows the tracking to put right.
 *
 * The extension going pauses the tracking, and its coming back resumes only
 * what that pause stopped, which "extension-paused" remembers across restarts:
 * the extension says it is there to every daemon it finds, after a restart or a
 * handoff and each time the screen unlocks, and none of that is it coming back
 * -- a pause of the user's own has to outlive all of it
 * (/daemon/methods/extension-state). */
G_PASTE_VISIBLE void
g_paste_daemon_methods_extension_state_changed (const GPasteDaemonMethods *self,
                                                gboolean                   state)
{
    GPasteSettings *settings = self->settings;

    if (!g_paste_settings_get_track_extension_state (settings))
        return;

    if (!state && g_paste_settings_get_track_changes (settings))
    {
        g_paste_settings_set_extension_paused (settings, TRUE);
        g_paste_settings_set_track_changes (settings, FALSE);
    }
    else if (state && g_paste_settings_get_extension_paused (settings))
    {
        g_paste_settings_set_extension_paused (settings, FALSE);
        g_paste_settings_set_track_changes (settings, TRUE);
    }
}

/* The matches themselves, not their uuids: a caller wanting to show them would
 * only have to ask for every one of them straight back.
 *
 * A query too long, or no regular expression at all, is the caller's to hear
 * of rather than the daemon's journal, once per keystroke: refused, the
 * history's one compile of it saying which. */
G_PASTE_VISIBLE GVariant *
g_paste_daemon_methods_search (const GPasteDaemonMethods *self,
                               const gchar               *query,
                               GError                   **error)
{
    G_PASTE_DBUS_ASSERT_FULL (strlen (query) <= G_PASTE_HISTORY_MAX_SEARCH_LENGTH, G_PASTE_ERROR_INVALID_ARGUMENT, "the query is too long", NULL);

    g_autoptr (GPtrArray) items = g_paste_history_search_items (self->history, query, error);

    if (!items)
        return NULL;

    return g_paste_daemon_methods_items_variant (items);
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_set_favourite (const GPasteDaemonMethods *self,
                                      const gchar               *uuid,
                                      gboolean                   favourite,
                                      GError                   **error)
{
    if (!g_paste_daemon_methods_check_owned (self, error))
        return;

    G_PASTE_DBUS_ASSERT (g_paste_history_set_favourite (self->history, uuid, favourite), G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_select (const GPasteDaemonMethods *self,
                               const gchar               *uuid,
                               GError                   **error)
{
    if (!g_paste_daemon_methods_check_owned (self, error))
        return;

    G_PASTE_DBUS_ASSERT (g_paste_history_select (self->history, uuid), G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
}

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_replace (const GPasteDaemonMethods *self,
                                const gchar               *uuid,
                                const gchar               *contents,
                                GError                   **error)
{
    if (!g_paste_daemon_methods_check_owned (self, error))
        return NULL;

    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);
    G_PASTE_DBUS_ASSERT_FULL (G_PASTE_IS_TEXT_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND, "attempted to replace an item other than GPasteTextItem", NULL);
    G_PASTE_DBUS_ASSERT_FULL (contents && *contents, G_PASTE_ERROR_INVALID_ARGUMENT, "no contents given", NULL);

    gchar *new_uuid = g_paste_history_replace (self->history, uuid, contents);

    /* The history refuses contents weighing more than max-memory-usage on their
     * own, as an add of them is refused, and the item it matched is then still
     * there: that is what tells the refusal from an item gone since the lookup
     * above, which is unlocked. */
    G_PASTE_DBUS_ASSERT_FULL (new_uuid || !g_paste_history_get_by_uuid (self->history, uuid),
                              G_PASTE_ERROR_REJECTED,
                              "the content is larger than max-memory-usage",
                              NULL);
    G_PASTE_DBUS_ASSERT_FULL (new_uuid, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);

    return new_uuid;
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_strip_rich_text (const GPasteDaemonMethods *self,
                                        const gchar               *uuid,
                                        GError                   **error)
{
    if (!g_paste_daemon_methods_check_owned (self, error))
        return;

    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
    G_PASTE_DBUS_ASSERT (G_PASTE_IS_TEXT_ITEM (item), G_PASTE_ERROR_WRONG_ITEM_KIND,
                         "attempted to strip the rich text of an item other than GPasteTextItem");

    /* Asked again rather than taken for granted: an item that had no rich text to
     * begin with is stripped successfully, but the lookup above is unlocked, so
     * the item can be evicted, deleted or replaced by one of another kind before
     * the strip takes the history's lock -- and an empty reply is a success the
     * caller shows as done.
     *
     * Which of the two refusals it was decides the error, as it does above: the
     * interface answers WrongItemKind for anything but a text item, and a caller
     * told NOT_FOUND for one that is still there may drop it from its view. */
    gboolean found;
    gboolean stripped = g_paste_history_strip_rich_text (self->history, uuid, &found);

    G_PASTE_DBUS_ASSERT (found, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.");
    G_PASTE_DBUS_ASSERT (stripped, G_PASTE_ERROR_WRONG_ITEM_KIND,
                         "attempted to strip the rich text of an item other than GPasteTextItem");

    item = g_paste_history_get_by_uuid (self->history, uuid);
    if (item)
        g_paste_clipboards_manager_refresh_text (self->clipboards_manager, item);
}

G_PASTE_VISIBLE gchar *
g_paste_daemon_methods_make_password (const GPasteDaemonMethods *self,
                                      const gchar               *uuid,
                                      const gchar               *name,
                                      guint                      timeout,
                                      GError                   **error)
{
    /* The item a load has yet to bring back is not found either. */
    if (!g_paste_daemon_methods_check_owned (self, error) || !g_paste_daemon_methods_check_names_loaded (self, name, error))
        return NULL;

    GPasteItem *item = g_paste_history_get_by_uuid (self->history, uuid);

    G_PASTE_DBUS_ASSERT_FULL (item, G_PASTE_ERROR_NOT_FOUND, "Provided uuid doesn't match any item.", NULL);
    G_PASTE_DBUS_ASSERT_FULL (G_PASTE_IS_TEXT_ITEM (item) || G_PASTE_IS_PASSWORD_ITEM (item),
                              G_PASTE_ERROR_WRONG_ITEM_KIND,
                              "attempted to make a password of an item that is neither a GPasteTextItem nor a GPastePasswordItem",
                              NULL);
    G_PASTE_DBUS_ASSERT_FULL (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no password name given", NULL);

    /* Keeping the name it already carries is not a collision: the timeout may be
     * all the caller came to change. Neither is G_PASTE_PASSWORD_ITEM_NO_NAME,
     * which every nameless password reads as rather than a name anybody picked. */
    GPastePasswordItem *namesake = (g_paste_str_equal (name, G_PASTE_PASSWORD_ITEM_NO_NAME)) ? NULL : g_paste_history_get_password (self->history, name);

    G_PASTE_DBUS_ASSERT_FULL (!namesake || (GPasteItem *) namesake == item,
                              G_PASTE_ERROR_ALREADY_EXISTS,
                              "another password with that name already exists",
                              NULL);

    /* The checks above leave one refusal: a password its name makes heavier
     * than the memory cap, which Replace refuses for contents as heavy. */
    gchar *new_uuid = g_paste_history_make_password (self->history, uuid, name, timeout);

    G_PASTE_DBUS_ASSERT_FULL (new_uuid, G_PASTE_ERROR_REJECTED, "the password, its name included, weighs more than max-memory-usage", NULL);

    /* @timeout is how long the password may stay on a selection, so a countdown
     * armed from the old one is now running for the wrong duration. The history
     * publishes nothing here, which is what leaves the re-arming to us. */
    GPasteItem *password = g_paste_history_get_by_uuid (self->history, new_uuid);

    if (password)
        g_paste_clipboards_manager_rearm_password (self->clipboards_manager, password);

    return new_uuid;
}

G_PASTE_VISIBLE void
g_paste_daemon_methods_switch_history (const GPasteDaemonMethods *self,
                                       const gchar               *name,
                                       GError                   **error)
{
    G_PASTE_DBUS_ASSERT (name && *name, G_PASTE_ERROR_INVALID_ARGUMENT, "no history to switch to");
    G_PASTE_DBUS_ASSERT_HISTORY_NAME (name);

    /* What was copied while the store is handed over is the history left's,
     * kept for the reload ending the handover to record: a switch would drop
     * it unrecorded. A history-name written straight into GSettings meanwhile
     * still does, the D-Bus switch being the one this can refuse. */
    if (!g_paste_daemon_methods_check_owned (self, error))
        return;

    /* Switching to a name no history goes by creates it: that is how a new
     * history comes into being, there being no CreateHistory of its own, and it
     * is the set of histories growing just as much as a backup is. */
    gboolean is_new = g_paste_daemon_methods_history_is_new (self, name);

    G_PASTE_DBUS_ASSERT (!is_new || g_paste_daemon_methods_name_fits (self, name),
                         G_PASTE_ERROR_INVALID_ARGUMENT, "too long a name for a history of this storage");

    G_PASTE_DBUS_ASSERT (g_paste_history_switch (self->history, name), G_PASTE_ERROR_FAILED, "the history-name setting cannot be changed");

    if (is_new)
        g_paste_daemon3_emit_raw_histories_changed (self->skeleton);
}
