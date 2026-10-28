// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The mutter backend, against a real MetaSelection outside gnome-shell and,
 * like gnome-shell, with GTK never initialised: whatever it reads or publishes
 * beyond text has to go through formats GDK knows without gtk_init (). */

#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-clipboard-provider-private.h>
#include <gpaste-daemon/gpaste-clipboard-meta.h>
#include <gpaste-daemon/gpaste-daemon-util.h>
#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-history-private.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-password-item.h>
#include <gpaste-daemon/gpaste-sensitive-mime.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <meta/meta-selection-source-memory.h>

#include <dlfcn.h>
#include <glib/gstdio.h>
#include <string.h>

/* The texts wiped as they were freed which held @wiped_secret: a published
 * password's copies are. Counted by defining libgpaste-daemon's own function
 * again, which only works where the library's calls reach the binary:
 * library_interposable in tests/meson.build says where they do not, and the
 * definition reading @wiped_secret is left out there. */
static const gchar *wiped_secret G_GNUC_UNUSED;
static guint wipes_of_secret;

#ifndef G_PASTE_TEST_LIBRARY_NOT_INTERPOSABLE
void
g_paste_util_free_cleartext (GPasteCleartext *cleartext)
{
    typedef void (*Free) (GPasteCleartext *);
    Free original = (Free) dlsym (RTLD_NEXT, "g_paste_util_free_cleartext");

    if (wiped_secret && cleartext && g_str_equal (cleartext, wiped_secret))
        ++wipes_of_secret;

    original (cleartext);
}
#endif

/* The PNG encodes GTK makes, as the serializer an image's own bytes spare
 * makes them: GTK's, reached from the library through the PLT. */
static guint png_encodes;

GBytes *
gdk_texture_save_to_png_bytes (GdkTexture *texture)
{
    typedef GBytes *(*Save) (GdkTexture *texture);
    static Save real;

    if (!real)
        real = (Save) dlsym (RTLD_NEXT, "gdk_texture_save_to_png_bytes");
    g_assert_nonnull (real);
    ++png_encodes;

    return real (texture);
}

typedef struct
{
    GPasteSettings          *settings;
    GPasteHistory           *history;
    GPasteClipboardsManager *manager;
    MetaSelection           *selection;
    GPasteClipboardProvider *clipboard;
} Fixture;

static void
fixture_setup (Fixture      *f,
               gconstpointer user_data G_GNUC_UNUSED)
{
    f->settings = g_paste_settings_new ();
    g_paste_settings_set_storage_backend (f->settings, G_PASTE_STORAGE_NOOP);
    g_paste_settings_set_images_support (f->settings, TRUE);
    g_paste_settings_set_synchronize_clipboards (f->settings, FALSE);
    f->history = g_paste_history_new (f->settings);
    g_paste_history_load (f->history, "meta-tests");
    f->manager = g_paste_clipboards_manager_new (f->history, f->settings);
    f->selection = meta_selection_new (NULL);
    f->clipboard = g_paste_clipboard_meta_new_clipboard (f->selection, f->settings);
    g_paste_clipboards_manager_add_clipboard (f->manager, f->clipboard);
    g_paste_clipboards_manager_activate (f->manager);
}

static void
fixture_teardown (Fixture      *f,
                  gconstpointer user_data G_GNUC_UNUSED)
{
    g_clear_object (&f->manager);
    g_clear_object (&f->clipboard);
    g_clear_object (&f->selection);
    g_clear_object (&f->history);
    g_clear_object (&f->settings);

    /* What the case left to conclude, a read given up on concluding on the
     * next turn, holds the manager and everything behind it: run here, so that
     * nothing of this case reaches the next. */
    while (g_main_context_iteration (NULL, FALSE));
}

static void
pump_until_head (Fixture       *f,
                 GPasteItemKind kind)
{
    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

    while (g_get_monotonic_time () < deadline &&
           (!g_paste_history_get_length (f->history) || g_paste_item_get_kind (g_paste_history_get (f->history, 0)) != kind))
    {
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    }
    g_assert_cmpuint (g_paste_history_get_length (f->history), >, 0);
    g_assert_cmpint (g_paste_item_get_kind (g_paste_history_get (f->history, 0)), ==, kind);
}

static GBytes *
png_bytes (void)
{
    static const guchar pixels[] = { 1, 2, 3, 255, 4, 5, 6, 255 };
    g_autoptr (GBytes) data = g_bytes_new_static (pixels, sizeof (pixels));
    g_autoptr (GdkTexture) texture = gdk_memory_texture_new (2, 1, GDK_MEMORY_R8G8B8A8, data, 8);

    return gdk_texture_save_to_png_bytes (texture);
}

static void
offer (Fixture     *f,
       const gchar *mimetype,
       GBytes      *bytes)
{
    g_autoptr (GError) error = NULL;
    g_autoptr (MetaSelectionSource) source = meta_selection_source_memory_new (mimetype, bytes, &error);

    g_assert_no_error (error);
    meta_selection_set_owner (f->selection, META_SELECTION_CLIPBOARD, source);
}

/* A selection owner offering several formats of one copy, as applications do
 * and #MetaSelectionSourceMemory cannot. */
G_DECLARE_FINAL_TYPE (MultiSource, multi_source, MULTI, SOURCE, MetaSelectionSource)

struct _MultiSource
{
    MetaSelectionSource parent_instance;

    GList       *mimetypes;
    GHashTable  *contents;
    /* A mimetype whose read is held rather than answered, and the read it
     * holds, for the case to answer. */
    const gchar *held_mime;
    GTask       *held;
};

G_DEFINE_FINAL_TYPE (MultiSource, multi_source, META_TYPE_SELECTION_SOURCE)

static gpointer
copy_string (gconstpointer string,
             gpointer      user_data G_GNUC_UNUSED)
{
    return g_strdup (string);
}

