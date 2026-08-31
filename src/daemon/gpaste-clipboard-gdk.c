// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include "gpaste-clipboard-gdk.h"
#include "gpaste-text-content-provider.h"

#include <gpaste-gtk4/gpaste-gtk-util.h>

#include <gpaste-daemon/gpaste-clipboard-content.h>
#include <gpaste-daemon/gpaste-color-item.h>
#include <gpaste-daemon/gpaste-image-item.h>
#include <gpaste-daemon/gpaste-special-mime.h>
#include <gpaste-daemon/gpaste-text-item.h>
#include <gpaste-daemon/gpaste-uris-item.h>

enum
{
    C_CHANGED,

    C_LAST_SIGNAL
};

struct _GPasteClipboardGdk
{
    GObject parent_instance;

    GdkClipboard          *real;
    gboolean               is_clipboard;
    GPasteSettings        *settings;

    GPasteClipboardContent content;

    gulong                 c_signals[C_LAST_SIGNAL];
};

static void g_paste_clipboard_gdk_provider_iface_init (GPasteClipboardProviderInterface *iface);

G_PASTE_DEFINE_TYPE_WITH_INTERFACE (ClipboardGdk, clipboard_gdk, G_TYPE_OBJECT,
                                    G_PASTE_TYPE_CLIPBOARD_PROVIDER, g_paste_clipboard_gdk_provider_iface_init)

/* @reselect: whether the text read was stripped of something and the selection
 * therefore has to be re-owned with what came out of it. Answered rather than
 * acted on, the re-owning having to wait for the whole update: see update (). */
typedef void (*GPasteClipboardGdkTextCallback)    (GPasteClipboardGdk *self,
                                                   const gchar        *text,
                                                   gboolean            reselect,
                                                   gpointer            user_data);

typedef void (*GPasteClipboardGdkTextureCallback) (GPasteClipboardGdk *self,
                                                   GdkTexture         *texture,
                                                   gpointer            user_data);

static gboolean
g_paste_clipboard_gdk_is_clipboard (GPasteClipboardGdk *self)
{
    return self->is_clipboard;
}

static const gchar *
g_paste_clipboard_gdk_get_text (GPasteClipboardGdk *self)
{
    return g_paste_clipboard_content_get_text (&self->content);
}

static void
g_paste_clipboard_gdk_private_set_text (GPasteClipboardGdk *self,
                                        const gchar        *text)
{
    g_debug ("%s: set text", g_paste_clipboard_provider_target_name (self->is_clipboard));

    g_paste_clipboard_content_set_text (&self->content, text);
}

/* Same, for the callers that already own the string they hand over. */
static void
g_paste_clipboard_gdk_private_set_text_take (GPasteClipboardGdk *self,
                                             gchar              *text)
{
    g_debug ("%s: set text", g_paste_clipboard_provider_target_name (self->is_clipboard));

    g_paste_clipboard_content_set_text_take (&self->content, text);
}

typedef struct
{
    GPasteClipboardGdk            *self; /* ref'd for the duration of the read */
    GPasteClipboardUpdate         *update; /* what the read counts into, and what says it still matters */
    GPasteClipboardGdkTextCallback callback;
} GPasteClipboardGdkTextCallbackData;

static void
g_paste_clipboard_gdk_on_text_ready (GObject      *source_object,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
    g_autofree GPasteClipboardGdkTextCallbackData *data = user_data;
    g_autoptr (GPasteClipboardGdk) self = data->self; /* ref taken in set_text */
    g_autoptr (GError) error = NULL;
    g_autofree gchar *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source_object), res, &error);

    /* Nothing is waiting for this one any more: the update that fired it was
     * concluded by its guard, so what the read brings back can only reach the
     * provider's cache -- where it would dedup, out of every later update, the
     * very text that never made it to the history. Cancelling cannot fail these
     * reads (see update ()), and the cancel is not even what says the deadline
     * is past: a conclusion asks for it last, so the update itself is the only
     * thing that answers for the window in between (see update_is_expired ()). */
    if (g_paste_clipboard_update_is_expired (data->update))
    {
        if (data->callback)
            data->callback (self, NULL, FALSE, data->update);
        return;
    }

    if (!text)
    {
        if (error)
            g_debug ("Failed to read text from clipboard: %s", error->message);
        if (data->callback)
            data->callback (self, NULL, FALSE, data->update);
        return;
    }

    g_autofree gchar *value = NULL;
    gboolean reselect = FALSE;

    switch (g_paste_clipboard_content_classify_text (&self->content, self->settings, self->is_clipboard, text, &value))
    {
    case G_PASTE_CLIPBOARD_TEXT_REJECT:
        if (data->callback)
            data->callback (self, NULL, FALSE, data->update);
        return;
    case G_PASTE_CLIPBOARD_TEXT_RESELECT:
        reselect = TRUE;
        break;
    case G_PASTE_CLIPBOARD_TEXT_SET:
        break;
    }

    g_paste_clipboard_gdk_private_set_text_take (self, g_steal_pointer (&value));

    if (data->callback)
        data->callback (self, self->content.str, reselect, data->update);
}

