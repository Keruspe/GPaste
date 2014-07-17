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

gpaste_daemon_binary = bin/gpaste-daemon

pkglibexec_PROGRAMS += \
	$(gpaste_daemon_binary) \
	$(NULL)

$(gpaste_daemon_binary): $(libgpaste_daemon_la_file)

bin_gpaste_daemon_SOURCES = \
	src/daemon/gpaste-daemon.c \
	$(NULL)

bin_gpaste_daemon_CFLAGS = \
	-I$(srcdir)/libgpaste/daemon/ \
	$(AM_CFLAGS) \
	$(NULL)

bin_gpaste_daemon_LDADD = \
	$(builddir)/$(libgpaste_core_la_file) \
	$(builddir)/$(libgpaste_daemon_la_file) \
	$(builddir)/$(libgpaste_gnome_shell_client_la_file) \
	$(builddir)/$(libgpaste_keybinder_la_file) \
	$(builddir)/$(libgpaste_settings_la_file) \
	$(GTK_LIBS) \
	$(AM_LIBS) \
	$(NULL)