static GList *
multi_source_get_mimetypes (MetaSelectionSource *source)
{
    return g_list_copy_deep (MULTI_SOURCE (source)->mimetypes, copy_string, NULL);
}

static void
multi_source_read_async (MetaSelectionSource *source,
                         const gchar         *mimetype,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
    MultiSource *self = MULTI_SOURCE (source);
    g_autoptr (GTask) task = g_task_new (source, cancellable, callback, user_data);
    GBytes *bytes = g_hash_table_lookup (self->contents, mimetype);

    if (self->held_mime && g_str_equal (mimetype, self->held_mime))
    {
        g_assert_null (self->held);
        self->held = g_steal_pointer (&task);
    }
    else if (bytes)
        g_task_return_pointer (task, g_memory_input_stream_new_from_bytes (bytes), g_object_unref);
    else
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s is not offered", mimetype);
}

static GInputStream *
multi_source_read_finish (MetaSelectionSource *source G_GNUC_UNUSED,
                          GAsyncResult        *result,
                          GError             **error)
{
    return g_task_propagate_pointer (G_TASK (result), error);
}

static void
multi_source_dispose (GObject *object)
{
    g_clear_pointer (&MULTI_SOURCE (object)->contents, g_hash_table_unref);
    G_OBJECT_CLASS (multi_source_parent_class)->dispose (object);
}

static void
multi_source_finalize (GObject *object)
{
    g_list_free_full (MULTI_SOURCE (object)->mimetypes, g_free);
    G_OBJECT_CLASS (multi_source_parent_class)->finalize (object);
}

static void
multi_source_class_init (MultiSourceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    MetaSelectionSourceClass *source_class = META_SELECTION_SOURCE_CLASS (klass);

    object_class->dispose = multi_source_dispose;
    object_class->finalize = multi_source_finalize;
    source_class->get_mimetypes = multi_source_get_mimetypes;
    source_class->read_async = multi_source_read_async;
    source_class->read_finish = multi_source_read_finish;
}

static void
multi_source_init (MultiSource *self)
{
    self->contents = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_bytes_unref);
}

/* One copy offering each of @mimetypes, %NULL-terminated, with the matching
 * one of @contents, in that order. */
static MultiSource *
multi_source_new (const gchar * const *mimetypes,
                  GBytes             **contents)
{
    MultiSource *source = g_object_new (multi_source_get_type (), NULL);

    for (guint i = 0; mimetypes[i]; ++i)
    {
        source->mimetypes = g_list_append (source->mimetypes, g_strdup (mimetypes[i]));
        g_hash_table_insert (source->contents, g_strdup (mimetypes[i]), g_bytes_ref (contents[i]));
    }

    return source;
}

/* That copy, as the selection's owner. */
static void
offer_formats (Fixture            *f,
               const gchar * const *mimetypes,
               GBytes             **contents)
{
    g_autoptr (MultiSource) source = multi_source_new (mimetypes, contents);

    meta_selection_set_owner (f->selection, META_SELECTION_CLIPBOARD, META_SELECTION_SOURCE (source));
}

/* Until the reads the last copy started are over. */
static void
pump_until_read (Fixture *f)
{
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

    do
    {
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    } while (g_paste_clipboard_provider_is_reading (f->clipboard) && g_get_monotonic_time () < deadline);
    g_assert_false (g_paste_clipboard_provider_is_reading (f->clipboard));
}

/* What a screenshot tool, a colour picker and a file manager copy, each offering
 * nothing else, is recorded as what it is. */
static void
capture (Fixture      *f,
         gconstpointer user_data)
{
    GPasteItemKind kind = GPOINTER_TO_INT (user_data);

    switch (kind)
    {
    case G_PASTE_ITEM_KIND_IMAGE:
    {
        g_autoptr (GBytes) png = png_bytes ();

        offer (f, "image/png", png);
        break;
    }
    case G_PASTE_ITEM_KIND_COLOR:
    {
        static const guint16 channels[] = { 65535, 0, 0, 65535 };
        g_autoptr (GBytes) color = g_bytes_new_static (channels, sizeof (channels));

        offer (f, "application/x-color", color);
        break;
    }
    case G_PASTE_ITEM_KIND_URIS:
    {
        g_autoptr (GBytes) uris = g_bytes_new_static ("file:///tmp/a\r\nfile:///tmp/b\r\n", 30);

        offer (f, "text/uri-list", uris);
        break;
    }
    default:
        g_assert_not_reached ();
    }

    pump_until_head (f, kind);
}

/* Spreadsheet cells are offered as a text beside a picture of themselves, and
 * the text is what is recorded, whichever the owner lists first. */
static void
text_beside_image (Fixture      *f,
                   gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GBytes) png = png_bytes ();
    g_autoptr (GBytes) text = g_bytes_new_static ("A1\tB1", 5);
    const gchar *mimetypes[] = { "image/png", "text/plain;charset=utf-8", NULL };
    GBytes *contents[] = { png, text };

    offer_formats (f, mimetypes, contents);
    pump_until_head (f, G_PASTE_ITEM_KIND_TEXT);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (f->history, 0)), ==, "A1\tB1");
}

/* A file list naming more files than G_PASTE_CLIPBOARD_MAX_FILES is left out
 * unread, as is an empty one; the next copy is read as ever. */
static void
file_list_bounded (Fixture      *f,
                   gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GString) list = g_string_new (NULL);

    for (guint i = 0; i <= G_PASTE_CLIPBOARD_MAX_FILES; ++i)
        g_string_append (list, "file:///a\r\n");

    g_autoptr (GBytes) too_many = g_string_free_to_bytes (g_steal_pointer (&list));
    g_autoptr (GBytes) none = g_bytes_new_static ("", 0);
    g_autoptr (GBytes) some = g_bytes_new_static ("file:///tmp/a\r\n", 15);

    offer (f, "text/uri-list", too_many);
    pump_until_read (f);
    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 0);

    offer (f, "text/uri-list", none);
    pump_until_read (f);
    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 0);

    offer (f, "text/uri-list", some);
    pump_until_head (f, G_PASTE_ITEM_KIND_URIS);
}