static void
g_paste_clipboard_gdk_set_text (GPasteClipboardGdk            *self,
                                GPasteClipboardUpdate         *update,
                                GPasteClipboardGdkTextCallback callback)
{
    GPasteClipboardGdkTextCallbackData *data = g_new (GPasteClipboardGdkTextCallbackData, 1);

    /* Hold a ref for the whole read, as the meta backend does: nothing else
     * keeps us alive between the request and its callback. @update needs none:
     * this read is counted into it and an update is freed by the last read to
     * report, so it outlives every one of them by construction. */
    data->self = g_object_ref (self);
    data->update = update;
    data->callback = callback;

    gdk_clipboard_read_text_async (self->real,
                                   update->guard.cancellable,
                                   g_paste_clipboard_gdk_on_text_ready,
                                   data);
}

static void
g_paste_clipboard_gdk_select_text (GPasteClipboardGdk *self,
                                   const gchar        *text)
{
    g_debug ("%s: select text", g_paste_clipboard_provider_target_name (self->is_clipboard));

    /* Avoid cycling twice as setting the content will make the clipboards manager react */
    g_paste_clipboard_gdk_private_set_text (self, text);

    g_autoptr (GdkContentProvider) provider = g_paste_text_content_provider_new (text);

    gdk_clipboard_set_content (self->real, provider);
}

static void
g_paste_clipboard_gdk_sync_ready (GObject      *source_object,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
    g_autoptr (GPasteClipboardGdk) other = user_data; /* ref taken in sync_text */
    g_autoptr (GError) error = NULL;
    g_autofree gchar *text = gdk_clipboard_read_text_finish (GDK_CLIPBOARD (source_object), res, &error);

    if (error)
        g_debug ("Failed to sync clipboard text: %s", error->message);
    else if (text)
        g_paste_clipboard_gdk_select_text (other, text);
}

static void
g_paste_clipboard_gdk_sync_text (GPasteClipboardGdk *self,
                                 GPasteClipboardGdk *other)
{
    /* The target outlives us in practice, but the read is asynchronous: hold a
     * ref on it until the text lands, as the meta backend does. */
    gdk_clipboard_read_text_async (self->real, NULL, g_paste_clipboard_gdk_sync_ready, g_object_ref (other));
}

static void
g_paste_clipboard_gdk_store_async_done (GObject      *source_object,
                                        GAsyncResult *res,
                                        gpointer      user_data G_GNUC_UNUSED)
{
    g_autoptr (GError) error = NULL;

    if (!gdk_clipboard_store_finish (GDK_CLIPBOARD (source_object), res, &error))
        g_warning ("Failed to store clipboard: %s", error->message);
}

static void
g_paste_clipboard_gdk_store (GPasteClipboardGdk *self)
{
    g_debug ("%s: store", g_paste_clipboard_provider_target_name (self->is_clipboard));

    gdk_clipboard_store_async (self->real,
                               G_PRIORITY_DEFAULT,
                               NULL, /* cancellable */
                               g_paste_clipboard_gdk_store_async_done,
                               NULL);
}

static const gchar *
g_paste_clipboard_gdk_get_image_checksum (GPasteClipboardGdk *self)
{
    return g_paste_clipboard_content_get_image_checksum (&self->content);
}

static void
g_paste_clipboard_gdk_private_set_image_checksum (GPasteClipboardGdk *self,
                                                  const gchar        *image_checksum)
{
    g_paste_clipboard_content_set_image_checksum (&self->content, image_checksum);
}

static void
g_paste_clipboard_gdk_private_set_color (GPasteClipboardGdk *self,
                                         const GdkRGBA      *rgba)
{
    g_debug ("%s: set color", g_paste_clipboard_provider_target_name (self->is_clipboard));

    g_paste_clipboard_content_set_color (&self->content, rgba);
}

static void
g_paste_clipboard_gdk_private_set_file_list (GPasteClipboardGdk *self,
                                             GdkFileList        *file_list)
{
    g_debug ("%s: set file list", g_paste_clipboard_provider_target_name (self->is_clipboard));

    g_paste_clipboard_content_set_file_list (&self->content, file_list);
}

