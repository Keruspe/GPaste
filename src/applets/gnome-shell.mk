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

gnomeshelldir = $(datadir)/gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org

gnomeshell_extension_files =                 \
	src/applets/gnome-shell/aboutItem.js \
	src/applets/gnome-shell/deleteButton.js \
	src/applets/gnome-shell/deleteItemPart.js \
	src/applets/gnome-shell/dummyHistoryItem.js \
	src/applets/gnome-shell/emptyHistoryItem.js \
	src/applets/gnome-shell/extension.js \
	src/applets/gnome-shell/indicator.js \
	src/applets/gnome-shell/item.js \
	src/applets/gnome-shell/prefs.js \
	src/applets/gnome-shell/searchItem.js \
	src/applets/gnome-shell/stateSwitch.js \
	src/applets/gnome-shell/statusIcon.js \
	src/applets/gnome-shell/stylesheet.css \
	$(NULL)

gnomeshell_metadata_file = src/applets/gnome-shell/metadata.json

SUFFIXES += .json .json.in
.json.in.json:
	@ $(MKDIR_P) src/applets/gnome-shell
	$(AM_V_GEN) $(SED) -e 's,[@]localedir[@],$(localedir),g' \
			   -e 's,[@]gettext_package[@],$(GETTEXT_PACKAGE),g' \
			   < $< > $@

if ENABLE_EXTENSION
nodist_gnomeshell_DATA = \
	$(gnomeshell_extension_files) \
	$(gnomeshell_metadata_file)   \
	$(NULL)

CLEANFILES += \
	$(gnomeshell_metadata_file) \
	$(NULL)
endif

EXTRA_DIST += \
	$(gnomeshell_extension_files)              \
	$(gnomeshell_metadata_file:.json=.json.in) \
	$(NULL)
