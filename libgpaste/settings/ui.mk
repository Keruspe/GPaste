# This file is part of GPaste.
#
# Copyright 2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

libgpaste_settings_ui_la_file = libgpaste/settings/ui/libgpaste-settings-ui.la

LIBGPASTE_SETTINGS_UI_CURRENT=1
LIBGPASTE_SETTINGS_UI_REVISION=0
LIBGPASTE_SETTINGS_UI_AGE=0

libgpaste_settings_ui_public_headers = \
	libgpaste/settings/ui/gpaste-settings-ui-notebook.h \
	libgpaste/settings/ui/gpaste-settings-ui-panel.h \
	libgpaste/settings/ui/gpaste-settings-ui-window.h \
	$(NULL)

libgpaste_settings_ui_private_headers = \
	libgpaste/settings/ui/gpaste-settings-ui-notebook-private.h \
	libgpaste/settings/ui/gpaste-settings-ui-panel-private.h \
	libgpaste/settings/ui/gpaste-settings-ui-window-private.h \
	$(NULL)

libgpaste_settings_ui_libgpaste_settings_ui_la_SOURCES = \
	$(libgpaste_settings_ui_public_headers) \
	$(libgpaste_settings_ui_private_headers) \
	libgpaste/settings/ui/gpaste-settings-ui-notebook.c \
	libgpaste/settings/ui/gpaste-settings-ui-panel.c \
	libgpaste/settings/ui/gpaste-settings-ui-window.c \
	$(NULL)

libgpaste_settings_ui_libgpaste_settings_ui_la_CFLAGS = \
	$(GTK_CFLAGS) \
	$(AM_CFLAGS) \
	$(NULL)

libgpaste_settings_ui_libgpaste_settings_ui_la_LIBADD = \
	$(libgpaste_client_la_file) \
	$(libgpaste_settings_la_file) \
	$(GTK_LIBS) \
	$(AM_LIBS) \
	$(NULL)

libgpaste_settings_ui_symbols_file = $(srcdir)/libgpaste/settings/ui/libgpaste-settings-ui.sym

libgpaste_settings_ui_libgpaste_settings_ui_la_LDFLAGS = \
	-version-info $(LIBGPASTE_SETTINGS_UI_CURRENT):$(LIBGPASTE_SETTINGS_UI_REVISION):$(LIBGPASTE_SETTINGS_UI_AGE) \
	-Wl,--version-script=$(libgpaste_settings_ui_symbols_file) \
	$(NULL)

libgpaste_settings_ui_libgpaste_settings_ui_la_DEPENDENCIES = \
	$(libgpaste_settings_ui_symbols_file) \
	$(NULL)

pkginclude_HEADERS += \
	$(libgpaste_settings_ui_public_headers) \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_settings_ui_la_file) \
	$(NULL)

EXTRA_DIST += \
	$(libgpaste_settings_ui_symbols_file) \
	$(NULL)