static void
g_paste_clipboard_gdk_private_select_texture (GPasteClipboardGdk *self,
                                              GdkTexture         *texture,
                                              const gchar        *checksum)
{
    g_return_if_fail (GDK_IS_TEXTURE (texture));

    g_debug ("%s: select image", g_paste_clipboard_provider_target_name (self->is_clipboard));

    g_paste_clipboard_gdk_private_set_image_checksum (self, checksum);
    gdk_clipboard_set (self->real, GDK_TYPE_TEXTURE, texture);
}

typedef struct
{
    GPasteClipboardGdk               *self; /* ref'd for the duration of the read */
    GPasteClipboardUpdate            *update; /* what the read counts into, and what says it still matters */
    GPasteClipboardGdkTextureCallback callback;
} GPasteClipboardGdkTextureCallbackData;

static void
g_paste_clipboard_gdk_on_texture_ready (GObject      *source_object,
                                        GAsyncResult *res,
                                        gpointer      user_data)
{
    g_autofree GPasteClipboardGdkTextureCallbackData *data = user_data;
    g_autoptr (GPasteClipboardGdk) self = data->self; /* ref taken in set_texture */
    g_autoptr (GError) error = NULL;
    g_autoptr (GdkTexture) texture = gdk_clipboard_read_texture_finish (GDK_CLIPBOARD (source_object), res, &error);

    /* Past the deadline, the cache is the only place this could reach: see
     * on_text_ready (). */
    if (g_paste_clipboard_update_is_expired (data->update))
    {
        if (data->callback)
            data->callback (self, NULL, data->update);
        return;
    }

    if (!texture)
    {
        if (error)
            g_debug ("Failed to read texture from clipboard: %s", error->message);
        if (data->callback)
            data->callback (self, NULL, data->update);
        return;
    }

    g_autofree gchar *checksum = g_paste_image_item_compute_checksum (texture);
    GdkTexture *result = NULL;

    if (self->content.kind == CLIPBOARD_CONTENT_IMAGE && g_paste_str_equal (checksum, self->content.str))
    {
        /* Same image, nothing to do */
    }
    else
    {
        g_paste_clipboard_gdk_private_select_texture (self, texture, checksum);
        result = texture;  /* borrowed from the g_autoptr above */
    }

    if (data->callback)
        data->callback (self, result, data->update);
}

static void
g_paste_clipboard_gdk_set_texture (GPasteClipboardGdk               *self,
                                   GPasteClipboardUpdate            *update,
                                   GPasteClipboardGdkTextureCallback callback)
{
    GPasteClipboardGdkTextureCallbackData *data = g_new (GPasteClipboardGdkTextureCallbackData, 1);

    /* A ref for the whole read, and none on the update (see set_text). */
    data->self = g_object_ref (self);
    data->update = update;
    data->callback = callback;

    gdk_clipboard_read_texture_async (self->real,
                                      update->guard.cancellable,
                                      g_paste_clipboard_gdk_on_texture_ready,
                                      data);
}

typedef void (*GPasteClipboardGdkRGBACallback) (GPasteClipboardGdk *self,
                                               const GdkRGBA       *rgba,
                                               gpointer             user_data);

typedef struct
{
    GPasteClipboardGdk            *self; /* ref'd for the duration of the read */
    GPasteClipboardUpdate         *update; /* what the read counts into, and what says it still matters */
    GPasteClipboardGdkRGBACallback callback;
} GPasteClipboardGdkRGBACallbackData;

static void
g_paste_clipboard_gdk_on_rgba_ready (GObject      *source_object,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
    g_autofree GPasteClipboardGdkRGBACallbackData *data = user_data;
    g_autoptr (GPasteClipboardGdk) self = data->self; /* ref taken in set_color */
    g_autoptr (GError) error = NULL;
    const GValue *value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source_object), res, &error);

    /* Past the deadline, the cache is the only place this could reach: see
     * on_text_ready (). */
    if (g_paste_clipboard_update_is_expired (data->update))
    {
        if (data->callback)
            data->callback (self, NULL, data->update);
        return;
    }

    if (!value)
    {
        if (error)
            g_debug ("Failed to read color from clipboard: %s", error->message);
        if (data->callback)
            data->callback (self, NULL, data->update);
        return;
    }

    const GdkRGBA *rgba = g_value_get_boxed (value);

    if (!rgba)
    {
        if (data->callback)
            data->callback (self, NULL, data->update);
        return;
    }

    if (self->content.kind == CLIPBOARD_CONTENT_COLOR && gdk_rgba_equal (rgba, &self->content.rgba))
    {
        if (data->callback)
            data->callback (self, NULL, data->update);
        return;
    }

    g_paste_clipboard_gdk_private_set_color (self, rgba);

    if (data->callback)
        data->callback (self, &self->content.rgba, data->update);
}

