// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-keybindings.h>

#include <gpaste-ui-shortcuts-window.h>

/* The window's own shortcuts, which are not the daemon's.
 *
 * G_PASTE_FOR_EACH_KEYBINDING is the one place the *global* shortcuts are
 * written down, because three things have to agree on them: this dialog, the
 * preferences, and the control center's 42-gpaste.xml. These are none of that
 * -- they work only while this window has the focus, they are not configurable,
 * and the desktop portal has no business listing them -- so they are written
 * down here, beside the window that answers them. */
static const struct
{
    const gchar *description;
    /* AdwShortcutLabel parses each part of this and warns about anything it
     * cannot, so no range syntax: what a range would have said goes in the
     * subtitle instead. */
    const gchar *accelerator;
    const gchar *subtitle;
} window_shortcuts[] = {
    { N_ ("Search the history"),          "<primary>f",        NULL },
    { N_ ("Add a new item"),              "<primary>n",        NULL },
    { N_ ("Paste the item at an index"),  "<primary>0",        N_ ("Ctrl+0 through Ctrl+9, for the first ten items") },
    { N_ ("Show the preferences"),        "<primary>comma",    NULL },
    { N_ ("Show the keyboard shortcuts"), "<primary>question", NULL },
    { N_ ("Close the window"),            "<primary>w Escape", NULL },
};

static void
add_window_section (AdwShortcutsDialog *self)
{
    AdwShortcutsSection *section = adw_shortcuts_section_new (_("This window"));

    for (gsize i = 0; i < G_N_ELEMENTS (window_shortcuts); ++i)
    {
        AdwShortcutsItem *item = adw_shortcuts_item_new (_(window_shortcuts[i].description),
                                                         window_shortcuts[i].accelerator);

        if (window_shortcuts[i].subtitle)
            adw_shortcuts_item_set_subtitle (item, _(window_shortcuts[i].subtitle));

        adw_shortcuts_section_add (section, item);
    }

    adw_shortcuts_dialog_add (self, section);
}

/**
 * g_paste_ui_shortcuts_window_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new #AdwShortcutsDialog for GPaste
 *
 * Returns: a newly allocated #AdwShortcutsDialog
 *          free it with g_object_unref
 */
GtkWidget *
g_paste_ui_shortcuts_window_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwDialog *self = adw_shortcuts_dialog_new ();
    gsize n = 0;
    const GPasteKeybindingInfo *keybindings = g_paste_keybindings (&n);
    AdwShortcutsSection *section = NULL;
    const gchar *current = NULL;

    for (gsize i = 0; i < n; ++i)
    {
        const GPasteKeybindingInfo *k = &keybindings[i];

        if (!current || !g_paste_str_equal (current, k->group))
        {
            if (section)
                adw_shortcuts_dialog_add (ADW_SHORTCUTS_DIALOG (self), section);
            current = k->group;
            section = adw_shortcuts_section_new (_(current));
        }

        /* Every setting is a property named exactly like its key, so the
         * accelerator comes straight off @settings. */
        g_autofree gchar *accelerator = NULL;
        g_object_get (settings, k->key, &accelerator, NULL);

        adw_shortcuts_section_add (section, adw_shortcuts_item_new (_(k->description), accelerator));
    }

    if (section)
        adw_shortcuts_dialog_add (ADW_SHORTCUTS_DIALOG (self), section);

    add_window_section (ADW_SHORTCUTS_DIALOG (self));

    return GTK_WIDGET (self);
}