/* A secret, which no size setting turns down, is read whole up to the memory
 * limit and left out past it, rather than kept cut at the limit. */
static void
secret_bounded (Fixture      *f,
                gconstpointer user_data G_GNUC_UNUSED)
{
    gsize limit = 5 * 1024 * 1024;
    const gchar *mimetypes[] = { "text/plain;charset=utf-8", "x-kde-passwordManagerHint", NULL };
    g_autoptr (GBytes) hint = g_bytes_new_static ("secret", 6);

    g_paste_settings_set_max_memory_usage (f->settings, 5);
    g_assert_cmpuint (g_paste_settings_get_max_memory_usage (f->settings), ==, 5);

    /* Two different secrets: one cut at the limit must not pass for the first. */
    for (gsize size = limit; size <= limit + 1; ++size)
    {
        g_autoptr (GBytes) secret = g_bytes_new_take (g_strnfill (size, (size == limit) ? 'a' : 'b'), size);
        GBytes *contents[] = { secret, hint };

        offer_formats (f, mimetypes, contents);
        pump_until_read (f);
    }

    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 1);

    GPasteItem *head = g_paste_history_get (f->history, 0);

    g_assert_cmpint (g_paste_item_get_kind (head), ==, G_PASTE_ITEM_KIND_PASSWORD);
    g_assert_cmpuint (strlen (g_paste_item_get_real_value (head)), ==, limit);
}

/* A text whose owner terminates it with a NUL is the text before it. */
static void
nul_terminated_text (Fixture      *f,
                     gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GBytes) text = g_bytes_new_static ("hello again", 12);

    offer (f, "text/plain;charset=utf-8", text);
    pump_until_head (f, G_PASTE_ITEM_KIND_TEXT);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (f->history, 0)), ==, "hello again");
}

/* An input stream serving 'a's up to @total, counting what it served. */
G_DECLARE_FINAL_TYPE (CountingStream, counting_stream, COUNTING, STREAM, GInputStream)

struct _CountingStream
{
    GInputStream parent_instance;

    gsize served;
    gsize total;
};

G_DEFINE_FINAL_TYPE (CountingStream, counting_stream, G_TYPE_INPUT_STREAM)

static gssize
counting_stream_read (GInputStream *stream,
                      void         *buffer,
                      gsize         count,
                      GCancellable *cancellable G_GNUC_UNUSED,
                      GError      **error G_GNUC_UNUSED)
{
    CountingStream *self = COUNTING_STREAM (stream);
    gsize n = MIN (count, self->total - self->served);

    memset (buffer, 'a', n);
    self->served += n;
    return (gssize) n;
}

static gboolean
counting_stream_close (GInputStream *stream G_GNUC_UNUSED,
                       GCancellable *cancellable G_GNUC_UNUSED,
                       GError      **error G_GNUC_UNUSED)
{
    return TRUE;
}

static void
counting_stream_class_init (CountingStreamClass *klass)
{
    G_INPUT_STREAM_CLASS (klass)->read_fn = counting_stream_read;
    G_INPUT_STREAM_CLASS (klass)->close_fn = counting_stream_close;
}

static void
counting_stream_init (CountingStream *self G_GNUC_UNUSED)
{
}

/* A selection owner serving its text from a #CountingStream. */
G_DECLARE_FINAL_TYPE (CountingSource, counting_source, COUNTING, SOURCE, MetaSelectionSource)

struct _CountingSource
{
    MetaSelectionSource parent_instance;

    CountingStream *stream;
};

G_DEFINE_FINAL_TYPE (CountingSource, counting_source, META_TYPE_SELECTION_SOURCE)

static GList *
counting_source_get_mimetypes (MetaSelectionSource *source G_GNUC_UNUSED)
{
    return g_list_prepend (NULL, g_strdup ("text/plain;charset=utf-8"));
}

static void
counting_source_read_async (MetaSelectionSource *source,
                            const gchar         *mimetype G_GNUC_UNUSED,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
    g_autoptr (GTask) task = g_task_new (source, cancellable, callback, user_data);

    g_task_return_pointer (task, g_object_ref (COUNTING_SOURCE (source)->stream), g_object_unref);
}

static GInputStream *
counting_source_read_finish (MetaSelectionSource *source G_GNUC_UNUSED,
                             GAsyncResult        *result,
                             GError             **error)
{
    return g_task_propagate_pointer (G_TASK (result), error);
}

static void
counting_source_dispose (GObject *object)
{
    g_clear_object (&COUNTING_SOURCE (object)->stream);
    G_OBJECT_CLASS (counting_source_parent_class)->dispose (object);
}

static void
counting_source_class_init (CountingSourceClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = counting_source_dispose;
    META_SELECTION_SOURCE_CLASS (klass)->get_mimetypes = counting_source_get_mimetypes;
    META_SELECTION_SOURCE_CLASS (klass)->read_async = counting_source_read_async;
    META_SELECTION_SOURCE_CLASS (klass)->read_finish = counting_source_read_finish;
}

static void
counting_source_init (CountingSource *self G_GNUC_UNUSED)
{
}

/* A read takes no more than the memory limit allows, whatever the owner serves:
 * nothing larger could be kept, and an endless offer would otherwise have the
 * compositor buffer it until the read's deadline. */
