/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gsettings-keys.h>

#include <gpaste-sync-clipboard-to-primary-keybinding.h>

struct _GPasteSyncClipboardToPrimaryKeybinding
{
    GPasteKeybinding parent_instance;
};

G_PASTE_DEFINE_TYPE (SyncClipboardToPrimaryKeybinding, sync_clipboard_to_primary_keybinding, G_PASTE_TYPE_KEYBINDING)

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

    g_paste_clipboards_manager_sync_from_to (gcm, TRUE);
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
    return g_paste_keybinding_new (G_PASTE_TYPE_SYNC_CLIPBOARD_TO_PRIMARY_KEYBINDING,
                                   G_PASTE_SYNC_CLIPBOARD_TO_PRIMARY_SETTING,
                                   g_paste_settings_get_sync_clipboard_to_primary,
                                   g_paste_sync_clipboard_to_primary_keybinding_sync,
                                   gcm);
}
