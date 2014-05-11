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

nodist_dbusservices_DATA = \
	data/dbus/org.gnome.GPaste.service \
	data/dbus/org.gnome.GPaste.Settings.service \
	$(NULL)

if ENABLE_APPLET
nodist_dbusservices_DATA += \
	data/dbus/org.gnome.GPaste.Applet.service \
	$(NULL)
endif

if ENABLE_UNITY
nodist_dbusservices_DATA += \
	data/dbus/org.gnome.GPaste.AppIndicator.service \
	$(NULL)
endif

SUFFIXES += .service .dbus.in
.dbus.in.service:
	@ $(MKDIR_P) data/dbus
	$(AM_V_GEN) $(SED) \
	    -e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' \
	    <$^ >$@

EXTRA_DIST += \
	$(nodist_dbusservices_DATA:.service=.dbus.in) \
	$(NULL)

CLEANFILES += \
	$(nodist_dbusservices_DATA) \
	$(NULL)
