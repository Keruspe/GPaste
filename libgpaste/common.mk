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

libgpaste_common_la_file = libgpaste/common/libgpaste-common.la

libgpaste_common_public_headers = \
	libgpaste/common/gdbus-defines.h  \
	libgpaste/common/gpaste-clipboard-common.h \
	$(NULL)

libgpaste_common_libgpaste_common_la_SOURCES = \
	$(libgpaste_common_public_headers) \
	libgpaste/common/gpaste-clipboard-common.c \
	$(NULL)

libgpaste_common_libgpaste_common_la_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GDK_PIXBUF_CFLAGS) \
	$(AM_CFLAGS) \
	$(NULL)

libgpaste_common_libgpaste_common_la_LIBADD = \
	$(GTK_LIBS) \
	$(GDK_PIXBUF_LIBS) \
	$(AM_LIBS) \
	$(NULL)

libgpaste_common_libgpaste_common_la_LDFLAGS = \
	-avoid-version \
	$(NULL)

pkglib_LTLIBRARIES += \
	$(libgpaste_common_la_file) \
	$(NULL)