static void
bounded_read (Fixture      *f,
              gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (CountingSource) source = g_object_new (counting_source_get_type (), NULL);
    gsize limit = 5 * 1024 * 1024;

    g_paste_settings_set_max_memory_usage (f->settings, 5);
    source->stream = g_object_new (counting_stream_get_type (), NULL);
    source->stream->total = 8 * limit;
    meta_selection_set_owner (f->selection, META_SELECTION_CLIPBOARD, META_SELECTION_SOURCE (source));

    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;

    while (g_paste_clipboard_provider_is_reading (f->clipboard) && g_get_monotonic_time () < deadline)
    {
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    }
    g_assert_false (g_paste_clipboard_provider_is_reading (f->clipboard));
    /* One read's worth of slack past the limit, which is what a read may take
     * before the transfer notices it is done. */
    g_assert_cmpuint (source->stream->served, <=, limit + 1 + 65536);
    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 0);
}

typedef struct
{
    gboolean done;
    GError  *error;
} Transfer;

static void
on_transferred (GObject      *source,
                GAsyncResult *result,
                gpointer      user_data)
{
    Transfer *transfer = user_data;

    meta_selection_transfer_finish (META_SELECTION (source), result, &transfer->error);
    transfer->done = TRUE;
}

/* The image the selection serves as PNG, read as another application would:
 * the one select_image () put there, pixel for pixel. Returns how many PNG
 * encodes serving it took. */
static guint
assert_serves_image (Fixture *f)
{
    Transfer transfer = { 0 };
    guint encodes_before = png_encodes;
    g_autoptr (GOutputStream) stream = g_memory_output_stream_new_resizable ();
    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

    meta_selection_transfer_async (f->selection, META_SELECTION_CLIPBOARD, "image/png", -1, stream, NULL, on_transferred, &transfer);
    while (!transfer.done && g_get_monotonic_time () < deadline)
    {
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    }
    g_assert_true (transfer.done);
    g_assert_no_error (transfer.error);
    g_output_stream_close (stream, NULL, NULL);

    guint encodes = png_encodes - encodes_before;

    g_autoptr (GBytes) served = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (stream));
    g_autoptr (GdkTexture) texture = gdk_texture_new_from_bytes (served, NULL);

    g_autoptr (GBytes) png = png_bytes ();
    g_autoptr (GdkTexture) expected = gdk_texture_new_from_bytes (png, NULL);
    guchar pixels[2 * 4], expected_pixels[2 * 4];

    g_assert_nonnull (texture);
    g_assert_cmpint (gdk_texture_get_width (texture), ==, 2);
    g_assert_cmpint (gdk_texture_get_height (texture), ==, 1);
    gdk_texture_download (texture, pixels, 2 * 4);
    gdk_texture_download (expected, expected_pixels, 2 * 4);
    g_assert_cmpmem (pixels, sizeof (pixels), expected_pixels, sizeof (expected_pixels));

    return encodes;
}

static void
select_image (Fixture *f)
{
    g_autoptr (GBytes) png = png_bytes ();
    g_autoptr (GDateTime) date = g_date_time_new_now_local ();
    g_autoptr (GPasteItem) item = g_paste_image_item_new_from_bytes (png, date, NULL);

    /* Active, as the history's head is when it is published. */
    g_assert_nonnull (item);
    g_paste_item_set_state (item, G_PASTE_ITEM_STATE_ACTIVE);
    g_assert_cmpint (g_paste_clipboard_provider_select_item_full (f->clipboard, item, TRUE), !=, G_PASTE_CLIPBOARD_PUBLICATION_INVALID);
}

/* An image put back on the clipboard is offered as a PNG, which reads back as
 * the same image -- its own bytes, encoded again for no read. */
static void
publish_image (Fixture      *f,
               gconstpointer user_data G_GNUC_UNUSED)
{
    select_image (f);

    GList *mimetypes = meta_selection_get_mimetypes (f->selection, META_SELECTION_CLIPBOARD);
    gboolean offered = FALSE;

    for (const GList *m = mimetypes; m; m = m->next)
        offered |= g_str_equal (m->data, "image/png");
    g_list_free_full (mimetypes, g_free);
    g_assert_true (offered);

    g_assert_cmpuint (assert_serves_image (f), ==, 0);
}

/* What the selection serves as @mime, read as another application would. */
static GBytes *
transfer (Fixture     *f,
          const gchar *mime)
{
    Transfer transfer = { 0 };
    g_autoptr (GOutputStream) stream = g_memory_output_stream_new_resizable ();
    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

    meta_selection_transfer_async (f->selection, META_SELECTION_CLIPBOARD, mime, -1, stream, NULL, on_transferred, &transfer);
    while (!transfer.done && g_get_monotonic_time () < deadline)
    {
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    }
    g_assert_true (transfer.done);
    g_assert_no_error (transfer.error);
    g_output_stream_close (stream, NULL, NULL);

    return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (stream));
}

static void
on_serialized (GObject      *source_object G_GNUC_UNUSED,
               GAsyncResult *result,
               gpointer      user_data)
{
    Transfer *transfer = user_data;

    gdk_content_serialize_finish (result, &transfer->error);
    transfer->done = TRUE;
}

/* The uri-list serializer GPaste registers for the whole of gnome-shell writes
 * nothing for an empty file list, as GTK's own does, rather than read through
 * the NULL it is. */
static void
serialize_empty_file_list (Fixture      *f G_GNUC_UNUSED,
                           gconstpointer user_data G_GNUC_UNUSED)
{
    g_auto (GValue) value = G_VALUE_INIT;
    g_autoptr (GOutputStream) stream = g_memory_output_stream_new_resizable ();
    Transfer transfer = { 0 };
    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

    g_value_init (&value, GDK_TYPE_FILE_LIST);
    gdk_content_serialize_async (stream, "text/uri-list", &value, G_PRIORITY_DEFAULT, NULL, on_serialized, &transfer);
    while (!transfer.done && g_get_monotonic_time () < deadline)
    {
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    }
    g_assert_true (transfer.done);
    g_assert_no_error (transfer.error);
    g_output_stream_close (stream, NULL, NULL);
    g_assert_cmpuint (g_memory_output_stream_get_data_size (G_MEMORY_OUTPUT_STREAM (stream)), ==, 0);
}

