## This file is part of GPaste.
##
## Copyright 2012-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
##
## GPaste is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## GPaste is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with GPaste.  If not, see <http://www.gnu.org/licenses/>.

VAPIGEN_VAPIS = $(NULL)

libgpaste_vapi_file = bindings/gpaste-1.0.vapi
libgpaste_vapi_deps_file = $(libgpaste_vapi_file:.vapi=.deps)

GPASTE_VAPI_DEPS =     \
	gdk-pixbuf-2.0 \
	gio-2.0        \
	glib-2.0       \
	gobject-2.0    \
	gtk+-3.0       \
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

if ENABLE_VAPIGEN
VAPIGEN_VAPIS+=                \
	$(libgpaste_vapi_file) \
	$(NULL)
endif

vapidir = $(datadir)/vala/vapi
vapi_DATA =                         \
	$(VAPIGEN_VAPIS)            \
	$(libgpaste_vapi_deps_file) \
	$(NULL)

CLEANFILES +=        \
	$(vapi_DATA) \
	$(NULL)

-include $(VAPIGEN_MAKEFILE)
