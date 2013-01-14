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

libgpaste_vapi_file = bindings/vapi/gpaste-1.0.vapi

$(libgpaste_vapi_file): $(libgpaste_gir_file)
	@ $(MKDIR_P) bindings/vapi
	$(AM_V_GEN) $(VAPIGEN) --directory bindings/vapi --library gpaste-1.0 --pkg gdk-pixbuf-2.0 --pkg gio-2.0 --pkg gobject-2.0 --pkg gtk+-3.0 $^

vapidir = $(datadir)/vala/vapi
dist_vapi_DATA = \
	$(libgpaste_vapi_file:.vapi=.deps) \
	$(NULL)

if ENABLE_VALA
nodist_vapi_DATA = \
	$(libgpaste_vapi_file) \
	$(NULL)

CLEANFILES += \
	$(libgpaste_vapi_file) \
	$(NULL)
endif
