/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#ifndef __G_PASTE_CLIPBOARD_H__
#define __G_PASTE_CLIPBOARD_H__

#include <gpaste-content-provider.h>
#include <gpaste-history.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARD (g_paste_clipboard_get_type ())

G_PASTE_FINAL_TYPE (Clipboard, clipboard, CLIPBOARD, GObject)

typedef void (*GPasteClipboardTextCallback)  (GPasteClipboard *self,
                                              const gchar     *text,
                                              gpointer         user_data);

typedef void (*GPasteClipboardUrisCallback)  (GPasteClipboard *self,
                                              const GSList    *files,
                                              gpointer         user_data);

typedef void (*GPasteClipboardImageCallback) (GPasteClipboard *self,
                                              GdkTexture      *texture,
                                              gpointer         user_data);

typedef void (*GPasteClipboardUpdateCallack) (GPasteClipboard *clipboard,
                                              GPasteItem      *item,
                                              const gchar     *synchronized_text,
                                              gboolean         something_in_clipboard,
                                              gpointer         user_data);

void          g_paste_clipboard_bootstrap    (GPasteClipboard *self,
                                              GPasteHistory   *history);
void          g_paste_clipboard_update       (GPasteClipboard             *self,
                                              GPasteHistory               *history,
                                              GPasteClipboardUpdateCallack callback,
                                              gpointer                     user_data);
gboolean      g_paste_clipboard_is_clipboard (const GPasteClipboard *self);
GdkClipboard *g_paste_clipboard_get_real     (const GPasteClipboard *self);
const gchar  *g_paste_clipboard_get_text     (const GPasteClipboard *self);
void          g_paste_clipboard_set_text     (GPasteClipboard            *self,
                                              GPasteClipboardTextCallback callback,
                                              gpointer                    user_data);
void          g_paste_clipboard_select_text  (GPasteClipboard *self,
                                              const gchar     *text);
void          g_paste_clipboard_sync_text    (const GPasteClipboard *self,
                                              GPasteClipboard       *other);
void          g_paste_clipboard_clear        (GPasteClipboard *self);
const gchar  *g_paste_clipboard_get_image_checksum (const GPasteClipboard *self);
void          g_paste_clipboard_set_image          (GPasteClipboard             *self,
                                                    GPasteClipboardImageCallback callback,
                                                    gpointer                     user_data);
gboolean      g_paste_clipboard_select_item        (GPasteClipboard *self,
                                                    GPasteItem      *item);
void          g_paste_clipboard_ensure_not_empty   (GPasteClipboard *self,
                                                    GPasteHistory   *history);

GPasteClipboard *g_paste_clipboard_new_clipboard (GPasteSettings        *settings,
                                                  GPasteContentProvider *content_provider);
GPasteClipboard *g_paste_clipboard_new_primary   (GPasteSettings        *settings,
                                                  GPasteContentProvider *content_provider);

G_END_DECLS

#endif /*__G_PASTE_CLIPBOARD_H__*/
