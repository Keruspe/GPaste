// SPDX-FileCopyrightText: 2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* The update API the X11 hints workaround (src/daemon/gpaste-clipboard-x11-hints.c)
 * reads through: the sensitive offer read and the two halves of the MIME-reading
 * policy. Kept apart from test-clipboard.c so it goes with the workaround. */

#include "gpaste-test-clipboard-provider.h"

#include <gpaste-test-env.h>
#include <gpaste-daemon/gpaste-password-item.h>

typedef struct
{
    GPasteItem           *item;
    GPasteClipboardSecret secret;
} ReadResult;

static void
capture_read (GPasteClipboardProvider *provider G_GNUC_UNUSED,
              GPasteItem              *item,
              gboolean                 superseded,
              GPasteClipboardSecret    secret,
              gpointer                 user_data)
{
    ReadResult *result = user_data;

    g_assert_false (superseded);
    result->item = item;
    result->secret = secret;
}

static gboolean
offers_everything (gconstpointer offer G_GNUC_UNUSED,
                   const gchar  *mimetype G_GNUC_UNUSED)
{
    return TRUE;
}

static gboolean
offers_nothing (gconstpointer offer G_GNUC_UNUSED,
                const gchar  *mimetype G_GNUC_UNUSED)
{
    return FALSE;
}

static void
keep_hint_read (gpointer                backend,
                const gchar            *mimetype G_GNUC_UNUSED,
                GCancellable           *cancellable G_GNUC_UNUSED,
                GPasteClipboardMimeCtx *ctx)
{
    GPasteClipboardMimeCtx **slot = backend;

    g_assert_null (*slot);
    *slot = ctx;
}

/* A backend that has to ask which hints are on offer: the answer never coming
 * leaves the secret unknown, one without the hint says no, and hints fired from
 * the offer's callback keep the update open until they report. */
static void
test_sensitive_offer_read (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestClipboard) clipboard = test_clipboard_new ();
    ReadResult result = { 0 };

    g_paste_settings_set_min_text_item_size (settings, 1);
    g_paste_settings_set_max_text_item_size (settings, 1000);

    GPasteClipboardUpdate *pending = g_paste_clipboard_update_new (G_PASTE_CLIPBOARD_PROVIDER (clipboard), settings,
                                                                     CLIPBOARD_CONTENT_TEXT, &clipboard->pending,
                                                                     &clipboard->content, capture_read, &result);

    g_assert_true (g_paste_clipboard_update_wants_sensitive_mimes (pending));
    g_paste_clipboard_update_add_read (pending);
    g_paste_clipboard_update_add_sensitive_offer_read (pending);
    test_clipboard_text_ready (pending, "offered text");
    g_paste_clipboard_update_maybe_done (pending);
    g_assert_null (result.item);

    GPasteClipboardMimeCtx *hint = NULL;

    if (variant == 1)
        g_paste_clipboard_update_read_sensitive_mimes (pending, NULL, offers_nothing, &hint, keep_hint_read);
    else if (variant == 2)
        g_paste_clipboard_update_read_sensitive_mimes (pending, NULL, offers_everything, &hint, keep_hint_read);

    g_paste_clipboard_update_on_sensitive_offer_read (pending, variant != 0);

    if (variant == 2)
    {
        g_assert_nonnull (hint);
        g_assert_null (result.item);
        g_assert_false (pending->concluded);
        g_paste_clipboard_update_on_mime_read (hint, g_paste_sensitive_mime_get_bytes (G_PASTE_SENSITIVE_MIME_KDE_PASSWORD_MANAGER_HINT));
    }
    else
        g_assert_null (hint);

    g_autoptr (GPasteItem) item = result.item;
    const GPasteClipboardSecret expected[] = { CLIPBOARD_SECRET_UNKNOWN, CLIPBOARD_SECRET_NO, CLIPBOARD_SECRET_YES };

    g_assert_nonnull (item);
    g_assert_cmpint (result.secret, ==, expected[variant]);
    g_assert_cmpint (G_PASTE_IS_PASSWORD_ITEM (item), ==, variant == 2);
    g_assert_cmpstr (g_paste_item_get_real_value (item), ==, "offered text");
}

