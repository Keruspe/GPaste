ACLOCAL_AMFLAGS  = -I m4 ${ACLOCAL_FLAGS}

CP = cp

CLEANFILES = 

SUBDIRS = po

bin_PROGRAMS = gpaste gpaste-preferences

libexec_PROGRAMS = gpasted

gpasted_SOURCES = \
	src/gpasted/clipboard.vala \
	src/gpasted/history.vala \
	src/gpasted/settings.vala \
	src/gpasted/gpasted.vala

gpasted_VALAFLAGS = \
	--vapidir vapi \
	$(GTK_VALAFLAGS) \
	$(GIO_VALAFLAGS) \
	$(GLIB_VALAFLAGS) \
	--pkg config \
	--pkg posix

gpasted_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	$(CFLAGS)

CLEANFILES += \
	${gpasted_SOURCES:.vala=.c}

gpasted_LDADD = \
	$(GTK_LIBS) \
	$(GIO_LIBS) \
	$(GLIB_LIBS)

gpaste_SOURCES = \
	src/gpaste.vala

gpaste_VALAFLAGS = \
	--vapidir vapi \
	$(GIO_VALAFLAGS) \
	$(GLIB_VALAFLAGS) \
	--pkg config \
	--pkg posix

gpaste_CFLAGS = \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	$(CFLAGS)

gpaste_LDADD = \
	$(GIO_LIBS) \
	$(GLIB_LIBS)

CLEANFILES += \
	${gpaste_SOURCES:.vala=.c}

gpaste_preferences_SOURCES = \
	src/gpaste-preferences/gpaste-preferences.vala

gpaste_preferences_VALAFLAGS = \
	$(GTK_VALAFLAGS) \
	$(GLIB_VALAFLAGS)

gpaste_preferences_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(CFLAGS)

gpaste_preferences_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS)

CLEANFILES += \
	${gpaste_preferences_SOURCES:.vala=.c}

gsettings_SCHEMAS = data/gpaste.gschema.xml

@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@

gschemas.compiled: $(gsettings_SCHEMAS:.xml=.valid)
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --targetdir=. .

EXTRA_DIST = gpaste.gschema.xml.in

CLEANFILES += \
	$(gsettings_SCHEMAS) \
	gschemas.compiled

servicedir = $(datadir)/dbus-1/services
service_DATA = data/org.gnome.GPaste.service

data/org.gnome.GPaste.service: data/org.gnome.GPaste.service.in
	$(AM_V_GEN) sed -e 's,[@]libexecdir[@],$(libexecdir),g' <$^ >$@

EXTRA_DIST += org.gnome.GPaste.service.in

CLEANFILES += $(service_DATA)

