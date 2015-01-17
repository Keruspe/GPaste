# This file is part of GPaste.
#
# Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

LIBGPASTE_CURRENT=2
LIBGPASTE_REVISION=2
LIBGPASTE_AGE=0

lib_libgpaste_la_private_headers = \
	src/libgpaste/gpaste-gdbus-macros.h \
	src/libgpaste/applet/gpaste-applet-private.h \
	src/libgpaste/applet/gpaste-applet-about-private.h \
	src/libgpaste/applet/gpaste-applet-delete-private.h \
	src/libgpaste/applet/gpaste-applet-empty-private.h \
	src/libgpaste/applet/gpaste-applet-footer-private.h \
	src/libgpaste/applet/gpaste-applet-header-private.h \
	src/libgpaste/applet/gpaste-applet-history-private.h \
	src/libgpaste/applet/gpaste-applet-icon-private.h \
	src/libgpaste/applet/gpaste-applet-item-private.h \
	src/libgpaste/applet/gpaste-applet-menu-private.h \
	src/libgpaste/applet/gpaste-applet-quit-private.h \
	src/libgpaste/applet/gpaste-applet-settings-private.h \
	src/libgpaste/applet/gpaste-applet-status-icon-private.h \
	src/libgpaste/applet/gpaste-applet-switch-private.h \
	src/libgpaste/client/gpaste-client-private.h \
	src/libgpaste/core/gpaste-clipboard-private.h \
	src/libgpaste/core/gpaste-clipboards-manager-private.h \
	src/libgpaste/core/gpaste-history-private.h \
	src/libgpaste/core/gpaste-image-item-private.h \
	src/libgpaste/core/gpaste-item-private.h \
	src/libgpaste/core/gpaste-password-item-private.h \
	src/libgpaste/core/gpaste-text-item-private.h \
	src/libgpaste/core/gpaste-uris-item-private.h \
	src/libgpaste/daemon/gpaste-daemon-private.h \
	src/libgpaste/gnome-shell-client/gpaste-gnome-shell-client-private.h \
	src/libgpaste/keybinder/gpaste-keybinder-private.h \
	src/libgpaste/keybinder/gpaste-keybinding-private.h \
	src/libgpaste/keybinder/gpaste-make-password-keybinding-private.h \
	src/libgpaste/keybinder/gpaste-pop-keybinding-private.h \
	src/libgpaste/keybinder/gpaste-show-history-keybinding-private.h \
	src/libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding-private.h \
	src/libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding-private.h \
	src/libgpaste/screensaver-client/gpaste-screensaver-client-private.h \
	src/libgpaste/settings/gpaste-settings-private.h \
	src/libgpaste/settings-ui/gpaste-settings-ui-panel-private.h \
	src/libgpaste/settings-ui/gpaste-settings-ui-stack-private.h \
	src/libgpaste/settings-ui/gpaste-settings-ui-widget-private.h \
	$(NULL)

lib_libgpaste_la_public_headers = \
	src/libgpaste/gpaste.h \
	src/libgpaste/gpaste-gdbus-defines.h  \
	src/libgpaste/gpaste-gsettings-keys.h \
	src/libgpaste/gpaste-macros.h \
	src/libgpaste/applet/gpaste-applet.h \
	src/libgpaste/applet/gpaste-applet-about.h \
	src/libgpaste/applet/gpaste-applet-delete.h \
	src/libgpaste/applet/gpaste-applet-empty.h \
	src/libgpaste/applet/gpaste-applet-footer.h \
	src/libgpaste/applet/gpaste-applet-header.h \
	src/libgpaste/applet/gpaste-applet-history.h \
	src/libgpaste/applet/gpaste-applet-icon.h \
	src/libgpaste/applet/gpaste-applet-item.h \
	src/libgpaste/applet/gpaste-applet-menu.h \
	src/libgpaste/applet/gpaste-applet-quit.h \
	src/libgpaste/applet/gpaste-applet-settings.h \
	src/libgpaste/applet/gpaste-applet-status-icon.h \
	src/libgpaste/applet/gpaste-applet-switch.h \
	src/libgpaste/client/gpaste-client.h \
	src/libgpaste/core/gpaste-clipboard.h \
	src/libgpaste/core/gpaste-clipboards-manager.h \
	src/libgpaste/core/gpaste-history.h \
	src/libgpaste/core/gpaste-image-item.h \
	src/libgpaste/core/gpaste-item.h \
	src/libgpaste/core/gpaste-password-item.h \
	src/libgpaste/core/gpaste-text-item.h \
	src/libgpaste/core/gpaste-update-enums.h \
	src/libgpaste/core/gpaste-uris-item.h \
	src/libgpaste/daemon/gpaste-daemon.h \
	src/libgpaste/gnome-shell-client/gpaste-gnome-shell-client.h \
	src/libgpaste/keybinder/gpaste-keybinder.h \
	src/libgpaste/keybinder/gpaste-keybinding.h \
	src/libgpaste/keybinder/gpaste-make-password-keybinding.h \
	src/libgpaste/keybinder/gpaste-pop-keybinding.h \
	src/libgpaste/keybinder/gpaste-show-history-keybinding.h \
	src/libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding.h \
	src/libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding.h \
	src/libgpaste/screensaver-client/gpaste-screensaver-client.h \
	src/libgpaste/settings/gpaste-settings.h \
	src/libgpaste/settings-ui/gpaste-settings-ui-panel.h \
	src/libgpaste/settings-ui/gpaste-settings-ui-stack.h \
	src/libgpaste/settings-ui/gpaste-settings-ui-widget.h \
	$(NULL)