/* How many times @type offers @mime. */
static guint
offered_times (Fixture          *f,
               MetaSelectionType type,
               const gchar      *mime)
{
    GList *mimetypes = meta_selection_get_mimetypes (f->selection, type);
    guint times = 0;

    for (const GList *m = mimetypes; m; m = m->next)
        times += g_str_equal (m->data, mime);
    g_list_free_full (mimetypes, g_free);

    return times;
}

/* Whether @type offers @mime. */
static gboolean
offers (Fixture          *f,
        MetaSelectionType type,
        const gchar      *mime)
{
    return offered_times (f, type, mime) > 0;
}

static void
pump (guint ms)
{
    gint64 deadline = g_get_monotonic_time () + ms * 1000;

    while (g_get_monotonic_time () < deadline)
    {
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    }
}

/* A text put back on the clipboard is offered as a #GdkContentProvider for a
 * string would offer it, the bare text/plain beside the UTF-8 one; a colour
 * with its text form; a password with the hint that it is one; and a text
 * with what rich formats it carries. */
static void
publish_offers (Fixture      *f,
                gconstpointer user_data G_GNUC_UNUSED)
{
    {
        g_autoptr (GPasteItem) text = g_paste_text_item_new ("published");

        g_paste_item_add_special_value (text, g_paste_binary_data_new (G_PASTE_SPECIAL_MIME_TEXT_HTML, g_bytes_new_static ("<b>published</b>", 16)));
        g_assert_cmpint (g_paste_clipboard_provider_select_item_full (f->clipboard, text, TRUE), ==, G_PASTE_CLIPBOARD_PUBLICATION_PUBLISHED);
        g_assert_true (offers (f, META_SELECTION_CLIPBOARD, "text/plain;charset=utf-8"));
        g_assert_true (offers (f, META_SELECTION_CLIPBOARD, "text/plain"));
        g_assert_true (offers (f, META_SELECTION_CLIPBOARD, g_paste_special_mime_get (G_PASTE_SPECIAL_MIME_TEXT_HTML)));
        g_assert_false (offers (f, META_SELECTION_CLIPBOARD, g_paste_sensitive_mime_get (G_PASTE_SENSITIVE_MIME_FIRST)));
    }
    {
        g_autoptr (GPasteItem) color = g_paste_color_item_new_from_str ("#ff0000");

        g_assert_nonnull (color);
        g_assert_cmpint (g_paste_clipboard_provider_select_item_full (f->clipboard, color, TRUE), ==, G_PASTE_CLIPBOARD_PUBLICATION_PUBLISHED);
        g_assert_true (offers (f, META_SELECTION_CLIPBOARD, "application/x-color"));
        g_assert_true (offers (f, META_SELECTION_CLIPBOARD, "text/plain;charset=utf-8"));
    }
    {
        g_autoptr (GPasteItem) password = g_paste_password_item_new ("name", "secret", 0);

        g_assert_cmpint (g_paste_clipboard_provider_select_item_full (f->clipboard, password, TRUE), !=, G_PASTE_CLIPBOARD_PUBLICATION_INVALID);
        for (GPasteSensitiveMime mime = G_PASTE_SENSITIVE_MIME_FIRST; mime < G_PASTE_SENSITIVE_MIME_LAST; ++mime)
            g_assert_true (offers (f, META_SELECTION_CLIPBOARD, g_paste_sensitive_mime_get (mime)));
    }
}

/* A password published is wiped from the source serving it once another owner
 * takes the selection over, as the cache wipes its own copy then: the two of
 * them, rather than one left in freed memory. */
static void
publish_wipes_password (Fixture      *f,
                        gconstpointer user_data G_GNUC_UNUSED)
{
    if (!g_paste_test_env_library_interposable ("The library's own calls do not reach the binary"))
        return;

    g_autoptr (GPasteItem) password = g_paste_password_item_new ("name", "secret", 0);
    g_autoptr (GBytes) text = g_bytes_new_static ("other", 5);

    g_assert_cmpint (g_paste_clipboard_provider_select_item_full (f->clipboard, password, TRUE), !=, G_PASTE_CLIPBOARD_PUBLICATION_INVALID);
    wiped_secret = "secret";
    wipes_of_secret = 0;
    offer (f, "text/plain;charset=utf-8", text);
    pump_until_read (f);
    wiped_secret = NULL;
    g_assert_cmpuint (wipes_of_secret, ==, 2);
}

/* What the selection's owner offers: kept for the case to release. */
static MetaSelectionSource *
own (Fixture     *f,
     const gchar *mimetype,
     GBytes      *bytes)
{
    g_autoptr (GError) error = NULL;
    MetaSelectionSource *source = meta_selection_source_memory_new (mimetype, bytes, &error);

    g_assert_no_error (error);
    meta_selection_set_owner (f->selection, META_SELECTION_CLIPBOARD, source);

    return source;
}

/* A selection its owner lets go of is empty to GPaste, which puts the history's
 * head back on it. */
static void
release_puts_head_back (Fixture      *f,
                        gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GBytes) text = g_bytes_new_static ("copied", 6);
    g_autoptr (MetaSelectionSource) source = own (f, "text/plain;charset=utf-8", text);

    pump_until_head (f, G_PASTE_ITEM_KIND_TEXT);
    meta_selection_unset_owner (f->selection, META_SELECTION_CLIPBOARD, source);
    pump (200);
    g_assert_true (offers (f, META_SELECTION_CLIPBOARD, "text/plain;charset=utf-8"));

    g_autoptr (GBytes) served = transfer (f, "text/plain;charset=utf-8");

    g_assert_true (g_bytes_equal (served, text));
}

/* The owners an owner-changed handler of the clipboard is told of, in order:
 * N for none, S for a source. */
