/*
 *      This file is part of GPaste.
 *
 *      Copyright 2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

#include "gpaste-sync-clipboard-to-primary-keybinding-private.h"

G_DEFINE_TYPE (GPasteSyncClipboardToPrimaryKeybinding, g_paste_sync_clipboard_to_primary_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_sync_clipboard_to_primary_keybinding_class_init (GPasteSyncClipboardToPrimaryKeybindingClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_sync_clipboard_to_primary_keybinding_init (GPasteSyncClipboardToPrimaryKeybinding *self G_GNUC_UNUSED)
{
}

static void
g_paste_sync_clipboard_to_primary_keybinding_sync (GPasteKeybinding *self G_GNUC_UNUSED,
                                                   gpointer          data)
{
    GPasteClipboardsManager *gcm = data;

    g_paste_clipboards_manager_sync_from_to (gcm,
                                             GDK_SELECTION_CLIPBOARD,
                                             GDK_SELECTION_PRIMARY);
}

/**
 * g_paste_sync_clipboard_to_primary_keybinding_new:
 * @gcm: a #GPasteClipboardManager instance
 *
 * Create a new instance of #GPasteSyncClipboardToPrimaryKeybinding
 *
 * Returns: a newly allocated #GPasteSyncClipboardToPrimaryKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_sync_clipboard_to_primary_keybinding_new (GPasteClipboardsManager *gcm)
{
    return _g_paste_keybinding_new (G_PASTE_TYPE_SYNC_CLIPBOARD_TO_PRIMARY_KEYBINDING,
                                    G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING,
                                    g_paste_settings_get_sync_clipboard_to_primary,
                                    g_paste_sync_clipboard_to_primary_keybinding_sync,
                                    gcm);
}
