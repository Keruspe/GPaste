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

if ENABLE_APPLET
pkglibexec_PROGRAMS += \
	bin/gpaste-applet \
	$(NULL)

nodist_bin_gpaste_applet_SOURCES = \
	$(libgpaste_vapi_file) \
	$(NULL)

bin_gpaste_applet_SOURCES = \
	src/applets/legacy/gpaste-client.vala \
	src/applets/legacy/gpaste-applet.vala \
	$(NULL)

bin_gpaste_applet_vala.stamp: $(libgpaste_vapi_file)

bin_gpaste_applet_VALAFLAGS = \
	$(libgpaste_vapi_file) \
	$(AM_VALAFLAGS) \
	$(NULL)

bin_gpaste_applet_CFLAGS = \
	$(AM_CFLAGS) \
	$(VALA_CFLAGS) \
	$(NULL)

bin_gpaste_applet_LDADD = \
	$(GDK_LIBS) \
	$(GTK_LIBS) \
	$(AM_LIBS) \
	$(libgpaste_la_file) \
	$(NULL)

CLEANFILES += \
	$(gpaste_applet_SOURCES:.vala=.c) \
	$(NULL)
endif
