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

nodist_man_MANS = \
	man/gpaste.1 \
	$(NULL)

gpaste_1_parts = \
	data/gpaste.1.part \
	$(NULL)

if ENABLE_APPLET
gpaste_1_parts += \
	data/gpaste-applet.1.part \
	$(NULL)
endif

man/gpaste.1: $(gpaste_1_parts)
	@ $(MKDIR_P) man
	$(AM_V_GEN) cat $^ > $@

EXTRA_DIST += \
	data/gpaste.1.part \
	data/gpaste-applet.1.part \
	$(NULL)

CLEANFILES += \
	$(nodist_man_MANS) \
	$(NULL)
