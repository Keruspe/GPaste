# This file is part of GPaste.
#
# Copyright 2014-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
#
# GPaste is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# GPaste is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GPaste.  If not, see <http://www.gnu.org/licenses/>.

libgpaste_la_file = lib/libgpaste.la

lib_libgpaste_la_private_headers = \
	src/libgpaste/gpaste-gdbus-macros.h \
	src/libgpaste/applet/gpaste-applet-icon-private.h \
	src/libgpaste/core/gpaste-item-private.h \
	src/libgpaste/keybinder/gpaste-keybinding-private.h \
	src/libgpaste/ui/gpaste-ui-history-action-private.h \
	$(NULL)

lib_libgpaste_la_misc_headers = \
	src/libgpaste/gpaste-gdbus-defines.h  \
	src/libgpaste/gpaste-gsettings-keys.h \
	src/libgpaste/gpaste-macros.h \
	src/libgpaste/util/gpaste-util.h \
	$(NULL)

lib_libgpaste_la_public_headers = \
	src/libgpaste/gpaste.h \
	src/libgpaste/applet/gpaste-applet.h \
	src/libgpaste/applet/gpaste-applet-about.h \
	src/libgpaste/applet/gpaste-applet-icon.h \
	src/libgpaste/applet/gpaste-applet-menu.h \
	src/libgpaste/applet/gpaste-applet-quit.h \
	src/libgpaste/applet/gpaste-applet-status-icon.h \
	src/libgpaste/applet/gpaste-applet-ui.h \
	src/libgpaste/client/gpaste-client.h \
	src/libgpaste/core/gpaste-clipboard.h \
	src/libgpaste/core/gpaste-clipboards-manager.h \
	src/libgpaste/core/gpaste-history.h \
	src/libgpaste/core/gpaste-image-item.h \
	src/libgpaste/core/gpaste-item.h \
	src/libgpaste/core/gpaste-password-item.h \
	src/libgpaste/core/gpaste-text-item.h \
	src/libgpaste/core/gpaste-item-enums.h \
	src/libgpaste/core/gpaste-update-enums.h \
	src/libgpaste/core/gpaste-uris-item.h \
	src/libgpaste/daemon/gpaste-bus.h \
	src/libgpaste/daemon/gpaste-bus-object.h \
	src/libgpaste/daemon/gpaste-daemon.h \
	src/libgpaste/daemon/gpaste-search-provider.h \
	src/libgpaste/gnome-shell-client/gpaste-gnome-shell-client.h \
	src/libgpaste/keybinder/gpaste-keybinder.h \
	src/libgpaste/keybinder/gpaste-keybinding.h \
	src/libgpaste/keybinder/gpaste-make-password-keybinding.h \
	src/libgpaste/keybinder/gpaste-pop-keybinding.h \
	src/libgpaste/keybinder/gpaste-show-history-keybinding.h \
	src/libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding.h \
	src/libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding.h \
	src/libgpaste/keybinder/gpaste-ui-keybinding.h \
	src/libgpaste/keybinder/gpaste-upload-keybinding.h \
	src/libgpaste/screensaver-client/gpaste-screensaver-client.h \
	src/libgpaste/settings/gpaste-settings.h \
	src/libgpaste/settings-ui/gpaste-settings-ui-panel.h \
	src/libgpaste/settings-ui/gpaste-settings-ui-stack.h \
	src/libgpaste/settings-ui/gpaste-settings-ui-widget.h \
	src/libgpaste/ui/gpaste-ui-about.h \
	src/libgpaste/ui/gpaste-ui-backup-history.h \
	src/libgpaste/ui/gpaste-ui-delete.h \
	src/libgpaste/ui/gpaste-ui-delete-history.h \
	src/libgpaste/ui/gpaste-ui-edit.h \
	src/libgpaste/ui/gpaste-ui-empty.h \
	src/libgpaste/ui/gpaste-ui-empty-item.h \
	src/libgpaste/ui/gpaste-ui-header.h \
	src/libgpaste/ui/gpaste-ui-history.h \
	src/libgpaste/ui/gpaste-ui-history-action.h \
	src/libgpaste/ui/gpaste-ui-history-actions.h \
	src/libgpaste/ui/gpaste-ui-item.h \
	src/libgpaste/ui/gpaste-ui-panel.h \
	src/libgpaste/ui/gpaste-ui-panel-history.h \
	src/libgpaste/ui/gpaste-ui-reexec.h \
	src/libgpaste/ui/gpaste-ui-search.h \
	src/libgpaste/ui/gpaste-ui-search-bar.h \
	src/libgpaste/ui/gpaste-ui-settings.h \
	src/libgpaste/ui/gpaste-ui-switch.h \
	src/libgpaste/ui/gpaste-ui-window.h \
	$(NULL)

