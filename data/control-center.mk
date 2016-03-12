## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

SUFFIXES += .xml.in .xml.in.in
.xml.in.in.xml.in:
	@ $(MKDIR_P) $(@D)
	@ cp $< $@

nodist_controlcenter_DATA =              \
	%D%/control-center/42-gpaste.xml \
	$(NULL)

EXTRA_DIST +=                                        \
	$(nodist_controlcenter_DATA:.xml=.xml.in.in) \
	$(NULL)

CLEANFILES +=                                     \
	$(nodist_controlcenter_DATA)              \
	$(nodist_controlcenter_DATA:.xml=.xml.in) \
	$(NULL)

