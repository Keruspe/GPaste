# This file is part of GPaste.
#
# Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

gpaste_extension_files = \
	src/extension/aboutItem.js \
	src/extension/deleteButton.js \
	src/extension/deleteItemPart.js \
	src/extension/dummyHistoryItem.js \
	src/extension/emptyHistoryItem.js \
	src/extension/indicator.js \
	src/extension/item.js \
	src/extension/searchItem.js \
	src/extension/stateSwitch.js \
	src/extension/statusIcon.js \
	src/extension/uiItem.js \
	$(NULL)

gpaste_extension_metadata_file = src/extension/metadata.json

SUFFIXES += .json .json.in
.json.in.json:
	@ $(MKDIR_P) src/extension
	$(AM_V_GEN) $(SED) -e 's,[@]localedir[@],$(localedir),g' \
			   -e 's,[@]gettext_package[@],$(GETTEXT_PACKAGE),g' \
			   -e 's,[@]version[@],$(VERSION),g' \
			   < $< > $@

CLEANFILES += \
	$(gpaste_extension_metadata_file) \
	$(NULL)

EXTRA_DIST += \
	$(gpaste_extension_files)              \
	$(gpaste_extension_metadata_file:.json=.json.in) \
	$(NULL)