typedef struct
{
    guint special;
    guint sensitive;
} MimeReads;

static void
count_mime_read (gpointer                backend,
                 const gchar            *mimetype,
                 GCancellable           *cancellable G_GNUC_UNUSED,
                 GPasteClipboardMimeCtx *ctx)
{
    MimeReads *reads = backend;
    gboolean sensitive = FALSE;

    for (GPasteSensitiveMime mime = G_PASTE_SENSITIVE_MIME_FIRST; mime < G_PASTE_SENSITIVE_MIME_LAST; ++mime)
        sensitive |= g_str_equal (mimetype, g_paste_sensitive_mime_get (mime));

    if (sensitive)
        ++reads->sensitive;
    else
        ++reads->special;

    g_paste_clipboard_update_on_mime_read (ctx, NULL);
}

static void
drop_read (GPasteClipboardProvider *provider G_GNUC_UNUSED,
           GPasteItem              *item,
           gboolean                 superseded G_GNUC_UNUSED,
           GPasteClipboardSecret    secret G_GNUC_UNUSED,
           gpointer                 user_data G_GNUC_UNUSED)
{
    g_clear_object (&item);
}

/* The two halves of the MIME-reading policy, apart and together: special values
 * with a file list or a rich text, hints with every text. */
static void
test_mime_read_policy (gconstpointer user_data)
{
    guint variant = GPOINTER_TO_UINT (user_data);
    gboolean text = variant & 1;
    gboolean rich = variant & 2;
    gboolean split = variant & 4;
    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (TestClipboard) clipboard = test_clipboard_new ();
    MimeReads reads = { 0 };

    g_paste_settings_set_rich_text_support (settings, rich);

    GPasteClipboardUpdate *pending = g_paste_clipboard_update_new (G_PASTE_CLIPBOARD_PROVIDER (clipboard), settings,
                                                                     (text) ? CLIPBOARD_CONTENT_TEXT : CLIPBOARD_CONTENT_FILE_LIST,
                                                                     &clipboard->pending, &clipboard->content, drop_read, NULL);

    g_assert_cmpint (g_paste_clipboard_update_wants_sensitive_mimes (pending), ==, text);

    if (split)
    {
        g_paste_clipboard_update_read_special_mimes (pending, NULL, offers_everything, &reads, count_mime_read);
        g_assert_cmpuint (reads.sensitive, ==, 0);
        g_paste_clipboard_update_read_sensitive_mimes (pending, NULL, offers_everything, &reads, count_mime_read);
    }
    else
        g_paste_clipboard_update_read_mimes (pending, NULL, offers_everything, &reads, count_mime_read);

    g_paste_clipboard_update_maybe_done (pending);

    g_assert_cmpuint (reads.special, ==, (!text || rich) ? G_PASTE_SPECIAL_MIME_LAST - G_PASTE_SPECIAL_MIME_FIRST : 0);
    g_assert_cmpuint (reads.sensitive, ==, (text) ? G_PASTE_SENSITIVE_MIME_LAST - G_PASTE_SENSITIVE_MIME_FIRST : 0);
}

int
main (int argc, char *argv[])
{
    g_paste_test_env_setup (G_PASTE_TEST_ENV_DEFAULT);
    g_test_init (&argc, &argv, NULL);

    g_test_add_data_func ("/clipboard-hints/sensitive_offer/unanswered", GUINT_TO_POINTER (0), test_sensitive_offer_read);
    g_test_add_data_func ("/clipboard-hints/sensitive_offer/no_hint", GUINT_TO_POINTER (1), test_sensitive_offer_read);
    g_test_add_data_func ("/clipboard-hints/sensitive_offer/hint", GUINT_TO_POINTER (2), test_sensitive_offer_read);
    for (guint variant = 0; variant < 8; ++variant)
    {
        g_autofree gchar *path = g_strdup_printf ("/clipboard-hints/mime_read_policy/%s/%s/%s", (variant & 1) ? "text" : "file_list",
                                                  (variant & 2) ? "rich" : "plain", (variant & 4) ? "split" : "joint");

        g_test_add_data_func (path, GUINT_TO_POINTER (variant), test_mime_read_policy);
    }

    return g_paste_test_env_run ();
}
