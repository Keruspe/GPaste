/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-keybinding-private.h"

#include <gpaste-gsettings-keys.h>
#include <gpaste-ui-keybinding.h>
#include <gpaste-util.h>

struct _GPasteUiKeybinding
{
    GPasteKeybinding parent_instance;
};

G_DEFINE_TYPE (GPasteUiKeybinding, g_paste_ui_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_ui_keybinding_class_init (GPasteUiKeybindingClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_ui_keybinding_init (GPasteUiKeybinding *self G_GNUC_UNUSED)
{
}

static void
launch_ui (GPasteKeybinding *self G_GNUC_UNUSED,
           gpointer          data G_GNUC_UNUSED)
{
    g_paste_util_spawn ("Ui");
}

/**
 * g_paste_ui_keybinding_new:
 *
 * Create a new instance of #GPasteUiKeybinding
 *
 * Returns: a newly allocated #GPasteUiKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_ui_keybinding_new (void)
{
    return _g_paste_keybinding_new (G_PASTE_TYPE_UI_KEYBINDING,
                                    G_PASTE_LAUNCH_UI_SETTING,
                                    g_paste_settings_get_launch_ui,
                                    launch_ui,
                                    NULL);
}
