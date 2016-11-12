/*
 * This file is part of GPaste.
 *
 * Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 */

#ifndef __G_PASTE_H__
#define __G_PASTE_H__

#define __G_PASTE_H_INSIDE__

/* Misc. macros */
#include <gpaste-macros.h>

/* Core GPaste Components */
#include <gpaste-clipboard.h>
#include <gpaste-clipboards-manager.h>
#include <gpaste-history.h>
#include <gpaste-image-item.h>
#include <gpaste-item.h>
#include <gpaste-password-item.h>
#include <gpaste-text-item.h>
#include <gpaste-uris-item.h>

/* GPasteIO */
#include <gpaste-storage-backend.h>
#include <gpaste-file-backend.h>

/* GPasteUtil */
#include <gpaste-util.h>

/* GPasteKeybinder */
#include <gpaste-keybinder.h>
#include <gpaste-keybinding.h>
#include <gpaste-make-password-keybinding.h>
#include <gpaste-pop-keybinding.h>
#include <gpaste-show-history-keybinding.h>
#include <gpaste-sync-clipboard-to-primary-keybinding.h>
#include <gpaste-sync-primary-to-clipboard-keybinding.h>
#include <gpaste-ui-keybinding.h>
#include <gpaste-upload-keybinding.h>

/* GPasteBus */
#include <gpaste-bus.h>
#include <gpaste-gdbus-defines.h>
#include <gpaste-update-enums.h>

/* GPasteBusObject */
#include <gpaste-bus-object.h>

/* GPasteDaemon */
#include <gpaste-daemon.h>

/* GPasteSearchProvider */
#include <gpaste-search-provider.h>

/* GPasteClient */
#include <gpaste-client.h>

/* GPasteGnomeShellClient */
#include <gpaste-gnome-shell-client.h>

/* GPasteScreensaverClient */
#include <gpaste-screensaver-client.h>

/* GPasteSettings */
#include <gpaste-gsettings-keys.h>
#include <gpaste-settings.h>

/* GPasteSettingsUi */
#include <gpaste-settings-ui-panel.h>
#include <gpaste-settings-ui-stack.h>
#include <gpaste-settings-ui-widget.h>

/* GPasteUi */
#include <gpaste-ui-about.h>
#include <gpaste-ui-backup-history.h>
#include <gpaste-ui-delete-item.h>
#include <gpaste-ui-delete-history.h>
#include <gpaste-ui-edit-item.h>
#include <gpaste-ui-empty-history.h>
#include <gpaste-ui-empty-item.h>
#include <gpaste-ui-header.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-history-action.h>
#include <gpaste-ui-history-actions.h>
#include <gpaste-ui-item.h>
#include <gpaste-ui-item-action.h>
#include <gpaste-ui-item-skeleton.h>
#include <gpaste-ui-panel.h>
#include <gpaste-ui-panel-history.h>
#include <gpaste-ui-reexec.h>
#include <gpaste-ui-search.h>
#include <gpaste-ui-search-bar.h>
#include <gpaste-ui-settings.h>
#include <gpaste-ui-shortcuts-window.h>
#include <gpaste-ui-switch.h>
#include <gpaste-ui-upload-item.h>
#include <gpaste-ui-window.h>

#undef __G_PASTE_H_INSIDE__

#endif /*__G_PASTE_H__*/
