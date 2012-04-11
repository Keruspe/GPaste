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

pkglibexec_PROGRAMS += \
	bin/gpasted \
	$(NULL)

bin_gpasted_headers = \
	src/gpasted/gpaste-daemon.h \
	src/gpasted/gpaste-daemon-private.h \
	$(NULL)

bin_gpasted_SOURCES = \
	$(bin_gpasted_headers) \
	src/gpasted/gpaste-daemon.c \
	src/gpasted/gpasted.c \
	$(NULL)

bin_gpasted_CFLAGS = \
	$(GDK_CFLAGS) \
	$(GTK_CFLAGS) \
	$(AM_CFLAGS) \
	$(NULL)

bin_gpasted_LDADD = \
	$(GDK_LIBS) \
	$(GTK_LIBS) \
	$(AM_LIBS) \
	$(libgpaste_la_file) \
	$(NULL)
