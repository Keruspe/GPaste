# This file is part of GPaste.
#
# Copyright 2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

@APPDATA_XML_RULES@
@INTLTOOL_XML_RULE@

appdata_in_files = \
	data/appdata/org.gnome.GPaste.Settings.appdata.xml.in \
	$(NULL)

if ENABLE_APPLET
appdata_in_files += \
	data/appdata/org.gnome.GPaste.Applet.appdata.xml.in \
	$(NULL)
endif

if ENABLE_UNITY
appdata_in_files += \
	data/appdata/org.gnome.GPaste.AppIndicator.appdata.xml.in \
	$(NULL)
endif

appdata_XML = $(appdata_in_files:.xml.in=.xml)

EXTRA_DIST += \
	$(appdata_in_files) \
	$(NULL)

CLEANFILES += \
	$(appdata_XML) \
	$(NULL)
