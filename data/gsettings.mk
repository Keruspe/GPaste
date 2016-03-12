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

EXTRA_DIST +=                  \
	$(gpaste_gschema_file) \
	$(NULL)

CLEANFILES +=                \
	$(gschemas_compiled) \
	$(NULL)