static void
g_paste_clipboard_gdk_set_color (GPasteClipboardGdk            *self,
                                 GPasteClipboardUpdate         *update,
                                 GPasteClipboardGdkRGBACallback callback)
{
    GPasteClipboardGdkRGBACallbackData *data = g_new (GPasteClipboardGdkRGBACallbackData, 1);

    /* A ref for the whole read, and none on the update (see set_text). */
    data->self = g_object_ref (self);
    data->update = update;
    data->callback = callback;

    gdk_clipboard_read_value_async (self->real,
                                    GDK_TYPE_RGBA,
                                    G_PRIORITY_DEFAULT,
                                    update->guard.cancellable,
                                    g_paste_clipboard_gdk_on_rgba_ready,
                                    data);
}

typedef void (*GPasteClipboardGdkBytesCallback) (GPasteClipboardGdk *self,
                                                 GBytes             *bytes,
                                                 gpointer            user_data);

typedef struct
{
    GPasteClipboardGdk             *self; /* ref'd for the duration of the read */
    GCancellable                   *cancellable; /* ref'd: the second half goes out on it */
    GOutputStream                  *ostream; /* what the transfer is drained into */
    GPasteClipboardGdkBytesCallback callback;
    gpointer                        user_data;
} GPasteClipboardGdkBytesData;

static void
g_paste_clipboard_gdk_bytes_data_free (GPasteClipboardGdkBytesData *data)
{
    g_clear_object (&data->self);
    g_clear_object (&data->cancellable);
    g_clear_object (&data->ostream);
    g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GPasteClipboardGdkBytesData, g_paste_clipboard_gdk_bytes_data_free)

static void
g_paste_clipboard_gdk_on_mime_bytes_ready (GObject      *source_object,
                                           GAsyncResult *res,
                                           gpointer      user_data)
{
    g_autoptr (GPasteClipboardGdkBytesData) data = user_data; /* built in fetch_mime */
    g_autoptr (GError) error = NULL;

    if (g_output_stream_splice_finish (G_OUTPUT_STREAM (source_object), res, &error) < 0)
    {
        if (error)
            g_debug ("Failed to read mime bytes: %s", error->message);
        data->callback (data->self, NULL, data->user_data);
        return;
    }

    /* steal_as_bytes requires a closed stream, which the splice above did
     * (%G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET). */
    g_autoptr (GBytes) bytes = g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (data->ostream));

    data->callback (data->self, bytes, data->user_data);
}

static void
g_paste_clipboard_gdk_on_mime_stream_ready (GObject      *source_object,
                                            GAsyncResult *res,
                                            gpointer      user_data)
{
    /* Released here unless the splice below takes @data over. */
    g_autoptr (GPasteClipboardGdkBytesData) data = user_data; /* built in fetch_mime */
    g_autoptr (GError) error = NULL;
    const gchar *actual_mime = NULL;
    g_autoptr (GInputStream) stream = gdk_clipboard_read_finish (GDK_CLIPBOARD (source_object), res, &actual_mime, &error);

    if (error || !stream)
    {
        if (error)
            g_debug ("Failed to read mime stream: %s", error->message);
        data->callback (data->self, NULL, data->user_data);
        return;
    }

    /* Held here rather than read off @data in the call below: the steal that
     * hands @data to the callback writes %NULL over it, and nothing orders that
     * after the two arguments beside it -- gcc evaluates the last one first, so
     * both of those would be read off a pointer already cleared. */
    GOutputStream *ostream = data->ostream;
    GCancellable *cancellable = data->cancellable;

    /* Spliced rather than read once: a #GdkX11SelectionInputStream hands back
     * whatever a single property already holds, which for an INCR transfer is
     * the first chunk of it -- and a representation truncated there is the one
     * that would be stored on the item and re-published on every later paste.
     * Draining it into a memory stream is what the mutter backend's transfer
     * does, so both backends end up with the whole of what the owner sent. */
    g_output_stream_splice_async (ostream,
                                  stream,
                                  G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                  G_PRIORITY_DEFAULT,
                                  cancellable,
                                  g_paste_clipboard_gdk_on_mime_bytes_ready,
                                  g_steal_pointer (&data));
}

