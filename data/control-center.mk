## This file is part of GPaste.
##
## Copyright (c) 2010-2017, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

controlcenter_DATA =                     \
	%D%/control-center/42-gpaste.xml \
	$(NULL)

SUFFIXES += .control-center.xml.in .xml
.control-center.xml.in.xml:
	@ $(MKDIR_P) $(@D)
	$(AM_V_GEN) $(SED) -e 's,[@]GETTEXT_PACKAGE[@],$(GETTEXT_PACKAGE),g' < $< > $@

EXTRA_DIST+=                                              \
	$(controlcenter_DATA:.xml=.control-center.xml.in) \
	$(NULL)

CLEANFILES+=                  \
	$(controlcenter_DATA) \
	$(NULL)
