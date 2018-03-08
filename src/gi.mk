## This file is part of GPaste.
##
## Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

INTROSPECTION_SCANNER_ARGS = $(NULL)
INTROSPECTION_COMPILER_ARGS = $(NULL)
INTROSPECTION_GIRS = $(NULL)

libgpaste_gir_file = bindings/GPaste-1.0.gir

$(libgpaste_gir_file): $(libgpaste_la_file)
bindings_GPaste_1_0_gir_FILES =     \
	$(pkginclude_HEADERS)       \
	$(lib_libgpaste_la_SOURCES) \
	$(NULL)
bindings_GPaste_1_0_gir_CFLAGS = \
	$(AM_CPPFLAGS)           \
	$(NULL)
bindings_GPaste_1_0_gir_LIBS = $(libgpaste_la_file)
bindings_GPaste_1_0_gir_EXPORT_PACKAGES = libgpaste
bindings_GPaste_1_0_gir_SCANNERFLAGS = --warn-all
bindings_GPaste_1_0_gir_C_INCLUDES = gpaste.h
bindings_GPaste_1_0_gir_INCLUDES = \
	GdkPixbuf-2.0              \
	Gio-2.0                    \
	GObject-2.0                \
	Gtk-3.0                    \
	$(NULL)

if HAVE_INTROSPECTION
INTROSPECTION_GIRS +=         \
	$(libgpaste_gir_file) \
	$(NULL)
endif

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES +=           \
	$(gir_DATA)     \
	$(typelib_DATA) \
	$(NULL)

-include $(INTROSPECTION_MAKEFILE)
