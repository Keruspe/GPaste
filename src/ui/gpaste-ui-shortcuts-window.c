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
    { N_ ("Add a new password"),          "<primary><shift>n", NULL },
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
 * The global shortcuts are always listed -- they are still the chords GPaste
 * would grab -- under section titles that say whether anything is listening for
 * them. The dialog reflects the settings as of the time it is built, so it is
 * built anew every time, and closed when what it shows changes under it.
 *
 * Returns: (transfer none): a newly created #AdwShortcutsDialog, holding the
 *          floating reference its caller sinks
 */
GtkWidget *
g_paste_ui_shortcuts_window_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwDialog *self = adw_shortcuts_dialog_new ();
    gboolean enabled = g_paste_settings_get_keybindings_enabled (settings);
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

            /* The notice the keybindings-enabled master switch being off
             * deserves is the title of the section its shortcuts are listed
             * under, and it is on every one of them: whichever the reader is
             * looking at is one nothing is listening for. It can be nowhere
             * else -- a section carries one or more items, so a notice of its
             * own would have to be an itemless one, and what that renders as is
             * nobody's contract; an item cannot carry it either, since one with
             * an empty accelerator renders a "No Shortcut" chip next to the
             * text and one with none at all is not rendered.
             * The warning sign is part of the translated string: where it
             * belongs is the translator's call, and an RTL locale wants it at
             * the other end. */
            g_autofree gchar *title = (enabled)
                ? NULL
                /* translators: %s is a group of shortcuts, e.g. "History access" */
                : g_strdup_printf (_("⚠ %s — GPaste is not listening for these"), _(current));

            section = adw_shortcuts_section_new ((enabled) ? _(current) : title);
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
