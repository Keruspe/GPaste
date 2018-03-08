## This file is part of GPaste.
##
## Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

SUFFIXES += .pc.in .pc
.pc.in.pc:
	@ $(MKDIR_P) $(@D)
	$(AM_V_GEN) $(SED)                          \
	    -e 's,[@]libdir[@],$(libdir),g'         \
	    -e 's,[@]includedir[@],$(includedir),g' \
	    -e 's,[@]VERSION[@],$(VERSION),g'       \
	    < $< > $@

nodist_pkgconfig_DATA =              \
	%D%/pkg-config/gpaste-1.0.pc \
	$(NULL)

EXTRA_DIST +=                               \
	$(nodist_pkgconfig_DATA:.pc=.pc.in) \
	$(NULL)

CLEANFILES +=                    \
	$(nodist_pkgconfig_DATA) \
	$(NULL)
