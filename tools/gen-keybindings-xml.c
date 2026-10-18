// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

/* Write the control center's keybindings file from GPaste's own list, the
 * G_PASTE_FOR_EACH_KEYBINDING macro the shortcuts dialog and the preferences
 * page are built from too: the compiler reads it here as it does there, so
 * every entry it accepts is in the file, whatever its spelling.
 *
 * Built for the machine running the build, it needs GLib alone. The control
 * center reads the descriptions from its own translation domain, so they go in
 * untranslated, as N_ () marks them. */

#define N_(String) (String)

#include <gpaste-3/gpaste-keybindings.h>

#include <stdio.h>
#include <stdlib.h>

static void
append_entry (GString     *xml,
              const gchar *key,
              const gchar *description)
{
    g_autofree gchar *entry = g_markup_printf_escaped ("\t<KeyListEntry name=\"%s\" description=\"%s\"></KeyListEntry>\n", key, description);

    g_string_append (xml, entry);
}

int
main (int   argc,
      char *argv[])
{
    if (argc != 2)
    {
        fprintf (stderr, "usage: %s OUTPUT\n", argv[0]);
        return EXIT_FAILURE;
    }

    g_autoptr (GString) xml = g_string_new ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                            "<!-- Generated from gpaste-keybindings.h by tools/gen-keybindings-xml.c -->\n"
                                            "<KeyListEntries schema=\"" G_PASTE_SETTINGS_NAME "\" group=\"system\" name=\"GPaste\" package=\"GPaste\">\n");
    g_autoptr (GError) error = NULL;

#define ENTRY(key, group, description, portal_label) append_entry (xml, key, description);
    G_PASTE_FOR_EACH_KEYBINDING (ENTRY)
#undef ENTRY

    g_string_append (xml, "</KeyListEntries>\n");

    if (!g_file_set_contents (argv[1], xml->str, (gssize) xml->len, &error))
    {
        fprintf (stderr, "%s\n", error->message);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
