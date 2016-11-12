## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

nodist_dbusservices_DATA =                   \
	%D%/dbus/org.gnome.GPaste.service    \
	%D%/dbus/org.gnome.GPaste.Ui.service \
	$(NULL)

SUFFIXES += .service .dbus.in
.dbus.in.service:
	@ $(MKDIR_P) $(@D)
	$(AM_V_GEN) $(SED)                                \
	    -e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' \
	    <$< >$@

EXTRA_DIST +=                                         \
	$(nodist_dbusservices_DATA:.service=.dbus.in) \
	$(NULL)

CLEANFILES +=                       \
	$(nodist_dbusservices_DATA) \
	$(NULL)
