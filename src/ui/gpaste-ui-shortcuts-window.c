// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-keybindings.h>

#include <gpaste-ui-shortcuts-window.h>

/**
 * g_paste_ui_shortcuts_window_new:
 * @settings: a #GPasteSettings instance
 *
 * Create a new #AdwShortcutsDialog for GPaste
 *
 * Returns: a newly allocated #AdwShortcutsDialog
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GtkWidget *
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

    return GTK_WIDGET (self);
}
