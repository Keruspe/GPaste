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

VAPI_FILES = \
	vapi/gpaste-1.0.vapi \
	$(NULL)

VAPI_DEPS = \
	vapi/gpaste-1.0.deps \
	$(NULL)

vapidir = $(datadir)/vala/vapi
dist_vapi_DATA = \
	$(VAPI_FILES) \
	$(VAPI_DEPS) \
	$(NULL)

if ENABLE_VALA
vapi/gpaste-1.0.vapi: gi/GPaste-1.0.gir
	@ $(MKDIR_P) vapi
	$(AM_V_GEN) $(VAPIGEN) --directory vapi --library gpaste-1.0 --pkg gdk-pixbuf-2.0 --pkg gio-2.0 --pkg gobject-2.0 --pkg gtk+-3.0 $^

CLEANFILES += \
	$(VAPI_FILES) \
	$(NULL)
endif
