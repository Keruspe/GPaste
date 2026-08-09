// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-gtk4/gpaste-gtk-preferences-pages.h>

/**
 * g_paste_gtk_preferences_pages_new:
 * @settings: a #GPasteSettings instance
 *
 * Build every preferences page, in the order they are shown.
 *
 * Returns: (transfer container) (array zero-terminated=1): the pages
 */
AdwPreferencesPage **
g_paste_gtk_preferences_pages_new (GPasteSettings *settings)
{
    g_return_val_if_fail (G_PASTE_IS_SETTINGS (settings), NULL);

    AdwPreferencesPage **pages = g_new (AdwPreferencesPage *, 5);

    pages[0] = g_paste_gtk_preferences_behaviour_page_new (settings);
    pages[1] = g_paste_gtk_preferences_history_settings_page_new (settings);
    pages[2] = g_paste_gtk_preferences_images_page_new (settings);
    pages[3] = g_paste_gtk_preferences_shortcuts_page_new (settings);
    pages[4] = NULL;

    return pages;
}
