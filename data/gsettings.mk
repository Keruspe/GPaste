## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

gpaste_gschema_file = %D%/gsettings/org.gnome.GPaste.gschema.xml
gschemas_compiled = %D%/gsettings/gschemas.compiled

gsettings_SCHEMAS =            \
	$(gpaste_gschema_file) \
	$(NULL)

@GSETTINGS_RULES@

$(gpaste_gschema_file:.xml=.valid): $(gpaste_gschema_file)
	@ $(MKDIR_P) $(@D)

$(gschemas_compiled): $(gsettings_SCHEMAS:.xml=.valid)
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --targetdir=$(srcdir) .

SUFFIXES += .gschema.xml.in .gschema.xml
.gschema.xml.in.gschema.xml:
	@ $(MKDIR_P) $(@D)
	$(AM_V_GEN) $(SED) -e 's,[@]GETTEXT_PACKAGE[@],$(GETTEXT_PACKAGE),g' < $< > $@

EXTRA_DIST +=                               \
	$(gpaste_gschema_file:.xml=.xml.in) \
	$(NULL)

CLEANFILES +=                  \
	$(gpaste_gschema_file) \
	$(gschemas_compiled)   \
	$(NULL)
