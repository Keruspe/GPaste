// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#if !defined (__G_PASTE_H_INSIDE__) && !defined (G_PASTE_COMPILATION)
#error "Only <gpaste.h> can be included directly."
#endif

#pragma once

#include <gpaste-3/gpaste-gsettings-keys.h>
#include <gpaste-3/gpaste-macros.h>

G_BEGIN_DECLS

/* The seven keyboard shortcuts, their grouping and what they do.
 *
 * This list is the one place they are written down. It feeds the shortcuts
 * dialog, the shortcuts page of the preferences, and -- through
 * tools/gen-keybindings-xml.py, which reads this very macro -- the control
 * center's 42-gpaste.xml, so none of the three can drift apart from the others
 * in wording, order or grouping.
 *
 * Each carries two names: the description the shortcut UIs show, and the short
 * label the desktop portal lists it under, where there is only room for a few
 * words. The strings are N_(): the table is static, so they are marked here and
 * translated at the point of use. The key doubles as the GSettings key and as
 * the GPasteSettings property name, which is what lets a consumer read the
 * accelerator with g_object_get() instead of a per-key getter. */
#define G_PASTE_FOR_EACH_KEYBINDING(K)                                                                                                                                    \
    K (G_PASTE_LAUNCH_UI_SETTING, N_ ("History access"), N_ ("Launch the graphical tool"), N_ ("Launch UI"))                                                              \
    K (G_PASTE_SHOW_HISTORY_SETTING, N_ ("History access"), N_ ("Display the history"), N_ ("Show History"))                                                              \
    K (G_PASTE_MAKE_PASSWORD_SETTING, N_ ("Active element manipulation"), N_ ("Mark the active item as being a password"), N_ ("Convert to Password"))                    \
    K (G_PASTE_UPLOAD_SETTING, N_ ("Active element manipulation"), N_ ("Upload the active item to a pastebin service"), N_ ("Upload to Pastebin"))                        \
    K (G_PASTE_POP_SETTING, N_ ("Active element manipulation"), N_ ("Delete the active item from history"), N_ ("Pop from History"))                                      \
    K (G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING, N_ ("Clipboard synchronization"), N_ ("Sync the clipboard to the primary selection"), N_ ("Sync Clipboard to Primary")) \
    K (G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING, N_ ("Clipboard synchronization"), N_ ("Sync the primary selection to the clipboard"), N_ ("Sync Primary to Clipboard"))

typedef struct
{
    /* The GSettings key, which is also the GPasteSettings property name. */
    const gchar *key;
    /* Untranslated; pass through gettext at the point of use. */
    const gchar *group;
    const gchar *description;
    /* What the desktop portal shows in its shortcut list: the same action, said
     * in the few words that fit there. */
    const gchar *portal_label;
} GPasteKeybindingInfo;

const GPasteKeybindingInfo *g_paste_keybindings (gsize *n_keybindings);

G_END_DECLS
