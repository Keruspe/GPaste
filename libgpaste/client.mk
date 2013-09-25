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

libgpaste_client_la_file = libgpaste/client/libgpaste-client.la

LIBGPASTE_CLIENT_CURRENT=1
LIBGPASTE_CLIENT_REVISION=0
LIBGPASTE_CLIENT_AGE=0

libgpaste_client_libgpaste_client_la_private_headers = \
	libgpaste/client/gpaste-client-private.h \
	$(NULL)

libgpaste_client_libgpaste_client_la_public_headers = \
	libgpaste/client/gpaste-client.h \
	$(NULL)

libgpaste_client_libgpaste_client_la_SOURCES = \
	$(libgpaste_client_libgpaste_client_la_public_headers) \
	$(libgpaste_client_libgpaste_client_la_private_headers) \
	libgpaste/client/gpaste-client.c \
	$(NULL)

libgpaste_client_libgpaste_client_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(NULL)

libgpaste_client_libgpaste_client_la_LIBADD = \
	$(AM_LIBS) \
	$(NULL)

libgpaste_client_symbols_file = $(srcdir)/libgpaste/client/libgpaste-client.sym

libgpaste_client_libgpaste_client_la_LDFLAGS = \
	-version-info $(LIBGPASTE_CLIENT_CURRENT):$(LIBGPASTE_CLIENT_REVISION):$(LIBGPASTE_CLIENT_AGE) \
	-Wl,--version-script=$(libgpaste_client_symbols_file) \
	$(NULL)

libgpaste_client_libgpaste_client_la_DEPENDENCIES = \
	$(libgpaste_client_symbols_file) \
	$(NULL)

pkginclude_HEADERS += \
	$(libgpaste_client_libgpaste_client_la_public_headers) \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_client_la_file) \
	$(NULL)

EXTRA_DIST += \
	$(libgpaste_client_symbols_file) \
	$(NULL)