static void
on_clipboard_owner (MetaSelection       *selection G_GNUC_UNUSED,
                    guint                selection_type,
                    MetaSelectionSource *owner,
                    gpointer             user_data)
{
    if ((MetaSelectionType) selection_type == META_SELECTION_CLIPBOARD)
        g_string_append_c (user_data, (owner) ? 'S' : 'N');
}

/* A handler connected after GPaste's is told of a release before the head
 * GPaste puts back on the selection, as one connected before it is: told the
 * other way round, it would end up taking for empty a selection GPaste owns. */
static void
release_emission_order (Fixture      *f,
                        gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GBytes) text = g_bytes_new_static ("copied", 6);
    g_autoptr (MetaSelectionSource) source = own (f, "text/plain;charset=utf-8", text);
    g_autoptr (GString) owners = g_string_new (NULL);

    pump_until_head (f, G_PASTE_ITEM_KIND_TEXT);

    gulong handler = g_signal_connect (f->selection, "owner-changed", G_CALLBACK (on_clipboard_owner), owners);

    meta_selection_unset_owner (f->selection, META_SELECTION_CLIPBOARD, source);
    pump_until_read (f);
    g_signal_handler_disconnect (f->selection, handler);

    g_assert_cmpstr (owners->str, ==, "NS");
    g_assert_true (offers (f, META_SELECTION_CLIPBOARD, "text/plain;charset=utf-8"));
}

/* An owner serving its text and leaving before a second read answers -- the
 * hint, here -- has its copy recorded all the same, and put back on the
 * selection it left empty, the release being answered only once the read it
 * ended is in (g_paste_clipboard_meta_answer_release ()). With @user_data, the
 * copy also reaches the primary selection, as one whose owner stayed does. */
static void
release_after_text (Fixture      *f,
                    gconstpointer user_data)
{
    gboolean synced = GPOINTER_TO_INT (user_data);
    const gchar *mimetypes[] = { "text/plain;charset=utf-8", "x-kde-passwordManagerHint", NULL };
    g_autoptr (GBytes) text = g_bytes_new_static ("served then gone", 16);
    g_autoptr (GBytes) hint = g_bytes_new_static ("secret", 6);
    GBytes *contents[] = { text, hint };
    g_autoptr (MultiSource) source = multi_source_new (mimetypes, contents);
    g_autoptr (GPasteClipboardProvider) primary = NULL;

    if (synced)
    {
        primary = g_paste_clipboard_meta_new_primary (f->selection, f->settings);
        g_paste_clipboards_manager_add_clipboard (f->manager, primary);
        g_paste_settings_set_synchronize_clipboards (f->settings, TRUE);
    }

    source->held_mime = "x-kde-passwordManagerHint";
    meta_selection_set_owner (f->selection, META_SELECTION_CLIPBOARD, META_SELECTION_SOURCE (source));

    /* Every step of the text's transfer runs on this main context, the streams
     * it goes through being memory ones: once nothing is left to dispatch, the
     * text is in and the hint alone is out. */
    while (g_main_context_iteration (NULL, FALSE));
    g_assert_nonnull (source->held);
    g_assert_true (g_paste_clipboard_provider_is_reading (f->clipboard));

    meta_selection_unset_owner (f->selection, META_SELECTION_CLIPBOARD, META_SELECTION_SOURCE (source));
    pump_until_read (f);

    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 1);
    g_assert_cmpstr (g_paste_item_get_value (g_paste_history_get (f->history, 0)), ==, "served then gone");
    g_assert_true (offers (f, META_SELECTION_CLIPBOARD, "text/plain;charset=utf-8"));

    g_autoptr (GBytes) served = transfer (f, "text/plain;charset=utf-8");

    g_assert_true (g_bytes_equal (served, text));
    if (synced)
    {
        g_assert_true (offers (f, META_SELECTION_PRIMARY, "text/plain;charset=utf-8"));
        g_assert_cmpstr (g_paste_clipboard_provider_get_text (primary), ==, "served then gone");
    }

    /* What the gone owner never answered, for the read to report and go. */
    g_autoptr (GTask) held = g_steal_pointer (&source->held);

    g_task_return_new_error (held, G_IO_ERROR, G_IO_ERROR_CLOSED, "The owner is gone");
}

/* An empty transfer is the empty text, which is what a password manager
 * clearing its copy writes -- not a read that failed. */
static void
read_empty_text (Fixture      *f,
                 gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GBytes) empty = g_bytes_new_static ("", 0);

    offer (f, "text/plain;charset=utf-8", empty);
    pump_until_read (f);
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (f->clipboard), ==, "");
}

/* An owner offering only what GPaste does not handle keeps the selection: it
 * is no empty one to put the history's head back on. */
static void
read_unhandled_kept (Fixture      *f,
                     gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GBytes) head = g_bytes_new_static ("head", 4);
    g_autoptr (GBytes) unhandled = g_bytes_new_static ("whatever", 8);

    offer (f, "text/plain;charset=utf-8", head);
    pump_until_head (f, G_PASTE_ITEM_KIND_TEXT);
    offer (f, "application/x-gpaste-test", unhandled);
    pump_until_read (f);
    pump (200);
    g_assert_false (g_paste_clipboard_provider_is_empty (f->clipboard));
    g_assert_true (offers (f, META_SELECTION_CLIPBOARD, "application/x-gpaste-test"));
    g_assert_false (offers (f, META_SELECTION_CLIPBOARD, "text/plain;charset=utf-8"));
}

/* Syncing the clipboard to the primary selection hands the primary the text
 * the clipboard holds. */
