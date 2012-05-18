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

libgpaste_daemon_la_file = libgpaste-daemon/libgpaste-daemon.la

$(libgpaste_daemon_la_file): $(libgpaste_la_file)

LIBGPASTE_DAEMON_CURRENT=0
LIBGPASTE_DAEMON_REVISION=0
LIBGPASTE_DAEMON_AGE=0

libgpaste_daemon_libgpaste_daemon_la_private_headers = \
	libgpaste-daemon/gpaste-daemon.h \
	libgpaste-daemon/gpaste-daemon-private.h \
	$(NULL)

libgpaste_daemon_libgpaste_daemon_la_SOURCES = \
	$(libgpaste_daemon_libgpaste_daemon_la_private_headers) \
	libgpaste-daemon/gpaste-daemon.c \
	$(NULL)

libgpaste_daemon_libgpaste_daemon_la_CFLAGS = \
	$(GDK_PIXBUF_CFLAGS) \
	$(GTK_CFLAGS) \
	$(AM_CFLAGS) \
	$(NULL)

libgpaste_daemon_libgpaste_daemon_la_LIBADD = \
	$(libgpaste_la_file) \
	$(NULL)

libgpaste_daemon_symbols_file = $(srcdir)/libgpaste-daemon/libgpaste-daemon.sym

libgpaste_daemon_libgpaste_daemon_la_LDFLAGS = \
	-version-info $(LIBGPASTE_DAEMON_CURRENT):$(LIBGPASTE_DAEMON_REVISION):$(LIBGPASTE_DAEMON_AGE) \
	-Wl,--version-script=$(libgpaste_daemon_symbols_file) \
	$(NULL)

libgpaste_daemon_libgpaste_daemon_la_DEPENDENCIES = \
	$(libgpaste_daemon_symbols_file) \
	$(NULL)

lib_LTLIBRARIES += \
	$(libgpaste_daemon_la_file) \
	$(NULL)

EXTRA_DIST += \
	$(libgpaste_daemon_symbols_file) \
	libgpaste-daemon/gdbus-defines.h \
	$(NULL)
