// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The least a #GPasteClipboardUpdate needs of a provider: a cache to commit to
 * and a slot to stand in. Shared by the suites that drive updates without a
 * backend -- the update API the X11 hints workaround reads through
 * (test-clipboard-hints.c), and the workaround itself against a real server
 * (test-clipboard-x11-hints.c). */

#pragma once

#include <gpaste-daemon/gpaste-clipboard-content.h>

G_BEGIN_DECLS

#define TEST_TYPE_CLIPBOARD (test_clipboard_get_type ())

G_DECLARE_FINAL_TYPE (TestClipboard, test_clipboard, TEST, CLIPBOARD, GObject)

struct _TestClipboard
{
    GObject parent_instance;

    GPasteClipboardContent content;
    GPasteClipboardUpdate *pending;
};

TestClipboard *test_clipboard_new        (void);

/* The text read reporting, as a backend's would: @pending keeps the value and
 * is asked whether that was the last read it was waiting for. */
void           test_clipboard_text_ready (GPasteClipboardUpdate *pending,
                                          const gchar           *text);

G_END_DECLS