/* Read one named mimetype off the selection, whatever it is being read for: the
 * mimetype is all this needs, so the caller carries what it means in @user_data.
 * @callback is required, and both halves report through it on every path, error
 * ones above all: it is what counts the read out of the update that fired it, so
 * one that went out without it would leave that update unable to conclude.
 *
 * gdk_clipboard_read_async() resolves to a single stream (the first of the
 * requested mimetypes the owner provides), so distinct mimetypes cannot be
 * collapsed into one read; update() already fires these reads in parallel. */
static void
g_paste_clipboard_gdk_fetch_mime (GPasteClipboardGdk             *self,
                                  const gchar                    *mimetype,
                                  GCancellable                   *cancellable,
                                  GPasteClipboardGdkBytesCallback callback,
                                  gpointer                        user_data)
{
    GPasteClipboardGdkBytesData *data = g_new0 (GPasteClipboardGdkBytesData, 1);

    /* Refs for the whole read (see set_text), across both of its halves: the
     * second one goes out on the cancellable the first was fired with, and what
     * keeps that alive is nothing this read can see. */
    data->self = g_object_ref (self);
    data->cancellable = g_object_ref (cancellable);
    data->ostream = g_memory_output_stream_new_resizable ();
    data->callback = callback;
    data->user_data = user_data;

    const gchar *mime_types[] = { mimetype, NULL };

    gdk_clipboard_read_async (self->real,
                              mime_types,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              g_paste_clipboard_gdk_on_mime_stream_ready,
                              data);
}

/* Every read below counts into the same #GPasteClipboardUpdate: what it holds,
 * what concluding it means and what releases it are the mutter backend's too,
 * and live in gpaste-clipboard-content.c. Only the calls that fetch the bytes
 * are this backend's. */

static void
g_paste_clipboard_gdk_update_on_file_list_ready (GObject      *source_object,
                                                 GAsyncResult *res,
                                                 gpointer      user_data)
{
    GPasteClipboardUpdate *update = user_data;
    g_autoptr (GError) error = NULL;
    const GValue *value = gdk_clipboard_read_value_finish (GDK_CLIPBOARD (source_object), res, &error);

    /* Past the deadline, the cache is the only place this could reach: see
     * on_text_ready (). Asked before the provider is read off @update, a
     * concluded one having handed it on. */
    if (g_paste_clipboard_update_is_expired (update))
    {
        g_paste_clipboard_update_maybe_done (update);
        return;
    }

    GPasteClipboardGdk *self = G_PASTE_CLIPBOARD_GDK (update->provider);

    if (!value)
    {
        if (error)
            g_debug ("Failed to read file list from clipboard: %s", error->message);
        g_paste_clipboard_update_maybe_done (update);
        return;
    }

    GdkFileList *file_list = g_value_get_boxed (value);
    /* (transfer container): the container is ours, the GFiles are not. */
    g_autoptr (GSList) files = (file_list) ? gdk_file_list_get_files (file_list) : NULL;

    if (!files)
    {
        g_paste_clipboard_update_maybe_done (update);
        return;
    }

    if (g_paste_clipboard_file_list_equal (g_paste_clipboard_content_get_file_list (&self->content), file_list))
    {
        g_paste_clipboard_update_maybe_done (update);
        return;
    }

    g_paste_clipboard_gdk_private_set_file_list (self, file_list);

    update->produced = TRUE;
    update->file_list = g_boxed_copy (GDK_TYPE_FILE_LIST, file_list);

    g_paste_clipboard_update_maybe_done (update);
}

static void
g_paste_clipboard_gdk_fetch_file_list (GPasteClipboardGdk    *self,
                                       GPasteClipboardUpdate *update)
{
    gdk_clipboard_read_value_async (self->real,
                                    GDK_TYPE_FILE_LIST,
                                    G_PRIORITY_DEFAULT,
                                    update->guard.cancellable,
                                    g_paste_clipboard_gdk_update_on_file_list_ready,
                                    update);
}

static void
g_paste_clipboard_gdk_update_on_text_ready (GPasteClipboardGdk *self G_GNUC_UNUSED,
                                            const gchar        *text,
                                            gboolean            reselect,
                                            gpointer            user_data)
{
    GPasteClipboardUpdate *update = user_data;

    update->produced = !!text;
    g_set_str (&update->text, text);
    update->reselect = reselect;
    g_paste_clipboard_update_maybe_done (update);
}

static void
g_paste_clipboard_gdk_update_on_texture_ready (GPasteClipboardGdk *self G_GNUC_UNUSED,
                                               GdkTexture         *texture,
                                               gpointer            user_data)
{
    GPasteClipboardUpdate *update = user_data;

    update->produced = !!texture;
    g_set_object (&update->texture, texture);
    g_paste_clipboard_update_maybe_done (update);
}

