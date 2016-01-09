/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gpaste-clipboard.h>

static void
on_owner_change (GPasteClipboard *clipboard,
                 GdkEvent        *event G_GNUC_UNUSED,
                 gpointer         user_data)
{
    const gchar *target = user_data;

    g_print ("%s changed: '%s'\n", target, gtk_clipboard_wait_for_text (g_paste_clipboard_get_real (clipboard)));
}

gint
main (gint argc, gchar *argv[])
{
    /* FIXME: remove this once gtk supports clipboard correctly on wayland */
    gdk_set_allowed_backends ("x11");

    G_PASTE_INIT_APPLICATION ("ClipboardWatcher");

    /* Keep the gapplication around */
    g_application_hold (gapp);

    g_autoptr (GPasteSettings) settings = g_paste_settings_new ();
    g_autoptr (GPasteClipboard) clipboard = g_paste_clipboard_new (GDK_SELECTION_CLIPBOARD, settings);
    g_autoptr (GPasteClipboard) primary = g_paste_clipboard_new (GDK_SELECTION_PRIMARY, settings);

    gulong cs = g_signal_connect (clipboard,
                                  "owner-change",
                                  G_CALLBACK (on_owner_change),
                                  (gpointer) "CLIPBOARD");
    gulong ps = g_signal_connect (primary,
                                  "owner-change",
                                  G_CALLBACK (on_owner_change),
                                  (gpointer) "PRIMARY");

    gint exit_code = g_application_run (gapp, argc, argv);

    g_signal_handler_disconnect (clipboard, cs);
    g_signal_handler_disconnect (primary, ps);

    return exit_code;
}