static void
sync_delivers (Fixture      *f,
               gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GPasteClipboardProvider) primary = g_paste_clipboard_meta_new_primary (f->selection, f->settings);
    g_autoptr (GBytes) text = g_bytes_new_static ("synced", 6);

    g_paste_clipboards_manager_add_clipboard (f->manager, primary);
    offer (f, "text/plain;charset=utf-8", text);
    pump_until_head (f, G_PASTE_ITEM_KIND_TEXT);
    g_paste_clipboards_manager_sync_from_to (f->manager, TRUE);

    gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

    while (!offers (f, META_SELECTION_PRIMARY, "text/plain;charset=utf-8") && g_get_monotonic_time () < deadline)
    {
        if (!g_main_context_iteration (NULL, FALSE))
            g_usleep (1000);
    }
    g_assert_true (offers (f, META_SELECTION_PRIMARY, "text/plain;charset=utf-8"));
    g_assert_cmpstr (g_paste_clipboard_provider_get_text (primary), ==, "synced");
}

/* The text form GTK's own serializer writes a file list as, which a host that
 * initialised GTK has GDK know: each file's path, or its uri, a line each. */
static void
serialize_file_list_text (GdkContentSerializer *serializer)
{
    GdkFileList *file_list = g_value_get_boxed (gdk_content_serializer_get_value (serializer));
    g_autoptr (GSList) files = gdk_file_list_get_files (file_list);
    g_autoptr (GString) text = g_string_new (NULL);

    for (const GSList *file = files; file; file = file->next)
    {
        g_autofree gchar *path = g_file_get_path (file->data);
        g_autofree gchar *uri = (path) ? NULL : g_file_get_uri (file->data);

        if (text->len)
            g_string_append_c (text, '\n');
        g_string_append (text, (path) ? path : uri);
    }

    g_output_stream_write_all (gdk_content_serializer_get_output_stream (serializer), text->str, text->len, NULL, NULL, NULL);
    gdk_content_serializer_return_success (serializer);
}

/* A file list put back on the clipboard pastes as text too, as the GDK
 * backend's does: each file's path, a line each, beside the uri list -- offered
 * once, in a host whose GDK serialises a file list to that text itself as much
 * as in gnome-shell, where it knows no text form for one. The first is run in
 * a process of its own, the serializer it registers staying registered. */
static void
publish_uris (Fixture      *f,
              gconstpointer user_data G_GNUC_UNUSED)
{
    if (g_test_subprocess ())
        gdk_content_register_serializer (GDK_TYPE_FILE_LIST, "text/plain;charset=utf-8", serialize_file_list_text, NULL, NULL);

    g_autoptr (GPasteItem) item = g_paste_uris_item_new_from_str ("file:///tmp/a%20b\nfile:///tmp/c");

    g_assert_nonnull (item);
    g_assert_cmpint (g_paste_clipboard_provider_select_item_full (f->clipboard, item, TRUE), ==, G_PASTE_CLIPBOARD_PUBLICATION_PUBLISHED);
    g_assert_cmpuint (offered_times (f, META_SELECTION_CLIPBOARD, "text/plain;charset=utf-8"), ==, 1);

    g_autoptr (GBytes) text = transfer (f, "text/plain;charset=utf-8");
    g_autoptr (GBytes) expected = g_bytes_new_static ("/tmp/a b\n/tmp/c", strlen ("/tmp/a b\n/tmp/c"));
    g_autoptr (GBytes) uris = transfer (f, "text/uri-list");

    g_assert_true (g_bytes_equal (text, expected));
    g_assert_cmpuint (g_bytes_get_size (uris), >, 0);

    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 0, G_TEST_SUBPROCESS_DEFAULT);
        g_test_trap_assert_passed ();
    }
}

/* gnome-shell disposes a daemon it lets go of, and its clipboards with it, but
 * mutter still holds the source they published until something else is
 * copied: that source serves what it was given on its own. */
static void
publish_outlives_clipboard (Fixture      *f,
                            gconstpointer user_data G_GNUC_UNUSED)
{
    select_image (f);
    g_clear_object (&f->manager);
    g_object_run_dispose (G_OBJECT (f->clipboard));
    g_clear_object (&f->clipboard);

    assert_serves_image (f);
}

/* A provider disposed with a read out -- which only an explicit
 * g_object_run_dispose () can find, the read holding the provider until it
 * concludes -- gives that read up: concluding, it would re-own the selection
 * for a trimmed text through a provider left with no selection. */
static void
dispose_mid_read (Fixture      *f,
                  gconstpointer user_data G_GNUC_UNUSED)
{
    const gchar *mimetypes[] = { "text/plain;charset=utf-8", "text/html", NULL };
    g_autoptr (GBytes) text = g_bytes_new_static ("  padded  ", 10);
    g_autoptr (GBytes) html = g_bytes_new_static ("<b>padded</b>", 13);
    GBytes *contents[] = { text, html };
    g_autoptr (MultiSource) source = multi_source_new (mimetypes, contents);

    g_paste_settings_set_trim_items (f->settings, TRUE);
    source->held_mime = "text/html";
    meta_selection_set_owner (f->selection, META_SELECTION_CLIPBOARD, META_SELECTION_SOURCE (source));
    while (g_main_context_iteration (NULL, FALSE));
    g_assert_nonnull (source->held);

    g_object_run_dispose (G_OBJECT (f->clipboard));

    g_autoptr (GTask) held = g_steal_pointer (&source->held);

    g_task_return_pointer (held, g_memory_input_stream_new_from_bytes (html), g_object_unref);
    while (g_main_context_iteration (NULL, FALSE));

    g_assert_cmpuint (g_paste_history_get_length (f->history), ==, 0);
    g_assert_false (offers (f, META_SELECTION_CLIPBOARD, "text/plain"));
}

/* Another owner taking the selection over leaves nothing of what GPaste
 * published alive in gnome-shell: the texture an image went out with -- one
 * read off its file, with no PNG of its own to serve -- goes with mutter's
 * reference on the source. */
