ACLOCAL_AMFLAGS  = -I m4 ${ACLOCAL_FLAGS}

CP = cp

CLEANFILES = 

SUBDIRS = po

bin_PROGRAMS = gpaste

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
	$(GIO_VALAFLAGS) \
	$(GLIB_VALAFLAGS) \
	--pkg posix

gpaste_CFLAGS = \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	$(CFLAGS)

gpaste_LDADD = \
	$(GIO_LIBS) \
	$(GLIB_LIBS)

CLEANFILES += \
	${gpaste_SOURCES:.vala=.c}

gsettings_SCHEMAS = data/gpaste.gschema.xml

@GSETTINGS_RULES@

data/gpaste.gschema.xml : data/gpaste.gschema.xml.in
	$(AM_V_GEN) $(CP) $^ $@

gschemas.compiled: $(gsettings_SCHEMAS:.xml=.valid)
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --targetdir=. .

EXTRA_DIST = gpaste.gschema.xml.in

CLEANFILES += \
	$(gsettings_SCHEMAS) \
	gschemas.compiled
