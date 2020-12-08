## This file is part of GPaste.
##
## Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

VAPIGEN_VAPIS = $(NULL)

libgpaste_vapi_file = bindings/gpaste-1.0.vapi
libgpaste_vapi_deps_file = $(libgpaste_vapi_file:.vapi=.deps)

GPASTE_VAPI_DEPS =     \
	gdk-pixbuf-2.0 \
	gio-2.0        \
	glib-2.0       \
	gobject-2.0    \
	gtk4           \
	$(NULL)

$(libgpaste_vapi_deps_file): $(libgpaste_gir_file)
	$(AM_V_GEN) rm -f $@;              \
	for dep in ${GPASTE_VAPI_DEPS}; do \
	    echo $$dep >> $@;              \
	done

$(libgpaste_vapi_file): $(libgpaste_gir_file) $(libgpaste_vapi_deps_file)
bindings_gpaste_1_0_vapi_FILES = \
	$(libgpaste_gir_file)    \
	$(NULL)
bindings_gpaste_1_0_vapi_DEPS = \
	$(GPASTE_VAPI_DEPS)     \
	$(NULL)

vapidir = $(datadir)/vala/vapi
vapi_DATA = $(NULL)

if ENABLE_VAPIGEN
VAPIGEN_VAPIS +=               \
	$(libgpaste_vapi_file) \
	$(NULL)
vapi_DATA +=                        \
	$(VAPIGEN_VAPIS)            \
	$(libgpaste_vapi_deps_file) \
	$(NULL)
endif

CLEANFILES +=        \
	$(vapi_DATA) \
	$(NULL)

-include $(VAPIGEN_MAKEFILE)
