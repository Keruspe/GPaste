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

libgpaste_core_la_file = libgpaste/core/libgpaste-core.la

LIBGPASTE_CORE_CURRENT=2
LIBGPASTE_CORE_REVISION=0
LIBGPASTE_CORE_AGE=0

$(libgpaste_core_la_file): $(libgpaste_common_la_file) $(libgpaste_settings_la_file)

libgpaste_core_public_headers = \
	libgpaste/core/gpaste.h \
	libgpaste/core/gpaste-clipboard.h \
	libgpaste/core/gpaste-clipboards-manager.h \
	libgpaste/core/gpaste-history.h \
	libgpaste/core/gpaste-image-item.h \
	libgpaste/core/gpaste-item.h \
	libgpaste/core/gpaste-text-item.h \
	libgpaste/core/gpaste-uris-item.h \
	$(NULL)

libgpaste_core_private_headers = \
	libgpaste/core/gpaste-clipboard-private.h \
	libgpaste/core/gpaste-clipboards-manager-private.h \
	libgpaste/core/gpaste-history-private.h \
	libgpaste/core/gpaste-image-item-private.h \
	libgpaste/core/gpaste-item-private.h \
	libgpaste/core/gpaste-text-item-private.h \
	libgpaste/core/gpaste-uris-item-private.h \
	$(NULL)

libgpaste_core_libgpaste_core_la_SOURCES = \
	$(libgpaste_core_public_headers) \
	$(libgpaste_core_private_headers) \
	libgpaste/core/gpaste-clipboard.c \
	libgpaste/core/gpaste-clipboards-manager.c \
	libgpaste/core/gpaste-history.c \
	libgpaste/core/gpaste-image-item.c \
	libgpaste/core/gpaste-item.c \
	libgpaste/core/gpaste-text-item.c \
	libgpaste/core/gpaste-uris-item.c \
	$(NULL)

libgpaste_core_libgpaste_core_la_CFLAGS = \
	$(GDK_PIXBUF_CFLAGS) \
	$(GTK_CFLAGS) \
	$(XML_CFLAGS) \
	$(AM_CFLAGS) \
	$(NULL)

libgpaste_core_libgpaste_core_la_LIBADD = \
	$(libgpaste_common_la_file) \
	$(libgpaste_settings_la_file) \
	$(XML_LIBS) \
	$(AM_LIBS) \
	$(NULL)

libgpaste_core_symbols_file = $(srcdir)/libgpaste/core/libgpaste-core.sym

libgpaste_core_libgpaste_core_la_LDFLAGS = \
	-version-info $(LIBGPASTE_CORE_CURRENT):$(LIBGPASTE_CORE_REVISION):$(LIBGPASTE_CORE_AGE) \
	-Wl,--version-script=$(libgpaste_core_symbols_file) \
	$(NULL)

libgpaste_core_libgpaste_core_la_DEPENDENCIES = \
	$(libgpaste_core_symbols_file) \
	$(NULL)

pkginclude_HEADERS += \
	$(libgpaste_core_public_headers) \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_core_la_file) \
	$(NULL)

EXTRA_DIST += \
	$(libgpaste_core_symbols_file) \
	$(NULL)
