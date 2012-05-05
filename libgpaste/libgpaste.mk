# This file is part of GPaste.
#
# Copyright 2012 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

libgpaste_la_file = libgpaste/libgpaste.la

LIBGPASTE_CURRENT=1
LIBGPASTE_REVISION=0
LIBGPASTE_AGE=0

libgpaste_libgpaste_la_public_headers = \
	libgpaste/gpaste.h \
	libgpaste/gpaste-clipboard.h \
	libgpaste/gpaste-clipboards-manager.h \
	libgpaste/gpaste-history.h \
	libgpaste/gpaste-item.h \
	libgpaste/gpaste-keybinder.h \
	libgpaste/gpaste-keybinding.h \
	libgpaste/gpaste-settings.h \
	libgpaste/gpaste-xcb-wrapper.h \
	$(NULL)

libgpaste_libgpaste_la_private_headers = \
	libgpaste/gpaste-clipboard-internal.h \
	libgpaste/gpaste-clipboard-private.h \
	libgpaste/gpaste-clipboards-manager-private.h \
	libgpaste/gpaste-history-private.h \
	libgpaste/gpaste-item-private.h \
	libgpaste/gpaste-keybinder-private.h \
	libgpaste/gpaste-keybinding-private.h \
	libgpaste/gpaste-settings-private.h \
	libgpaste/gpaste-xcb-wrapper-private.h \
	$(NULL)

libgpaste_libgpaste_la_SOURCES = \
	$(libgpaste_libgpaste_la_public_headers) \
	$(libgpaste_libgpaste_la_private_headers) \
	libgpaste/gpaste-clipboard.c \
	libgpaste/gpaste-clipboard-internal.c \
	libgpaste/gpaste-clipboards-manager.c \
	libgpaste/gpaste-history.c \
	libgpaste/gpaste-item.c \
	libgpaste/gpaste-keybinder.c \
	libgpaste/gpaste-keybinding.c \
	libgpaste/gpaste-settings.c \
	libgpaste/gpaste-xcb-wrapper.c \
	$(NULL)

libgpaste_libgpaste_la_CFLAGS = \
	$(GDK_PIXBUF_CFLAGS) \
	$(GDK_CFLAGS) \
	$(GTK_CFLAGS) \
	$(XCB_CFLAGS) \
	$(XML_CFLAGS) \
	$(AM_CFLAGS) \
	$(NULL)

libgpaste_libgpaste_la_LIBADD = \
	$(GDK_PIXBUF_LIBS) \
	$(GDK_LIBS) \
	$(GTK_LIBS) \
	$(XCB_LIBS) \
	$(XML_LIBS) \
	$(AM_LIBS) \
	$(NULL)

libgpaste_symbols_file = $(srcdir)/libgpaste/libgpaste.sym

libgpaste_libgpaste_la_LDFLAGS = \
	-version-info $(LIBGPASTE_CURRENT):$(LIBGPASTE_REVISION):$(LIBGPASTE_AGE) \
	-Wl,--version-script=$(libgpaste_symbols_file) \
	$(NULL)

libgpaste_libgpaste_la_DEPENDENCIES = \
	$(libgpaste_symbols_file) \
	$(NULL)

pkginclude_HEADERS = \
	$(libgpaste_libgpaste_la_public_headers) \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_la_file) \
	$(NULL)

EXTRA_DIST += \
	$(libgpaste_symbols_file) \
	$(NULL)
