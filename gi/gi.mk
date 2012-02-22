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

-include $(INTROSPECTION_MAKEFILE)

INTROSPECTION_GIRS = $(NULL)
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --c-include=gpaste.h
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

introspection_sources = \
       $(libgpaste_libgpaste_la_SOURCES) \
       $(libgpaste_libgpaste_la_public_headers) \
       $(libgpaste_libgpaste_la_private_headers) \
       $(NULL)

gi/GPaste-1.0.gir: libgpaste/libgpaste.la
gi_GPaste_1_0_gir_INCLUDES = GdkPixbuf-2.0 Gio-2.0 GObject-2.0 Gtk-3.0 libxml2-2.0
gi_GPaste_1_0_gir_CFLAGS = $(INCLUDES) -DG_PASTE_COMPILATION -I$(srcdir)/libgpaste
gi_GPaste_1_0_gir_LIBS = libgpaste/libgpaste.la
gi_GPaste_1_0_gir_SCANNERFLAGS = --warn-all --warn-error
gi_GPaste_1_0_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += gi/GPaste-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
