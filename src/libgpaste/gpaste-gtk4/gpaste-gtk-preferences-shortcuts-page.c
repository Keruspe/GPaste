// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-keybindings.h>

#include <gpaste-gtk4/gpaste-gtk-preferences-group.h>
#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>

/**
 * g_paste_gtk_preferences_shortcuts_page_new:
 * @settings: a #GPasteSettings instance
 *
 * Build the preferences page
 *
 * Returns: (transfer full): a newly allocated #AdwPreferencesPage
 */
AdwPreferencesPage *
g_paste_gtk_preferences_shortcuts_page_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwPreferencesPage *self = ADW_PREFERENCES_PAGE (g_object_new (ADW_TYPE_PREFERENCES_PAGE,
                                                                   "name", "shortcuts",
                                                                   "title", _("Keyboard shortcuts"),
                                                                   "icon-name", "preferences-desktop-keyboard-shortcuts",
                                                                   NULL));

    gsize n = 0;
    const GPasteKeybindingInfo *keybindings = g_paste_keybindings (&n);
    GPasteGtkPreferencesGroup *group = NULL;
    const gchar *current = NULL;

    /* One group per run of shortcuts sharing a group name; the table is already
     * in the order they are shown. */
    for (gsize i = 0; i < n; ++i)
    {
        const GPasteKeybindingInfo *k = &keybindings[i];

        if (!current || !g_paste_str_equal (current, k->group))
        {
            if (group)
                adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));
            current = k->group;
            group = g_paste_gtk_preferences_group_new (_(current));
        }

        g_paste_gtk_preferences_group_add_shortcut_setting (group, _(k->description), k->key, settings);
    }

    if (group)
        adw_preferences_page_add (self, ADW_PREFERENCES_GROUP (group));

    return self;
}
