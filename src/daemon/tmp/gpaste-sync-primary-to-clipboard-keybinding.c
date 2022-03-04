/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#include <gpaste/gpaste-gsettings-keys.h>

#include <gpaste-sync-primary-to-clipboard-keybinding.h>

struct _GPasteSyncPrimaryToClipboardKeybinding
{
    GPasteKeybinding parent_instance;
};

G_PASTE_DEFINE_TYPE (SyncPrimaryToClipboardKeybinding, sync_primary_to_clipboard_keybinding, G_PASTE_TYPE_KEYBINDING)

static void
g_paste_sync_primary_to_clipboard_keybinding_class_init (GPasteSyncPrimaryToClipboardKeybindingClass *klass G_GNUC_UNUSED)
{
}

static void
g_paste_sync_primary_to_clipboard_keybinding_init (GPasteSyncPrimaryToClipboardKeybinding *self G_GNUC_UNUSED)
{
}

static void
g_paste_sync_primary_to_clipboard_keybinding_sync (GPasteKeybinding *self G_GNUC_UNUSED,
                                                   gpointer          data)
{
    GPasteClipboardsManager *gcm = data;

    g_paste_clipboards_manager_sync_from_to (gcm, FALSE);
}

/**
 * g_paste_sync_primary_to_clipboard_keybinding_new:
 * @gcm: a #GPasteClipboardManager instance
 *
 * Create a new instance of #GPasteSyncPrimaryToClipboardKeybinding
 *
 * Returns: a newly allocated #GPasteSyncPrimaryToClipboardKeybinding
 *          free it with g_object_unref
 */
G_PASTE_VISIBLE GPasteKeybinding *
g_paste_sync_primary_to_clipboard_keybinding_new (GPasteClipboardsManager *gcm)
{
    return g_paste_keybinding_new (G_PASTE_TYPE_SYNC_PRIMARY_TO_CLIPBOARD_KEYBINDING,
                                   G_PASTE_SYNC_PRIMARY_TO_CLIPBOARD_SETTING,
                                   g_paste_settings_get_sync_primary_to_clipboard,
                                   g_paste_sync_primary_to_clipboard_keybinding_sync,
                                   gcm);
}
