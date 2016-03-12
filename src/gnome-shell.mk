## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

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
