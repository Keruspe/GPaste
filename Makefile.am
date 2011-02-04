ACLOCAL_AMFLAGS  = -I m4 ${ACLOCAL_FLAGS}

CP = cp

CLEANFILES = 

SUBDIRS = po
EXTRA_DIST = autogen.bash

bin_PROGRAMS = gpaste

libexec_PROGRAMS = gpasted gpaste-preferences

desktopdir=$(datadir)/applications
desktop_DATA = gpaste-preferences.desktop

data/gpaste-preferences.desktop.in: data/gpaste-preferences.desktop.in.in
	$(AM_V_GEN) sed "s|@bindir[@]|$(bindir)|" < $< > $@ || rm $@
gpaste-preferences.desktop: data/gpaste-preferences.desktop.in
	$(AM_V_GEN) sed s/^_// < $< > $@ || rm $@

EXTRA_DIST += data/gpaste-preferences.desktop.in.in

CLEANFILES += \
	data/gpaste-preferences.desktop.in \
	$(desktop_DATA)

gpasted_SOURCES = \
	src/gpasted/clipboard.vala \
	src/gpasted/history.vala \
	src/gpasted/settings.vala \
	src/gpasted/gpasted.vala

gpasted_VALAFLAGS = \
	--vapidir=vapi \
	$(GTK_VALAFLAGS) \
	$(GIO_VALAFLAGS) \
	--pkg=config \
	--pkg=posix

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
	--vapidir=vapi \
	$(GIO_VALAFLAGS) \
	--pkg=config \
	--pkg=posix

gpaste_CFLAGS = \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	-DLIBEXECDIR=\"${libexecdir}\" \
	$(CFLAGS)

gpaste_LDADD = \
	$(GIO_LIBS) \
	$(GLIB_LIBS)

CLEANFILES += \
	${gpaste_SOURCES:.vala=.c}

gpaste_preferences_SOURCES = \
	src/gpaste-preferences/gpaste-preferences.vala

gpaste_preferences_VALAFLAGS = \
	--vapidir=vapi \
	$(GTK_VALAFLAGS) \
	--pkg=config

gpaste_preferences_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	$(CFLAGS)

gpaste_preferences_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS)

CLEANFILES += \
	${gpaste_preferences_SOURCES:.vala=.c}

if ENABLE_APPLET

libexec_PROGRAMS += gpaste-applet

autostartdir=$(sysconfdir)/xdg/autostart
autostart_DATA = gpaste-applet.desktop

data/gpaste-applet.desktop.in: data/gpaste-applet.desktop.in.in
	$(AM_V_GEN) sed "s|@libexecdir[@]|$(libexecdir)|" < $< > $@ || rm $@
gpaste-applet.desktop: data/gpaste-applet.desktop.in
	$(AM_V_GEN) sed s/^_// < $< > $@ || rm $@

gpaste_applet_SOURCES = \
	src/applet/gpaste-applet.vala

gpaste_applet_VALAFLAGS = \
	--vapidir=vapi \
	$(GTK_VALAFLAGS) \
	$(GIO_VALAFLAGS) \
	--pkg=posix \
	--pkg=config

gpaste_applet_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	-DLIBEXECDIR=\"${libexecdir}\" \
	$(CFLAGS)

gpaste_applet_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS)

EXTRA_DIST += data/gpaste-applet.desktop.in.in

CLEANFILES += \
	data/gpaste-applet.desktop.in \
	gpaste-applet.desktop \
	${gpaste_applet_SOURCES:.vala=.c}

endif

gsettings_SCHEMAS = data/org.gnome.GPaste.gschema.xml

@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@

gschemas.compiled: $(gsettings_SCHEMAS:.xml=.valid)
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --targetdir=. .

EXTRA_DIST += data/org.gnome.GPaste.gschema.xml.in.in

CLEANFILES += \
	$(gsettings_SCHEMAS) \
	$(gsettings_SCHEMAS:.xml=.xml.in) \
	gschemas.compiled

servicedir = $(datadir)/dbus-1/services
service_DATA = data/org.gnome.GPaste.service

data/org.gnome.GPaste.service: data/org.gnome.GPaste.service.in
	$(AM_V_GEN) sed -e 's,[@]libexecdir[@],$(libexecdir),g' <$^ >$@

EXTRA_DIST += data/org.gnome.GPaste.service.in

CLEANFILES += $(service_DATA)

noinst_DATA = completions/_gpaste completions/gpaste
EXTRA_DIST += $(noinst_DATA)

gpastemandir = $(datadir)/man/man1
gpasteman_DATA = gpaste.1

gpaste.1: data/gpaste.pod
	$(POD2MAN) -u --section=1 --release=$(PACKAGE_VERSION) --center '' $^ $@

CLEANFILES += $(gpasteman_DATA)
EXTRA_DIST += data/gpaste.pod