static void
g_paste_clipboard_gdk_update_on_color_ready (GPasteClipboardGdk *self G_GNUC_UNUSED,
                                             const GdkRGBA      *rgba,
                                             gpointer            user_data)
{
    GPasteClipboardUpdate *update = user_data;

    if (rgba)
    {
        update->produced = TRUE;
        update->rgba = *rgba;
    }

    g_paste_clipboard_update_maybe_done (update);
}

/* What a mime read means and what it does to the update it counts into are both
 * gpaste-clipboard-content.c's; this is here for the shape fetch_mime () calls
 * its callback with. */
static void
g_paste_clipboard_gdk_on_mime_read (GPasteClipboardGdk *self G_GNUC_UNUSED,
                                    GBytes             *bytes,
                                    gpointer            user_data)
{
    g_paste_clipboard_update_on_mime_read (user_data, bytes);
}

static void
g_paste_clipboard_gdk_fetch_mime_for (GPasteClipboardGdk    *self,
                                      GPasteClipboardUpdate *update,
                                      const gchar           *mimetype,
                                      GPasteSpecialMime      mime)
{
    GPasteClipboardMimeCtx *ctx = g_paste_clipboard_update_add_mime_read (update, mime);

    g_paste_clipboard_gdk_fetch_mime (self, mimetype, update->guard.cancellable, g_paste_clipboard_gdk_on_mime_read, ctx);
}

/* The guard concluding an update rather than failing its reads is not a choice:
 * cancelling cannot fail them. gdk_x11_clipboard_read_async () and the selection
 * input stream behind it hand the cancellable to their #GTask and never connect
 * to it, so an owner that stopped answering leaves the read exactly where it was
 * whatever we ask of it -- and the update would never conclude, which is the
 * whole of what that guard is for. The cancel is still worth asking for: a read
 * that does honour it fails and counts itself out.
 *
 * What that leaves is the update outliving its own conclusion, freed only if
 * those reads ever land. Nothing can be done about the struct itself -- they
 * hold the pointer -- but everything of any size is let go of at the conclusion
 * instead (g_paste_clipboard_update_release_content ()), so what a stuck owner
 * leaves behind is a counter and not the content that was read, nor the ref that
 * keeps our requestor window on the server. */
static void
g_paste_clipboard_gdk_update (GPasteClipboardGdk                   *self,
                              GPasteClipboardProviderUpdateCallback callback,
                              gpointer                              user_data)
{
    GdkContentFormats *formats = gdk_clipboard_get_formats (self->real);
    GPasteClipboardContentKind content_kind = CLIPBOARD_CONTENT_NONE;
    if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_FILE_LIST))
        content_kind = CLIPBOARD_CONTENT_FILE_LIST;
    else if (gdk_content_formats_contain_gtype (formats, GDK_TYPE_RGBA))
        content_kind = CLIPBOARD_CONTENT_COLOR;
    else if (g_paste_settings_get_images_support (self->settings) &&
             gdk_content_formats_contain_gtype (formats, GDK_TYPE_TEXTURE))
        content_kind = CLIPBOARD_CONTENT_IMAGE;
    else if (gdk_content_formats_contain_gtype (formats, G_TYPE_STRING))
        content_kind = CLIPBOARD_CONTENT_TEXT;
    else if (gdk_content_formats_is_empty (formats))
    {
        /* The selection was released: clear our cache so callers see an
         * empty clipboard and act accordingly (e.g. ensure_not_empty). */
        g_paste_clipboard_content_clear (&self->content);
        if (callback)
            callback (G_PASTE_CLIPBOARD_PROVIDER (self), NULL, user_data);
        return;
    }
    else
    {
        /* The owner only provides types we don't handle (e.g. an image
         * while images-support is disabled). Don't track it, but flag the
         * clipboard as non-empty so ensure_not_empty doesn't override it. */
        g_paste_clipboard_content_clear (&self->content);
        self->content.kind = CLIPBOARD_CONTENT_IGNORED;
        if (callback)
            callback (G_PASTE_CLIPBOARD_PROVIDER (self), NULL, user_data);
        return;
    }

    GPasteClipboardUpdate *update = g_paste_clipboard_update_new (G_PASTE_CLIPBOARD_PROVIDER (self),
                                                                  content_kind,
                                                                  callback,
                                                                  user_data);

    /* Nothing here can have failed its preconditions, but the caller's own
     * bookkeeping is released by the callback and by nothing else, so the one
     * path that fires no read still answers. */
    if (!update)
    {
        if (callback)
            callback (G_PASTE_CLIPBOARD_PROVIDER (self), NULL, user_data);
        return;
    }

    gboolean mime_available[G_PASTE_SPECIAL_MIME_LAST] = { FALSE };

    if (content_kind == CLIPBOARD_CONTENT_FILE_LIST ||
        (content_kind == CLIPBOARD_CONTENT_TEXT && g_paste_settings_get_rich_text_support (self->settings)))
    {
        for (GPasteSpecialMime mime = G_PASTE_SPECIAL_MIME_FIRST; mime < G_PASTE_SPECIAL_MIME_LAST; ++mime)
        {
            if (gdk_content_formats_contain_mime_type (formats, g_paste_special_mime_get (mime)))
                mime_available[mime] = TRUE;
        }
    }

    /* Counted in beside the read it counts, never before the switch: an arm that
     * fires nothing would leave the update pending on a read that does not
     * exist. */
    switch (content_kind)
    {
    case CLIPBOARD_CONTENT_FILE_LIST:
        g_paste_clipboard_update_add_read (update);
        g_paste_clipboard_gdk_fetch_file_list (self, update);
        break;
    case CLIPBOARD_CONTENT_COLOR:
        g_paste_clipboard_update_add_read (update);
        g_paste_clipboard_gdk_set_color (self, update, g_paste_clipboard_gdk_update_on_color_ready);
        break;
    case CLIPBOARD_CONTENT_TEXT:
        g_paste_clipboard_update_add_read (update);
        g_paste_clipboard_gdk_set_text (self, update, g_paste_clipboard_gdk_update_on_text_ready);
        break;
    case CLIPBOARD_CONTENT_IMAGE:
        g_paste_clipboard_update_add_read (update);
        g_paste_clipboard_gdk_set_texture (self, update, g_paste_clipboard_gdk_update_on_texture_ready);
        break;
    case CLIPBOARD_CONTENT_IGNORED:
    case CLIPBOARD_CONTENT_NONE:
        g_assert_not_reached ();
    }

    for (GPasteSpecialMime mime = G_PASTE_SPECIAL_MIME_FIRST; mime < G_PASTE_SPECIAL_MIME_LAST; ++mime)
    {
        if (mime_available[mime])
            g_paste_clipboard_gdk_fetch_mime_for (self, update, g_paste_special_mime_get (mime), mime);
    }

    g_paste_clipboard_update_maybe_done (update);
}

