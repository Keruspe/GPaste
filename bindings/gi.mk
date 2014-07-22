# This file is part of GPaste.
#
# Copyright 2012-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

-include $(INTROSPECTION_MAKEFILE)

INTROSPECTION_SCANNER_ARGS = --c-include=gpaste.h --c-include=gpaste-client.h --c-include=gpaste-daemon.h
INTROSPECTION_COMPILER_ARGS = $(NULL)
INTROSPECTION_GIRS = $(NULL)

libgpaste_gir_file = bindings/gi/GPaste-1.0.gir

$(libgpaste_gir_file): $(libgpaste_la_file)
bindings_gi_GPaste_1_0_gir_FILES = \
	$(lib_libgpaste_la_SOURCES) \
	$(NULL)
bindings_gi_GPaste_1_0_gir_CFLAGS = \
	$(AM_CFLAGS) \
	$(NULL)
bindings_gi_GPaste_1_0_gir_LIBS = $(libgpaste_la_file)
bindings_gi_GPaste_1_0_gir_EXPORT_PACKAGES = libgpaste
bindings_gi_GPaste_1_0_gir_SCANNERFLAGS = --warn-all
bindings_gi_GPaste_1_0_gir_INCLUDES = \
	GdkPixbuf-2.0 \
	Gio-2.0 \
	GObject-2.0 \
	Gtk-3.0 \
	$(NULL)

if HAVE_INTROSPECTION
INTROSPECTION_GIRS += \
	$(libgpaste_gir_file) \
	$(NULL)
endif

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += \
	$(gir_DATA) \
	$(typelib_DATA) \
	$(NULL)
