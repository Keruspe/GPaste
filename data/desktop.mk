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

autostartdir = $(sysconfdir)/xdg/autostart
applicationsdir = $(datadir)/applications

gpaste_applet_desktop_file = data/desktop/org.gnome.GPaste.Applet.desktop
gpaste_app_indicator_desktop_file = data/desktop/org.gnome.GPaste.AppIndicator.desktop
gpaste_settings_desktop_file = data/desktop/org.gnome.GPaste.Settings.desktop

all_desktop_files = \
	$(gpaste_applet_desktop_file) \
	$(gpaste_app_indicator_desktop_file) \
	$(gpaste_settings_desktop_file) \
	$(NULL)

nodist_autostart_DATA = \
	$(NULL)

if ENABLE_APPLET
nodist_autostart_DATA += \
	$(gpaste_applet_desktop_file) \
	$(NULL)
endif

if ENABLE_UNITY
nodist_autostart_DATA += \
	$(gpaste_app_indicator_desktop_file) \
	$(NULL)
endif

nodist_applications_DATA = \
	$(gpaste_settings_desktop_file) \
	$(nodist_autostart_DATA) \
	$(NULL)

@INTLTOOL_DESKTOP_RULE@

SUFFIXES += .desktop.in.in .desktop.in
.desktop.in.in.desktop.in:
	@ $(MKDIR_P) data/desktop
	$(AM_V_GEN) $(SED) -e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' < $< > $@

EXTRA_DIST += \
	$(all_desktop_files:.desktop=.desktop.in.in) \
	$(NULL)

CLEANFILES += \
	$(all_desktop_files) \
	$(all_desktop_files:.desktop=.desktop.in) \
	$(NULL)
