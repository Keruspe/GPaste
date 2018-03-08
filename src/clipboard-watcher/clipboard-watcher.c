/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste-clipboard.h>

#define EXIT_TEST_SKIP 77

static void
on_owner_change (GPasteClipboard *clipboard,
                 GdkEvent        *event G_GNUC_UNUSED,
                 gpointer         user_data)
{
    const gchar *target = user_data;

    g_print ("%s changed: '%s'\n", target, gtk_clipboard_wait_for_text (g_paste_clipboard_get_real (clipboard)));
}

enum
{
    C_CLIPBOARD,
    C_PRIMARY,

    C_LAST_SIGNAL
};

gint
main (gint argc, gchar *argv[])
{
    /* FIXME: remove this once gtk supports clipboard correctly on wayland */
    gdk_set_allowed_backends ("x11");

    G_PASTE_INIT_APPLICATION ("ClipboardWatcher");

    /* Keep the gapplication around */
    g_application_hold (gapp);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteClipboard) clipboard = g_paste_clipboard_new_clipboard (settings);
    g_autoptr (GPasteClipboard) primary = g_paste_clipboard_new_primary (settings);

    guint64 c_signals[C_LAST_SIGNAL] = {
        [C_CLIPBOARD] = g_signal_connect (clipboard,
                                          "owner-change",
                                          G_CALLBACK (on_owner_change),
                                          (gpointer) "CLIPBOARD"),
        [C_PRIMARY]   = g_signal_connect (primary,
                                          "owner-change",
                                          G_CALLBACK (on_owner_change),
                                          (gpointer) "PRIMARY")
    };

    gint exit_code = g_application_run (gapp, argc, argv);

    g_signal_handler_disconnect (clipboard, c_signals[C_CLIPBOARD]);
    g_signal_handler_disconnect (primary,   c_signals[C_PRIMARY]);

    return exit_code;
}
