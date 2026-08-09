// SPDX-FileCopyrightText: 2010-2026 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <gdk/gdk.h>

G_BEGIN_DECLS

/**
 * g_paste_keyval_canonicalize:
 * @keyval: a key value
 *
 * Reduce @keyval to the primary spelling of its alias group.
 *
 * Keypad keys and their main-row twins are synonymous to GTK, but only one
 * spelling ends up in the accelerator string we store and hand to the portal —
 * so pressing keypad 1 would otherwise grab KP_1 alone. Capturing a shortcut
 * and translating one into a portal trigger have to canonicalise *identically*,
 * or the accelerator in GSettings and the trigger given to the portal name
 * different keys; that is the bug this exists to prevent, so the rule lives in
 * one place even though its two callers are in different libraries.
 *
 * Inline in a header rather than a function in one of them because neither
 * library can reach the other: libgpaste-daemon deliberately does not link
 * libgpaste-gtk4, and libgpaste itself is toolkit-free and must stay that way.
 *
 * Returns: the primary key value of @keyval's alias group
 */
static inline guint
g_paste_keyval_canonicalize (guint keyval)
{
    guint n_aliases = 0;
    /* The array includes @keyval itself, primary spelling first. */
    const guint *aliases = gdk_keyval_get_aliases (keyval, &n_aliases);

    return n_aliases ? aliases[0] : keyval;
}

G_END_DECLS