static gboolean
g_paste_clipboard_gdk_select_item (GPasteClipboardGdk *self,
                                   GPasteItem         *item)
{
    g_debug ("%s: select item", g_paste_clipboard_provider_target_name (self->is_clipboard));

    if (G_PASTE_IS_IMAGE_ITEM (item))
    {
        GdkTexture *texture = g_paste_image_item_get_image (G_PASTE_IMAGE_ITEM (item));
        const gchar *checksum = g_paste_image_item_get_checksum (G_PASTE_IMAGE_ITEM (item));

        if (!texture)
            return FALSE;

        g_paste_clipboard_gdk_private_select_texture (self, texture, checksum);
        return TRUE;
    }

    if (G_PASTE_IS_COLOR_ITEM (item))
    {
        const GdkRGBA *rgba = g_paste_color_item_get_rgba (G_PASTE_COLOR_ITEM (item));

        g_paste_clipboard_gdk_private_set_color (self, rgba);

        /* Offer the colour itself plus its textual form, so it can be pasted both
         * into colour-aware apps (application/x-color) and into plain text fields. */
        GdkContentProvider *providers[] = {
            gdk_content_provider_new_typed (GDK_TYPE_RGBA, rgba),
            g_paste_text_content_provider_new (g_paste_item_get_real_value (item)),
        };
        g_autoptr (GdkContentProvider) provider = gdk_content_provider_new_union (providers, G_N_ELEMENTS (providers));

        gdk_clipboard_set_content (self->real, provider);
        return TRUE;
    }

    g_autoptr (GPtrArray) providers = g_ptr_array_new ();

    if (G_PASTE_IS_URIS_ITEM (item))
    {
        GdkFileList *file_list = g_paste_uris_item_get_file_list (G_PASTE_URIS_ITEM (item));
        g_paste_clipboard_gdk_private_set_file_list (self, file_list);
        g_ptr_array_add (providers, gdk_content_provider_new_typed (GDK_TYPE_FILE_LIST, file_list));
    }
    else
    {
        const gchar *real_value = g_paste_item_get_real_value (item);
        g_paste_clipboard_gdk_private_set_text (self, real_value);
        g_ptr_array_add (providers, g_paste_text_content_provider_new (real_value));
    }

    for (const GSList *sv = g_paste_item_get_special_values (item); sv; sv = sv->next)
    {
        GPasteBinaryData *v = sv->data;
        g_ptr_array_add (providers, gdk_content_provider_new_for_bytes (g_paste_special_mime_get (g_paste_binary_data_get_mime (v)), g_paste_binary_data_get_bytes (v)));
    }

    g_autoptr (GdkContentProvider) provider = NULL;
    if (providers->len == 1)
        provider = g_ptr_array_index (providers, 0);
    else
        provider = gdk_content_provider_new_union ((GdkContentProvider **) providers->pdata, providers->len);

    gdk_clipboard_set_content (self->real, provider);

    return TRUE;
}