static void
publish_let_go (Fixture      *f,
                gconstpointer user_data G_GNUC_UNUSED)
{
    gpointer texture;
    g_autofree gchar *path = g_build_filename (g_get_user_cache_dir (), "let-go.png", NULL);

    {
        g_autoptr (GBytes) png = png_bytes ();
        g_autoptr (GDateTime) date = g_date_time_new_now_local ();

        g_assert_cmpint (g_mkdir_with_parents (g_get_user_cache_dir (), 0700), ==, 0);
        g_assert_true (g_file_set_contents (path, g_bytes_get_data (png, NULL), (gssize) g_bytes_get_size (png), NULL));

        g_autoptr (GPasteItem) item = g_paste_image_item_new_from_file (path, date, NULL);

        g_assert_null (g_paste_image_item_get_png_bytes (G_PASTE_IMAGE_ITEM (item)));

        g_paste_item_set_state (item, G_PASTE_ITEM_STATE_ACTIVE);
        g_assert_cmpint (g_paste_clipboard_provider_select_item_full (f->clipboard, item, TRUE), !=, G_PASTE_CLIPBOARD_PUBLICATION_INVALID);
        texture = g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item));
        g_object_add_weak_pointer (texture, &texture);
    }

    g_assert_nonnull (texture);

    g_autoptr (GBytes) text = g_bytes_new_static ("other", 5);

    offer (f, "text/plain;charset=utf-8", text);
    g_assert_null (texture);
    g_unlink (path);
}

/* An image that carries its PNG goes out as that PNG alone, all gnome-shell
 * serialises a texture to: its texture held by nothing GPaste published, it
 * goes once the history lets it go -- outside max-memory-usage otherwise, for
 * as long as GPaste owns the selection. */
static void
publish_image_holds_no_texture (Fixture      *f,
                                gconstpointer user_data G_GNUC_UNUSED)
{
    gpointer texture;

    {
        g_autoptr (GBytes) png = png_bytes ();
        g_autoptr (GDateTime) date = g_date_time_new_now_local ();
        g_autoptr (GPasteItem) item = g_paste_image_item_new_from_bytes (png, date, NULL);

        g_paste_item_set_state (item, G_PASTE_ITEM_STATE_ACTIVE);
        g_assert_cmpint (g_paste_clipboard_provider_select_item_full (f->clipboard, item, TRUE), ==, G_PASTE_CLIPBOARD_PUBLICATION_PUBLISHED);
        texture = g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item));
        g_object_add_weak_pointer (texture, &texture);
        g_paste_item_set_state (item, G_PASTE_ITEM_STATE_IDLE);
    }

    g_assert_null (texture);
    g_assert_true (offers (f, META_SELECTION_CLIPBOARD, "image/png"));
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    g_test_add ("/meta/capture/image", Fixture, GINT_TO_POINTER (G_PASTE_ITEM_KIND_IMAGE), fixture_setup, capture, fixture_teardown);
    g_test_add ("/meta/capture/color", Fixture, GINT_TO_POINTER (G_PASTE_ITEM_KIND_COLOR), fixture_setup, capture, fixture_teardown);
    g_test_add ("/meta/capture/uris", Fixture, GINT_TO_POINTER (G_PASTE_ITEM_KIND_URIS), fixture_setup, capture, fixture_teardown);
    g_test_add ("/meta/publish/image", Fixture, NULL, fixture_setup, publish_image, fixture_teardown);
    g_test_add ("/meta/publish/outlives-clipboard", Fixture, NULL, fixture_setup, publish_outlives_clipboard, fixture_teardown);
    g_test_add ("/meta/read/disposed", Fixture, NULL, fixture_setup, dispose_mid_read, fixture_teardown);
    g_test_add ("/meta/publish/let-go", Fixture, NULL, fixture_setup, publish_let_go, fixture_teardown);
    g_test_add ("/meta/publish/uris", Fixture, NULL, fixture_setup, publish_uris, fixture_teardown);
    g_test_add ("/meta/publish/image-holds-no-texture", Fixture, NULL, fixture_setup, publish_image_holds_no_texture, fixture_teardown);
    g_test_add ("/meta/serialize/empty-file-list", Fixture, NULL, fixture_setup, serialize_empty_file_list, fixture_teardown);
    g_test_add ("/meta/publish/offers", Fixture, NULL, fixture_setup, publish_offers, fixture_teardown);
    g_test_add ("/meta/publish/wipes-password", Fixture, NULL, fixture_setup, publish_wipes_password, fixture_teardown);
    g_test_add ("/meta/release/head-back", Fixture, NULL, fixture_setup, release_puts_head_back, fixture_teardown);
    g_test_add ("/meta/release/emission-order", Fixture, NULL, fixture_setup, release_emission_order, fixture_teardown);
    g_test_add ("/meta/release/after-text", Fixture, GINT_TO_POINTER (FALSE), fixture_setup, release_after_text, fixture_teardown);
    g_test_add ("/meta/release/after-text-synced", Fixture, GINT_TO_POINTER (TRUE), fixture_setup, release_after_text, fixture_teardown);
    g_test_add ("/meta/read/empty-text", Fixture, NULL, fixture_setup, read_empty_text, fixture_teardown);
    g_test_add ("/meta/read/unhandled-kept", Fixture, NULL, fixture_setup, read_unhandled_kept, fixture_teardown);
    g_test_add ("/meta/sync/delivers", Fixture, NULL, fixture_setup, sync_delivers, fixture_teardown);
    g_test_add ("/meta/read/nul-terminated", Fixture, NULL, fixture_setup, nul_terminated_text, fixture_teardown);
    g_test_add ("/meta/read/bounded", Fixture, NULL, fixture_setup, bounded_read, fixture_teardown);
    g_test_add ("/meta/read/secret-bounded", Fixture, NULL, fixture_setup, secret_bounded, fixture_teardown);
    g_test_add ("/meta/capture/text-beside-image", Fixture, NULL, fixture_setup, text_beside_image, fixture_teardown);
    g_test_add ("/meta/capture/file-list-bounded", Fixture, NULL, fixture_setup, file_list_bounded, fixture_teardown);

    return g_paste_test_env_run ();
}
