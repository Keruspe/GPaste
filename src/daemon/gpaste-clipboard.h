// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-history.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARD (g_paste_clipboard_get_type ())

G_PASTE_FINAL_TYPE (Clipboard, clipboard, CLIPBOARD, GObject)

typedef enum
{
    CLIPBOARD_CONTENT_NONE,
    CLIPBOARD_CONTENT_TEXT,
    CLIPBOARD_CONTENT_IMAGE,
    CLIPBOARD_CONTENT_FILE_LIST,
    CLIPBOARD_CONTENT_COLOR,
    /* The selection has an owner but only offers types we don't handle
     * (e.g. an image while images-support is disabled): not tracked, but
     * must not be overridden by ensure_not_empty either. */
    CLIPBOARD_CONTENT_IGNORED,
} GPasteClipboardContent;

/**
 * GPasteClipboardUpdateCallback:
 * @self: the #GPasteClipboard that was updated
 * @item: (transfer full) (nullable): what the clipboard held, owned by the callback
 * @user_data: the data passed to g_paste_clipboard_update()
 *
 * Called once a clipboard update has been read back. The callback owns @item:
 * pass it on to something that takes ownership (g_paste_history_add()) or
 * release it.
 */
typedef void (*GPasteClipboardUpdateCallback) (GPasteClipboard *self,
                                               GPasteItem      *item,
                                               gpointer         user_data);

GPasteClipboardContent g_paste_clipboard_content_for_formats (GdkContentFormats *formats,
                                                              gboolean           images_support);

void          g_paste_clipboard_update             (GPasteClipboard              *self,
                                                    GPasteClipboardUpdateCallback callback,
                                                    gpointer                      user_data);
gboolean      g_paste_clipboard_is_clipboard (const GPasteClipboard *self);
const gchar  *g_paste_clipboard_get_text     (const GPasteClipboard *self);
void          g_paste_clipboard_select_text  (GPasteClipboard *self,
                                              const gchar     *text);
void          g_paste_clipboard_sync_text    (const GPasteClipboard *self,
                                              GPasteClipboard       *other);
void          g_paste_clipboard_clear        (GPasteClipboard *self);
void          g_paste_clipboard_store        (GPasteClipboard *self);
const gchar  *g_paste_clipboard_get_image_checksum (const GPasteClipboard *self);
gboolean      g_paste_clipboard_select_item        (GPasteClipboard *self,
                                                    GPasteItem      *item);
void          g_paste_clipboard_ensure_not_empty   (GPasteClipboard *self,
                                                    GPasteHistory   *history);

GPasteClipboard *g_paste_clipboard_new_clipboard (GPasteSettings *settings);
GPasteClipboard *g_paste_clipboard_new_primary   (GPasteSettings *settings);

G_END_DECLS