lib_libgpaste_la_source_files = \
	src/libgpaste/applet/gpaste-applet.c \
	src/libgpaste/applet/gpaste-applet-about.c \
	src/libgpaste/applet/gpaste-applet-icon.c \
	src/libgpaste/applet/gpaste-applet-menu.c \
	src/libgpaste/applet/gpaste-applet-quit.c \
	src/libgpaste/applet/gpaste-applet-status-icon.c \
	src/libgpaste/applet/gpaste-applet-ui.c \
	src/libgpaste/client/gpaste-client.c \
	src/libgpaste/core/gpaste-clipboard.c \
	src/libgpaste/core/gpaste-clipboards-manager.c \
	src/libgpaste/core/gpaste-history.c \
	src/libgpaste/core/gpaste-image-item.c \
	src/libgpaste/core/gpaste-item.c \
	src/libgpaste/core/gpaste-password-item.c \
	src/libgpaste/core/gpaste-text-item.c \
	src/libgpaste/core/gpaste-item-enums.c \
	src/libgpaste/core/gpaste-update-enums.c \
	src/libgpaste/core/gpaste-uris-item.c \
	src/libgpaste/daemon/gpaste-bus.c \
	src/libgpaste/daemon/gpaste-bus-object.c \
	src/libgpaste/daemon/gpaste-daemon.c \
	src/libgpaste/daemon/gpaste-search-provider.c \
	src/libgpaste/gnome-shell-client/gpaste-gnome-shell-client.c \
	src/libgpaste/keybinder/gpaste-keybinder.c \
	src/libgpaste/keybinder/gpaste-keybinding.c \
	src/libgpaste/keybinder/gpaste-make-password-keybinding.c \
	src/libgpaste/keybinder/gpaste-pop-keybinding.c \
	src/libgpaste/keybinder/gpaste-show-history-keybinding.c \
	src/libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding.c \
	src/libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding.c \
	src/libgpaste/keybinder/gpaste-ui-keybinding.c \
	src/libgpaste/keybinder/gpaste-upload-keybinding.c \
	src/libgpaste/screensaver-client/gpaste-screensaver-client.c \
	src/libgpaste/settings/gpaste-settings.c \
	src/libgpaste/settings-ui/gpaste-settings-ui-panel.c \
	src/libgpaste/settings-ui/gpaste-settings-ui-stack.c \
	src/libgpaste/settings-ui/gpaste-settings-ui-widget.c \
	src/libgpaste/ui/gpaste-ui-about.c \
	src/libgpaste/ui/gpaste-ui-backup-history.c \
	src/libgpaste/ui/gpaste-ui-delete.c \
	src/libgpaste/ui/gpaste-ui-delete-history.c \
	src/libgpaste/ui/gpaste-ui-edit.c \
	src/libgpaste/ui/gpaste-ui-empty.c \
	src/libgpaste/ui/gpaste-ui-empty-item.c \
	src/libgpaste/ui/gpaste-ui-header.c \
	src/libgpaste/ui/gpaste-ui-history.c \
	src/libgpaste/ui/gpaste-ui-history-action.c \
	src/libgpaste/ui/gpaste-ui-history-actions.c \
	src/libgpaste/ui/gpaste-ui-item.c \
	src/libgpaste/ui/gpaste-ui-panel.c \
	src/libgpaste/ui/gpaste-ui-panel-history.c \
	src/libgpaste/ui/gpaste-ui-reexec.c \
	src/libgpaste/ui/gpaste-ui-search.c \
	src/libgpaste/ui/gpaste-ui-search-bar.c \
	src/libgpaste/ui/gpaste-ui-settings.c \
	src/libgpaste/ui/gpaste-ui-switch.c \
	src/libgpaste/ui/gpaste-ui-window.c \
	src/libgpaste/util/gpaste-util.c \
	$(NULL)

if ENABLE_UNITY
lib_libgpaste_la_public_headers += \
	src/libgpaste/applet/gpaste-applet-app-indicator.h \
	$(NULL)
lib_libgpaste_la_source_files += \
	src/libgpaste/applet/gpaste-applet-app-indicator.c \
	$(NULL)
endif

lib_libgpaste_la_SOURCES = \
	$(lib_libgpaste_la_misc_headers) \
	$(lib_libgpaste_la_public_headers) \
	$(lib_libgpaste_la_private_headers) \
	$(lib_libgpaste_la_source_files) \
	$(NULL)

lib_libgpaste_la_CFLAGS = \
	$(GDK_CFLAGS) \
	$(GDK_PIXBUF_CFLAGS) \
	$(UNITY_CFLAGS) \
	$(X11_CFLAGS) \
	$(AM_CFLAGS) \
	$(NULL)

lib_libgpaste_la_LIBADD = \
	$(GDK_LIBS) \
	$(GDK_PIXBUF_LIBS) \
	$(GTK_LIBS) \
	$(UNITY_LIBS) \
	$(X11_LIBS) \
	$(AM_LIBS) \
	$(NULL)

libgpaste_symbols_file = $(srcdir)/src/libgpaste/libgpaste.sym

lib_libgpaste_la_LDFLAGS = \
	-version-info $(LIBGPASTE_LT_VERSION) \
	-Wl,--version-script=$(libgpaste_symbols_file) \
	$(NULL)

lib_libgpaste_la_DEPENDENCIES = \
	$(libgpaste_symbols_file) \
	$(NULL)

pkginclude_HEADERS += \
	$(lib_libgpaste_la_misc_headers) \
	$(lib_libgpaste_la_public_headers) \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_la_file) \
	$(NULL)

EXTRA_DIST += \
	$(libgpaste_symbols_file) \
	$(NULL)
