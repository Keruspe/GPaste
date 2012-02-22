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

LIBGPASTE_CURRENT=0
LIBGPASTE_REVISION=0
LIBGPASTE_AGE=0

libgpaste_libgpaste_la_public_headers = \
	libgpaste/gpaste.h \
	libgpaste/gpaste-clipboard.h \
	libgpaste/gpaste-clipboards-manager.h \
	libgpaste/gpaste-history.h \
	libgpaste/gpaste-item.h \
	libgpaste/gpaste-keybinder.h \
	libgpaste/gpaste-settings.h \
	$(NULL)

libgpaste_libgpaste_la_private_headers = \
	libgpaste/gpaste-clipboard-private.h \
	libgpaste/gpaste-clipboards-manager-private.h \
	libgpaste/gpaste-history-private.h \
	libgpaste/gpaste-item-private.h \
	libgpaste/gpaste-keybinder-private.h \
	libgpaste/gpaste-settings-private.h \
	$(NULL)

libgpaste_libgpaste_la_SOURCES = \
	libgpaste/gpaste-clipboard.c \
	libgpaste/gpaste-clipboards-manager.c \
	libgpaste/gpaste-history.c \
	libgpaste/gpaste-item.c \
	libgpaste/gpaste-keybinder.c \
	libgpaste/gpaste-settings.c \
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

libgpaste_libgpaste_la_LDFLAGS = \
	-version-info $(LIBGPASTE_CURRENT):$(LIBGPASTE_REVISION):$(LIBGPASTE_AGE) \
	-Wl,--version-script=$(top_srcdir)/libgpaste/libgpaste.sym \
	$(NULL)

libgpaste_libgpaste_la_DEPENDENCIES = $(top_srcdir)/libgpaste/libgpaste.sym

pkginclude_HEADERS = $(libgpaste_libgpaste_la_public_headers)

pkgconfig_DATA += libgpaste/gpaste.pc

lib_LTLIBRARIES += libgpaste/libgpaste.la

EXTRA_DIST += \
	$(libgpaste_libgpaste_la_private_headers) \
	libgpaste/libgpaste.sym \
	$(NULL)
