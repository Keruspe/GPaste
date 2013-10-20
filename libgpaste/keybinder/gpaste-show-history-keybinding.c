/*
 *      This file is part of GPaste.
 *
 *      Copyright 2012-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-show-history-keybinding-private.h"

G_DEFINE_TYPE (GPasteShowHistoryKeybinding, g_paste_show_history_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_show_history_keybinding_class_init (GPasteShowHistoryKeybindingClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_show_history_keybinding_init (GPasteShowHistoryKeybinding *self G_GNUC_UNUSED)
{
}

static void
g_paste_show_history_keybinding_show_history (GPasteKeybinding *self G_GNUC_UNUSED,
                                              gpointer          data)
{
    GPasteDaemon *gpaste_daemon = data;

    g_paste_daemon_show_history (gpaste_daemon,
                                 NULL); /* error */
}

/**
 * g_paste_show_history_keybinding_new:
 * @gpaste_daemon: a #GPasteDaemon instance
 *
 * Create a new instance of #GPasteShowHistoryKeybinding
 *
 * Returns: a newly allocated #GPasteShowHistoryKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_show_history_keybinding_new (GPasteDaemon     *gpaste_daemon)
{
    return _g_paste_keybinding_new (G_PASTE_TYPE_SHOW_HISTORY_KEYBINDING,
                                    G_PASTE_SHOW_HISTORY_SETTING,
                                    g_paste_settings_get_show_history,
                                    g_paste_show_history_keybinding_show_history,
                                    gpaste_daemon);
}
