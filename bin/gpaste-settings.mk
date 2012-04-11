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

pkglibexec_PROGRAMS += \
	bin/gpaste-settings \
	$(NULL)

bin_gpaste_settings_SOURCES = \
	src/gpaste-settings/gpaste-settings.vala \
	$(libgpaste_vapi_file) \
	$(NULL)

bin_gpaste_settings_VALAFLAGS = \
	$(GTK_VALAFLAGS) \
	$(AM_VALAFLAGS) \
	$(NULL)

bin_gpaste_settings_CFLAGS = \
	$(GTK_CFLAGS) \
	$(AM_CFLAGS) \
	$(VALA_CFLAGS) \
	$(NULL)

bin_gpaste_settings_LDADD = \
	$(GTK_LIBS) \
	$(AM_LIBS) \
	$(libgpaste_la_file) \
	$(NULL)

bin_gpaste_settings_desktop_in_file = data/gpaste-settings.desktop.in

nodist_applications_DATA += \
	$(bin_gpaste_settings_desktop_in_file:.desktop.in=.desktop) \
	$(NULL)

EXTRA_DIST += \
	$(bin_gpaste_settings_desktop_in_file:.desktop.in=.desktop.in.in) \
	$(NULL)

CLEANFILES += \
	$(gpaste_settings_SOURCES:.vala=.c) \
	$(NULL)
