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

SUFFIXES += .pc.in .pc
.pc.in.pc:
	@ $(MKDIR_P) data/pkg-config
	$(AM_V_GEN) $(SED) \
	    -e 's,[@]libdir[@],$(libdir),g' \
	    -e 's,[@]includedir[@],$(includedir),g' \
	    -e 's,[@]VERSION[@],$(VERSION),g' \
	    < $< > $@

pkgconfigdir = $(libdir)/pkgconfig
nodist_pkgconfig_DATA = \
	data/pkg-config/gpaste-1.0.pc \
	$(NULL)

EXTRA_DIST += \
	$(nodist_pkgconfig_DATA:.pc=.pc.in) \
	$(NULL)

CLEANFILES += \
	$(nodist_pkgconfig_DATA) \
	$(NULL)
