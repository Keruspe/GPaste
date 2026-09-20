// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gpaste-daemon/gpaste-clipboard-content.h>

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define G_PASTE_TYPE_CLIPBOARD_X11_HINTS (g_paste_clipboard_x11_hints_get_type ())

G_PASTE_FINAL_TYPE (ClipboardX11Hints, clipboard_x11_hints, CLIPBOARD_X11_HINTS, GObject)

/* What the server says about a selection's owner: three answers and not two,
 * because a server that cannot be asked is not a server saying nobody owns this.
 * The caller acts on %G_PASTE_CLIPBOARD_X11_HINTS_OWNER_NONE by putting the
 * history's head back on the selection, which over a live owner overwrites the
 * copy that owner is publishing, so the two must not share a value
 * (g_paste_clipboard_x11_hints_get_owner ()). */
typedef enum
{
    G_PASTE_CLIPBOARD_X11_HINTS_OWNER_UNKNOWN, /* the display could not be asked */
    G_PASTE_CLIPBOARD_X11_HINTS_OWNER_NONE,    /* the server says nobody owns it */
    G_PASTE_CLIPBOARD_X11_HINTS_OWNER_SOME,    /* the server names an owner */
} GPasteClipboardX11HintsOwner;

gboolean                     g_paste_clipboard_x11_hints_available (GPasteClipboardX11Hints *self);
GPasteClipboardX11HintsOwner g_paste_clipboard_x11_hints_get_owner  (GPasteClipboardX11Hints *self);

void                         g_paste_clipboard_x11_hints_read       (GPasteClipboardX11Hints *self,
                                                                     GPasteClipboardUpdate   *update);

GPasteClipboardX11Hints     *g_paste_clipboard_x11_hints_new        (GdkClipboard            *clipboard,
                                                                     gboolean                 is_clipboard);

G_END_DECLS
