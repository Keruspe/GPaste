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

if ENABLE_SYSTEMD

nodist_systemduserunit_DATA += \
	data/systemd/gpasted.service \
	$(NULL)

endif

SUFFIXES += .service .systemd.in
.systemd.in.service:
	@ $(MKDIR_P) data/systemd
	$(AM_V_GEN) $(SED) \
	    -e 's:[@]pkglibexecdir[@]:$(pkglibexecdir):g' \
	    -e 's:[@]PACKAGE_NAME[@]:$(PACKAGE_NAME):g' \
	    < $< > $@

EXTRA_DIST += \
	$(nodist_systemduserunit_DATA:.service=.systemd.in) \
	$(NULL)

CLEANFILES += \
	$(nodist_systemduserunit_DATA) \
	$(NULL)
