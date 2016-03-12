## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

@APPSTREAM_XML_RULES@

appstream_in_files =                                     \
	%D%/appstream/org.gnome.GPaste.Ui.appdata.xml.in \
	$(NULL)

if ENABLE_APPLET
appstream_in_files +=                                        \
	%D%/appstream/org.gnome.GPaste.Applet.appdata.xml.in \
	$(NULL)
endif

if ENABLE_UNITY
appstream_in_files +=                                              \
	%D%/appstream/org.gnome.GPaste.AppIndicator.appdata.xml.in \
	$(NULL)
endif

appstream_XML = $(appstream_in_files:.xml.in=.xml)

SUFFIXES += .appdata.xml.in .appdata.xml
.appdata.xml.in.appdata.xml:
	@ $(MKDIR_P) $(@D)
	$(AM_V_GEN) $(MSGFMT) --xml --template $< -o $@ -d $(top_builddir)/po/

EXTRA_DIST +=                 \
	$(appstream_in_files) \
	$(NULL)

CLEANFILES +=            \
	$(appstream_XML) \
	$(NULL)