static gboolean
g_paste_clipboard_gdk_is_empty (GPasteClipboardGdk *self)
{
    return g_paste_clipboard_content_is_empty (&self->content);
}

static void
g_paste_clipboard_gdk_on_real_changed (GPasteClipboardGdk *self)
{
    /* GdkClipboard::changed fires for our own writes too; skip them rather than
     * re-process the content we just published. */
    if (gdk_clipboard_is_local (self->real))
        return;

    /* GTK4 fires changed twice per external selection event: once immediately
     * with empty formats (before TARGETS resolves) and once with the real
     * format list after TARGETS have been fetched. Only process the latter. */
    if (gdk_content_formats_is_empty (gdk_clipboard_get_formats (self->real)))
        return;

    g_debug ("%s: owner change", g_paste_clipboard_provider_target_name (self->is_clipboard));
    g_paste_clipboard_provider_emit_changed (G_PASTE_CLIPBOARD_PROVIDER (self));
}

/* GPasteClipboardProvider interface adapters */
G_PASTE_CLIPBOARD_PROVIDER_DEFINE_VFUNCS (gdk, GDK)

static void
g_paste_clipboard_gdk_dispose (GObject *object)
{
    GPasteClipboardGdk *self = G_PASTE_CLIPBOARD_GDK (object);

    if (self->settings)
    {
        g_signal_handler_disconnect (self->real, self->c_signals[C_CHANGED]);
        g_clear_object (&self->real);
        g_clear_object (&self->settings);
    }

    G_OBJECT_CLASS (g_paste_clipboard_gdk_parent_class)->dispose (object);
}

static void
g_paste_clipboard_gdk_finalize (GObject *object)
{
    GPasteClipboardGdk *self = G_PASTE_CLIPBOARD_GDK (object);

    g_paste_clipboard_content_clear (&self->content);

    G_OBJECT_CLASS (g_paste_clipboard_gdk_parent_class)->finalize (object);
}

static void
g_paste_clipboard_gdk_class_init (GPasteClipboardGdkClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = g_paste_clipboard_gdk_dispose;
    object_class->finalize = g_paste_clipboard_gdk_finalize;
}

static void
g_paste_clipboard_gdk_init (GPasteClipboardGdk *self G_GNUC_UNUSED)
{
}

static GPasteClipboardProvider *
_g_paste_clipboard_gdk_new (GPasteSettings *settings,
                            gboolean        is_clipboard)
{
    GPasteClipboardGdk *self = g_object_new (G_PASTE_TYPE_CLIPBOARD_GDK, NULL);

    GdkDisplay *display = gdk_display_get_default ();
    self->real = g_object_ref (is_clipboard ? gdk_display_get_clipboard (display)
                                            : gdk_display_get_primary_clipboard (display));
    self->is_clipboard = is_clipboard;
    self->settings = g_object_ref (settings);

    self->c_signals[C_CHANGED] = g_signal_connect_swapped (self->real,
                                                           "changed",
                                                           G_CALLBACK (g_paste_clipboard_gdk_on_real_changed),
                                                           self);

    return G_PASTE_CLIPBOARD_PROVIDER (self);
}

/**
 * g_paste_clipboard_gdk_new_clipboard:
 * @settings: a #GPasteSettings instance
 *
 * Create a new GDK-backed #GPasteClipboardProvider for the clipboard
 *
 * Returns: (transfer full): a newly allocated #GPasteClipboardProvider
 *          free it with g_object_unref
 */
GPasteClipboardProvider *
g_paste_clipboard_gdk_new_clipboard (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_gdk_new (settings, TRUE);
}

/**
 * g_paste_clipboard_gdk_new_primary:
 * @settings: a #GPasteSettings instance
 *
 * Create a new GDK-backed #GPasteClipboardProvider for the primary selection
 *
 * Returns: (transfer full): a newly allocated #GPasteClipboardProvider
 *          free it with g_object_unref
 */
GPasteClipboardProvider *
g_paste_clipboard_gdk_new_primary (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    return _g_paste_clipboard_gdk_new (settings, FALSE);
}