lib_libgpaste_la_SOURCES = \
	$(lib_libgpaste_la_public_headers) \
	$(lib_libgpaste_la_private_headers) \
	src/libgpaste/applet/gpaste-applet.c \
	src/libgpaste/applet/gpaste-applet-about.c \
	src/libgpaste/applet/gpaste-applet-delete.c \
	src/libgpaste/applet/gpaste-applet-empty.c \
	src/libgpaste/applet/gpaste-applet-footer.c \
	src/libgpaste/applet/gpaste-applet-header.c \
	src/libgpaste/applet/gpaste-applet-history.c \
	src/libgpaste/applet/gpaste-applet-icon.c \
	src/libgpaste/applet/gpaste-applet-item.c \
	src/libgpaste/applet/gpaste-applet-menu.c \
	src/libgpaste/applet/gpaste-applet-quit.c \
	src/libgpaste/applet/gpaste-applet-settings.c \
	src/libgpaste/applet/gpaste-applet-status-icon.c \
	src/libgpaste/applet/gpaste-applet-switch.c \
	src/libgpaste/client/gpaste-client.c \
	src/libgpaste/core/gpaste-clipboard.c \
	src/libgpaste/core/gpaste-clipboards-manager.c \
	src/libgpaste/core/gpaste-history.c \
	src/libgpaste/core/gpaste-image-item.c \
	src/libgpaste/core/gpaste-item.c \
	src/libgpaste/core/gpaste-password-item.c \
	src/libgpaste/core/gpaste-text-item.c \
	src/libgpaste/core/gpaste-update-enums.c \
	src/libgpaste/core/gpaste-uris-item.c \
	src/libgpaste/daemon/gpaste-daemon.c \
	src/libgpaste/gnome-shell-client/gpaste-gnome-shell-client.c \
	src/libgpaste/keybinder/gpaste-keybinder.c \
	src/libgpaste/keybinder/gpaste-keybinding.c \
	src/libgpaste/keybinder/gpaste-make-password-keybinding.c \
	src/libgpaste/keybinder/gpaste-pop-keybinding.c \
	src/libgpaste/keybinder/gpaste-show-history-keybinding.c \
	src/libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding.c \
	src/libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding.c \
	src/libgpaste/screensaver-client/gpaste-screensaver-client.c \
	src/libgpaste/settings/gpaste-settings.c \
	src/libgpaste/settings-ui/gpaste-settings-ui-panel.c \
	src/libgpaste/settings-ui/gpaste-settings-ui-stack.c \
	src/libgpaste/settings-ui/gpaste-settings-ui-widget.c \
	$(NULL)

nodist_lib_libgpaste_la_SOURCES = \
	src/libgpaste/gpaste-config.h \
	$(NULL)

if ENABLE_UNITY
lib_libgpaste_la_private_headers += \
	src/libgpaste/applet/gpaste-applet-app-indicator-private.h \
	$(NULL)
lib_libgpaste_la_public_headers += \
	src/libgpaste/applet/gpaste-applet-app-indicator.h \
	$(NULL)
lib_libgpaste_la_SOURCES += \
	src/libgpaste/applet/gpaste-applet-app-indicator.c \
	$(NULL)
endif

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
	-version-info $(LIBGPASTE_CURRENT):$(LIBGPASTE_REVISION):$(LIBGPASTE_AGE) \
	-Wl,--version-script=$(libgpaste_symbols_file) \
	$(NULL)

lib_libgpaste_la_DEPENDENCIES = \
	$(libgpaste_symbols_file) \
	$(NULL)

pkginclude_HEADERS += \
	$(lib_libgpaste_la_public_headers) \
	$(NULL)

nodist_pkginclude_HEADERS = \
	src/libgpaste/gpaste-config.h \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_la_file) \
	$(NULL)

EXTRA_DIST += \
	src/libgpaste/gpaste-config.h.in \
	$(libgpaste_symbols_file) \
	$(NULL)
