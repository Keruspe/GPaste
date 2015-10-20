## This file is part of GPaste.
##
## Copyright 2012-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
##
## GPaste is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## GPaste is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with GPaste.  If not, see <http://www.gnu.org/licenses/>.

gnomeshelldir = $(datadir)/gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org

gnomeshell_extension_metadata_file = %D%/gnome-shell/metadata.json

gnomeshell_extension_files =                \
	%D%/gnome-shell/aboutItem.js        \
	%D%/gnome-shell/deleteButton.js     \
	%D%/gnome-shell/deleteItemPart.js   \
	%D%/gnome-shell/dummyHistoryItem.js \
	%D%/gnome-shell/emptyHistoryItem.js \
	%D%/gnome-shell/extension.js        \
	%D%/gnome-shell/indicator.js        \
	%D%/gnome-shell/item.js             \
	%D%/gnome-shell/prefs.js            \
	%D%/gnome-shell/searchItem.js       \
	%D%/gnome-shell/stateSwitch.js      \
	%D%/gnome-shell/statusIcon.js       \
	%D%/gnome-shell/uiItem.js           \
	$(NULL)

if ENABLE_GNOME_SHELL_EXTENSION
nodist_gnomeshell_DATA =                      \
	$(gnomeshell_extension_metadata_file) \
	$(gnomeshell_extension_files)         \
	$(NULL)
endif

EXTRA_DIST +=                                                \
	$(gnomeshell_extension_metadata_file:.json=.json.in) \
	$(gnomeshell_extension_files)                        \
	$(NULL)

CLEANFILES +=                                 \
	$(gnomeshell_extension_metadata_file) \
	$(NULL)

SUFFIXES += .json .json.in
.json.in.json:
	@ $(MKDIR_P) $(@D)
	$(AM_V_GEN) $(SED) -e 's,[@]localedir[@],$(localedir),g'             \
			   -e 's,[@]gettext_package[@],$(GETTEXT_PACKAGE),g' \
			   -e 's,[@]version[@],$(VERSION),g'                 \
			   < $< > $@
