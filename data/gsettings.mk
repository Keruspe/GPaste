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

gsettings_SCHEMAS = data/org.gnome.GPaste.gschema.xml

@GSETTINGS_RULES@
@INTLTOOL_XML_NOMERGE_RULE@

gschemas.compiled: $(gsettings_SCHEMAS:.xml=.valid)
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --targetdir=. .

EXTRA_DIST += $(gsettings_SCHEMAS:.xml=.xml.in.in)

CLEANFILES += \
	$(gsettings_SCHEMAS:.xml=.xml.in) \
	$(gsettings_SCHEMAS) \
	gschemas.compiled \
	$(NULL)
