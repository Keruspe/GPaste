/*
 *      This file is part of GPaste.
 *
 *      Copyright 2011-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

/* GPasteDaemon */
#include <gpaste-daemon.h>
#include <gpaste-gdbus-defines.h>
#include <gpaste-update-enums.h>

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

/* GPasteApplet */
#include <gpaste-applet-about.h>
#include <gpaste-applet-icon.h>
#include <gpaste-applet-menu.h>
#include <gpaste-applet-quit.h>
#include <gpaste-applet-status-icon.h>
#include <gpaste-applet-ui.h>
#include <gpaste-applet.h>

/* GPasteUi */
#include <gpaste-ui-about.h>
#include <gpaste-ui-delete.h>
#include <gpaste-ui-empty.h>
#include <gpaste-ui-empty-item.h>
#include <gpaste-ui-header.h>
#include <gpaste-ui-history.h>
#include <gpaste-ui-item.h>
#include <gpaste-ui-search.h>
#include <gpaste-ui-search-bar.h>
#include <gpaste-ui-settings.h>
#include <gpaste-ui-switch.h>
#include <gpaste-ui-window.h>

#undef __G_PASTE_H_INSIDE__

#endif /*__G_PASTE_H__*/
