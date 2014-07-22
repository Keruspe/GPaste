/*
 *      This file is part of GPaste.
 *
 *      Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-make-password-keybinding-private.h"

G_DEFINE_TYPE (GPasteMakePasswordKeybinding, g_paste_make_password_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_make_password_keybinding_class_init (GPasteMakePasswordKeybindingClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_make_password_keybinding_init (GPasteMakePasswordKeybinding *self G_GNUC_UNUSED)
{
}

static void
g_paste_make_password_keybinding_make_password (GPasteKeybinding *self G_GNUC_UNUSED,
                                                gpointer          data)
{
    GPasteHistory *history = data;

    g_paste_history_set_password (history, 0, NULL);
}

/**
 * g_paste_make_password_keybinding_new:
 * @history: a #GPasteHistory instance
 *
 * Create a new instance of #GPasteMakePasswordKeybinding
 *
 * Returns: a newly allocated #GPasteMakePasswordKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_make_password_keybinding_new (GPasteHistory *history)
{
    return _g_paste_keybinding_new (G_PASTE_TYPE_MAKE_PASSWORD_KEYBINDING,
                                    G_PASTE_MAKE_PASSWORD_SETTING,
                                    g_paste_settings_get_make_password,
                                    g_paste_make_password_keybinding_make_password,
                                    history);
}
