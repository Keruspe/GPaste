// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gdk/gdk.h>

#include <gpaste-3/gpaste-settings.h>

#include <gpaste-daemon/gpaste-item.h>
#include <gpaste-daemon/gpaste-special-atom.h>

G_BEGIN_DECLS

/* The single piece of content a clipboard provider currently holds, shared by
 * the GDK and mutter backends so the kind enum and its tagged value live in one
 * place. Only the field matching @kind is live at any time: prefer the get_*
 * accessors below, which check @kind, and check it by hand for the fields that
 * have none — reading the wrong member reinterprets unrelated bytes (a string
 * pointer as a GdkFileList *, an RGBA's floats as a pointer, …). */
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
} GPasteClipboardContentKind;

typedef struct
{
    GPasteClipboardContentKind kind;
    union {
        gchar       *str;       /* TEXT: the text; IMAGE: the image checksum */
        GdkFileList *file_list; /* FILE_LIST */
        GdkRGBA      rgba;      /* COLOR */
    };
} GPasteClipboardContent;

void         g_paste_clipboard_content_clear              (GPasteClipboardContent       *content);
gboolean     g_paste_clipboard_content_is_empty           (const GPasteClipboardContent *content);
const gchar *g_paste_clipboard_content_get_text           (const GPasteClipboardContent *content);
const gchar *g_paste_clipboard_content_get_image_checksum (const GPasteClipboardContent *content);
GdkFileList *g_paste_clipboard_content_get_file_list      (const GPasteClipboardContent *content);

void         g_paste_clipboard_content_set_text           (GPasteClipboardContent       *content,
                                                          const gchar                  *text);
void         g_paste_clipboard_content_set_text_take      (GPasteClipboardContent       *content,
                                                          gchar                        *text);
void         g_paste_clipboard_content_set_image_checksum (GPasteClipboardContent       *content,
                                                          const gchar                  *checksum);
void         g_paste_clipboard_content_set_image_checksum_take (GPasteClipboardContent  *content,
                                                                gchar                   *checksum);
void         g_paste_clipboard_content_set_color          (GPasteClipboardContent       *content,
                                                          const GdkRGBA                *rgba);
void         g_paste_clipboard_content_set_file_list      (GPasteClipboardContent       *content,
                                                          GdkFileList                  *file_list);

/* What a backend should do with a candidate clipboard text, as decided by
 * g_paste_clipboard_content_classify_text() from the trim/size/dedup policy. */
typedef enum
{
    G_PASTE_CLIPBOARD_TEXT_REJECT,   /* too short/long, or unchanged: drop it */
    G_PASTE_CLIPBOARD_TEXT_SET,      /* cache @out_value as the new text */
    G_PASTE_CLIPBOARD_TEXT_RESELECT, /* re-own the selection with the stripped @out_value */
} GPasteClipboardTextAction;

GPasteClipboardTextAction g_paste_clipboard_content_classify_text (const GPasteClipboardContent *content,
                                                                  GPasteSettings         *settings,
                                                                  gboolean                      is_clipboard,
                                                                  const gchar                  *text,
                                                                  gchar                       **out_value);

gboolean     g_paste_clipboard_file_list_equal (GdkFileList *a,
                                                GdkFileList *b);

/* Turn a finished clipboard read into the item it describes. Only the argument
 * matching @kind is looked at, and %NULL is answered for a kind that produced
 * nothing -- including NONE and IGNORED, so a backend that decided not to
 * produce anything just passes those.
 *
 * @special_atoms is the G_PASTE_SPECIAL_ATOM_LAST-long array of alternative
 * representations gathered alongside; the ones that end up on the item are
 * stolen from it, and the rest are left for the caller to release. */
GPasteItem *g_paste_clipboard_content_to_item (GPasteClipboardContentKind kind,
                                               const gchar               *text,
                                               GdkTexture                *texture,
                                               GdkFileList               *file_list,
                                               const GdkRGBA             *rgba,
                                               GPasteBinaryData         **special_atoms);

G_END_DECLS
