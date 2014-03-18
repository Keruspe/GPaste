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

libgpaste_applet_la_file = libgpaste/applet/libgpaste-applet.la

$(libgpaste_applet_la_file): $(libgpaste_client_la_file)

LIBGPASTE_APPLET_CURRENT=1
LIBGPASTE_APPLET_REVISION=0
LIBGPASTE_APPLET_AGE=0

libgpaste_applet_libgpaste_applet_la_private_headers = \
	libgpaste/applet/gpaste-applet-private.h \
	libgpaste/applet/gpaste-applet-delete-private.h \
	libgpaste/applet/gpaste-applet-empty-private.h \
	libgpaste/applet/gpaste-applet-footer-private.h \
	libgpaste/applet/gpaste-applet-header-private.h \
	libgpaste/applet/gpaste-applet-history-private.h \
	libgpaste/applet/gpaste-applet-icon-private.h \
	libgpaste/applet/gpaste-applet-item-private.h \
	libgpaste/applet/gpaste-applet-menu-private.h \
	libgpaste/applet/gpaste-applet-quit-private.h \
	libgpaste/applet/gpaste-applet-settings-private.h \
	libgpaste/applet/gpaste-applet-status-icon-private.h \
	libgpaste/applet/gpaste-applet-switch-private.h \
	$(NULL)

libgpaste_applet_libgpaste_applet_la_public_headers = \
	libgpaste/applet/gpaste-applet.h \
	libgpaste/applet/gpaste-applet-delete.h \
	libgpaste/applet/gpaste-applet-empty.h \
	libgpaste/applet/gpaste-applet-footer.h \
	libgpaste/applet/gpaste-applet-header.h \
	libgpaste/applet/gpaste-applet-history.h \
	libgpaste/applet/gpaste-applet-icon.h \
	libgpaste/applet/gpaste-applet-item.h \
	libgpaste/applet/gpaste-applet-menu.h \
	libgpaste/applet/gpaste-applet-quit.h \
	libgpaste/applet/gpaste-applet-settings.h \
	libgpaste/applet/gpaste-applet-status-icon.h \
	libgpaste/applet/gpaste-applet-switch.h \
	$(NULL)

libgpaste_applet_libgpaste_applet_la_SOURCES = \
	$(libgpaste_applet_libgpaste_applet_la_public_headers) \
	$(libgpaste_applet_libgpaste_applet_la_private_headers) \
	libgpaste/applet/gpaste-applet.c \
	libgpaste/applet/gpaste-applet-delete.c \
	libgpaste/applet/gpaste-applet-empty.c \
	libgpaste/applet/gpaste-applet-footer.c \
	libgpaste/applet/gpaste-applet-header.c \
	libgpaste/applet/gpaste-applet-history.c \
	libgpaste/applet/gpaste-applet-icon.c \
	libgpaste/applet/gpaste-applet-item.c \
	libgpaste/applet/gpaste-applet-menu.c \
	libgpaste/applet/gpaste-applet-quit.c \
	libgpaste/applet/gpaste-applet-settings.c \
	libgpaste/applet/gpaste-applet-status-icon.c \
	libgpaste/applet/gpaste-applet-switch.c \
	$(NULL)

libgpaste_applet_libgpaste_applet_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(NULL)

libgpaste_applet_libgpaste_applet_la_LIBADD = \
	$(builddir)/$(libgpaste_client_la_file) \
	$(AM_LIBS) \
	$(NULL)

libgpaste_applet_symbols_file = $(srcdir)/libgpaste/applet/libgpaste-applet.sym

libgpaste_applet_libgpaste_applet_la_LDFLAGS = \
	-version-info $(LIBGPASTE_APPLET_CURRENT):$(LIBGPASTE_APPLET_REVISION):$(LIBGPASTE_APPLET_AGE) \
	-Wl,--version-script=$(libgpaste_applet_symbols_file) \
	$(NULL)

libgpaste_applet_libgpaste_applet_la_DEPENDENCIES = \
	$(libgpaste_applet_symbols_file) \
	$(NULL)

pkginclude_HEADERS += \
	$(libgpaste_applet_libgpaste_applet_la_public_headers) \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_applet_la_file) \
	$(NULL)

EXTRA_DIST += \
	$(libgpaste_applet_symbols_file) \
	$(NULL)
