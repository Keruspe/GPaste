// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The mutter backend, against a real MetaSelection outside gnome-shell and,
 * like gnome-shell, with GTK never initialised: whatever it reads or publishes
 * beyond text has to go through formats GDK knows without gtk_init (). */

#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-clipboard-meta.h>
#include <gpaste-daemon/gpaste-clipboards-manager.h>
#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

#include <meta/meta-selection-source-memory.h>

#include <string.h>

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

    GList      *mimetypes;
    GHashTable *contents;
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
    g_autoptr (GTask) task = g_task_new (source, cancellable, callback, user_data);
    GBytes *bytes = g_hash_table_lookup (MULTI_SOURCE (source)->contents, mimetype);

    if (bytes)
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
static void
offer_formats (Fixture            *f,
               const gchar * const *mimetypes,
               GBytes             **contents)
{
    g_autoptr (MultiSource) source = g_object_new (multi_source_get_type (), NULL);

    for (guint i = 0; mimetypes[i]; ++i)
    {
        source->mimetypes = g_list_append (source->mimetypes, g_strdup (mimetypes[i]));
        g_hash_table_insert (source->contents, g_strdup (mimetypes[i]), g_bytes_ref (contents[i]));
    }
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

/* An image put back on the clipboard is offered as a PNG, which reads back as
 * the same image. */
static void
publish_image (Fixture      *f,
               gconstpointer user_data G_GNUC_UNUSED)
{
    g_autoptr (GBytes) png = png_bytes ();
    Transfer transfer = { 0 };
    g_autoptr (GDateTime) date = g_date_time_new_now_local ();
    g_autoptr (GPasteItem) item = g_paste_image_item_new_from_bytes (png, date, NULL);

    /* Active, as the history's head is when it is published. */
    g_assert_nonnull (item);
    g_paste_item_set_state (item, G_PASTE_ITEM_STATE_ACTIVE);
    g_assert_true (g_paste_clipboard_provider_select_item (f->clipboard, item));

    GList *mimetypes = meta_selection_get_mimetypes (f->selection, META_SELECTION_CLIPBOARD);
    gboolean offered = FALSE;

    for (const GList *m = mimetypes; m; m = m->next)
        offered |= g_str_equal (m->data, "image/png");
    g_list_free_full (mimetypes, g_free);
    g_assert_true (offered);

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

    g_autoptr (GBytes) served = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (stream));
    g_autoptr (GdkTexture) texture = gdk_texture_new_from_bytes (served, NULL);

    g_assert_nonnull (texture);
    g_assert_cmpint (gdk_texture_get_width (texture), ==, 2);
    g_assert_cmpint (gdk_texture_get_height (texture), ==, 1);
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
    g_test_add ("/meta/read/nul-terminated", Fixture, NULL, fixture_setup, nul_terminated_text, fixture_teardown);
    g_test_add ("/meta/read/bounded", Fixture, NULL, fixture_setup, bounded_read, fixture_teardown);
    g_test_add ("/meta/read/secret-bounded", Fixture, NULL, fixture_setup, secret_bounded, fixture_teardown);
    g_test_add ("/meta/capture/text-beside-image", Fixture, NULL, fixture_setup, text_beside_image, fixture_teardown);
    g_test_add ("/meta/capture/file-list-bounded", Fixture, NULL, fixture_setup, file_list_bounded, fixture_teardown);

    return g_paste_test_env_run ();
}
