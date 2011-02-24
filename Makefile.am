DISTCHECK_CONFIGURE_FLAGS = --enable-applet --enable-gnome-shell-extension

ACLOCAL_AMFLAGS  = -I m4 ${ACLOCAL_FLAGS}

CP = cp

CLEANFILES = 

SUBDIRS = po
EXTRA_DIST = autogen.bash NEWS README

bin_PROGRAMS = gpaste

gpasteexecdir = $(libexecdir)/gpaste
gpasteexec_PROGRAMS = gpasted gpaste-settings

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
	-DGPASTEEXECDIR=\"${gpasteexecdir}\" \
	$(CFLAGS)

gpaste_LDADD = \
	$(GIO_LIBS) \
	$(GLIB_LIBS)

CLEANFILES += \
	${gpaste_SOURCES:.vala=.c}

gpaste_settings_SOURCES = \
	src/gpaste-settings/gpaste-settings.vala

gpaste_settings_VALAFLAGS = \
	--vapidir=vapi \
	$(GTK_VALAFLAGS) \
	--pkg=config

gpaste_settings_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	$(CFLAGS)

gpaste_settings_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS)

CLEANFILES += \
	${gpaste_settings_SOURCES:.vala=.c}

if ENABLE_APPLET

gpasteexec_PROGRAMS += gpaste-applet

autostartdir=$(sysconfdir)/xdg/autostart
nodist_autostart_DATA = gpaste-applet.desktop

data/gpaste-applet.desktop.in: data/gpaste-applet.desktop.in.in
	$(AM_V_GEN) sed "s|@gpasteexecdir[@]|$(gpasteexecdir)|" < $< > $@ || rm $@
gpaste-applet.desktop: data/gpaste-applet.desktop.in
	$(AM_V_GEN) sed s/^_// < $< > $@ || rm $@

gpaste_applet_SOURCES = \
	src/applets/standard/gpaste-applet.vala

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
	-DGPASTEEXECDIR=\"${gpasteexecdir}\" \
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

if ENABLE_EXTENSION

gnomeshelldir = $(datadir)/gnome-shell/extensions/GPaste@gnome.org

dist_gnomeshell_DATA = \
	src/applets/gnome-shell/stylesheet.css

nodist_gnomeshell_DATA = \
	src/applets/gnome-shell/extension.js \
	src/applets/gnome-shell/metadata.json

extensiondir:
	@ mkdir -p src/applets/gnome-shell

src/applets/gnome-shell/extension.js: src/applets/gnome-shell/extension.js.in extensiondir
	$(AM_V_GEN) sed -e 's,[@]libexecdir[@],$(libexecdir),g' <$< >$@
src/applets/gnome-shell/metadata.json: src/applets/gnome-shell/metadata.json.in extensiondir
	$(AM_V_GEN) sed -e 's,[@]localedir[@],$(localedir),g' <$< >$@

EXTRA_DIST += \
	src/applets/gnome-shell/extension.js.in \
	src/applets/gnome-shell/metadata.json.in

CLEANFILES += \
	src/applets/gnome-shell/extension.js \
	src/applets/gnome-shell/metadata.json

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
	$(AM_V_GEN) sed -e 's,[@]gpasteexecdir[@],$(gpasteexecdir),g' <$^ >$@

EXTRA_DIST += data/org.gnome.GPaste.service.in

CLEANFILES += $(service_DATA)

noinst_DATA = completions/_gpaste completions/gpaste
EXTRA_DIST += $(noinst_DATA)

gpastemandir = $(datadir)/man/man1
gpasteman_DATA = gpaste.1

gpaste.1: data/gpaste.pod
	$(AM_V_GEN) $(POD2MAN) -u --section=1 --release=$(PACKAGE_VERSION) --center '' $^ $@

CLEANFILES += $(gpasteman_DATA)
EXTRA_DIST += data/gpaste.pod

