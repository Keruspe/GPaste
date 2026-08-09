// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#include <gpaste-3/gpaste-keybindings.h>

#define KEYBINDING(key, group, description) { key, group, description },
static const GPasteKeybindingInfo keybindings[] = {
    G_PASTE_FOR_EACH_KEYBINDING (KEYBINDING)
};
#undef KEYBINDING

/**
 * g_paste_keybindings:
 * @n_keybindings: (out): how many there are
 *
 * Get the keyboard shortcuts GPaste defines, in the order they should be shown.
 * The strings are untranslated; run them through gettext before displaying them.
 *
 * Returns: (array length=n_keybindings) (transfer none): a read-only array
 */
G_PASTE_VISIBLE const GPasteKeybindingInfo *
g_paste_keybindings (gsize *n_keybindings)
{
    g_return_val_if_fail (n_keybindings, NULL);

    *n_keybindings = G_N_ELEMENTS (keybindings);

    return keybindings;
}
