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

libgpaste_settings_la_file = libgpaste/settings/libgpaste-settings.la

LIBGPASTE_SETTINGS_CURRENT=4
LIBGPASTE_SETTINGS_REVISION=0
LIBGPASTE_SETTINGS_AGE=0

libgpaste_settings_public_headers = \
	libgpaste/settings/gpaste-settings.h \
	$(NULL)

libgpaste_settings_private_headers = \
	libgpaste/settings/gpaste-settings-private.h \
	$(NULL)

libgpaste_settings_libgpaste_settings_la_SOURCES = \
	$(libgpaste_settings_public_headers) \
	$(libgpaste_settings_private_headers) \
	libgpaste/settings/gpaste-settings.c \
	$(NULL)

libgpaste_settings_libgpaste_settings_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(NULL)

libgpaste_settings_libgpaste_settings_la_LIBADD = \
	$(AM_LIBS) \
	$(NULL)

libgpaste_settings_symbols_file = $(srcdir)/libgpaste/settings/libgpaste-settings.sym

libgpaste_settings_libgpaste_settings_la_LDFLAGS = \
	-version-info $(LIBGPASTE_SETTINGS_CURRENT):$(LIBGPASTE_SETTINGS_REVISION):$(LIBGPASTE_SETTINGS_AGE) \
	-Wl,--version-script=$(libgpaste_settings_symbols_file) \
	$(NULL)

libgpaste_settings_libgpaste_settings_la_DEPENDENCIES = \
	$(libgpaste_settings_symbols_file) \
	$(NULL)

pkginclude_HEADERS += \
	$(libgpaste_settings_public_headers) \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_settings_la_file) \
	$(NULL)

EXTRA_DIST += \
	$(libgpaste_settings_symbols_file) \
	$(NULL)
