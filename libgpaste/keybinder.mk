# This file is part of GPaste.
#
# Copyright 2012-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

libgpaste_keybinder_la_file = libgpaste/keybinder/libgpaste-keybinder.la

LIBGPASTE_KEYBINDER_CURRENT=2
LIBGPASTE_KEYBINDER_REVISION=0
LIBGPASTE_KEYBINDER_AGE=0

$(libgpaste_keybinder_la_file): $(libgpaste_common_la_file) $(libgpaste_settings_la_file)

libgpaste_keybinder_public_headers = \
	libgpaste/keybinder/gpaste-keybinder.h \
	libgpaste/keybinder/gpaste-keybinding.h \
	libgpaste/keybinder/gpaste-pop-keybinding.h \
	libgpaste/keybinder/gpaste-show-history-keybinding.h \
	libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding.h \
	libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding.h \
	$(NULL)

libgpaste_keybinder_private_headers = \
	libgpaste/keybinder/gpaste-keybinder-private.h \
	libgpaste/keybinder/gpaste-keybinding-private.h \
	libgpaste/keybinder/gpaste-pop-keybinding-private.h \
	libgpaste/keybinder/gpaste-show-history-keybinding-private.h \
	libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding-private.h \
	libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding-private.h \
	$(NULL)

libgpaste_keybinder_libgpaste_keybinder_la_SOURCES = \
	$(libgpaste_keybinder_public_headers) \
	$(libgpaste_keybinder_private_headers) \
	libgpaste/keybinder/gpaste-keybinder.c \
	libgpaste/keybinder/gpaste-keybinding.c \
	libgpaste/keybinder/gpaste-pop-keybinding.c \
	libgpaste/keybinder/gpaste-show-history-keybinding.c \
	libgpaste/keybinder/gpaste-sync-clipboard-to-primary-keybinding.c \
	libgpaste/keybinder/gpaste-sync-primary-to-clipboard-keybinding.c \
	$(NULL)

libgpaste_keybinder_libgpaste_keybinder_la_CFLAGS = \
	$(GDK_CFLAGS) \
	$(GTK_CFLAGS) \
	$(X11_CFLAGS) \
	$(AM_CFLAGS) \
	$(NULL)

libgpaste_keybinder_libgpaste_keybinder_la_LIBADD = \
	$(libgpaste_common_la_file) \
	$(libgpaste_settings_la_file) \
	$(X11_LIBS) \
	$(AM_LIBS) \
	$(NULL)

libgpaste_keybinder_symbols_file = $(srcdir)/libgpaste/keybinder/libgpaste-keybinder.sym

libgpaste_keybinder_libgpaste_keybinder_la_LDFLAGS = \
	-version-info $(LIBGPASTE_KEYBINDER_CURRENT):$(LIBGPASTE_KEYBINDER_REVISION):$(LIBGPASTE_KEYBINDER_AGE) \
	-Wl,--version-script=$(libgpaste_keybinder_symbols_file) \
	$(NULL)

libgpaste_keybinder_libgpaste_keybinder_la_DEPENDENCIES = \
	$(libgpaste_keybinder_symbols_file) \
	$(NULL)

pkginclude_HEADERS += \
	$(libgpaste_keybinder_public_headers) \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_keybinder_la_file) \
	$(NULL)

EXTRA_DIST += \
	$(libgpaste_keybinder_symbols_file) \
	$(NULL)
