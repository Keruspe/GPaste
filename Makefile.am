DISTCHECK_CONFIGURE_FLAGS = --enable-applet --enable-gnome-shell-extension

ACLOCAL_AMFLAGS  = -I m4 ${ACLOCAL_FLAGS}

CLEANFILES = 

SUBDIRS = po
EXTRA_DIST = autogen.bash NEWS README

bin_PROGRAMS = gpaste

pkglibexec_PROGRAMS = gpasted gpaste-settings

gpasted_SOURCES = \
	src/gpasted/clipboard.vala \
	src/gpasted/gpasted.vala \
	src/gpasted/history.vala \
	src/gpasted/item.vala \
	src/gpasted/keybinder.vala \
	src/gpasted/settings.vala

gpasted_VALAFLAGS = \
	--vapidir=vapi \
	$(GTK_VALAFLAGS) \
	$(GDK_VALAFLAGS) \
	$(GIO_VALAFLAGS) \
	--pkg=config \
	--pkg=posix \
	$(VALAFLAGS)

gpasted_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GDK_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	$(CFLAGS)

CLEANFILES += \
	${gpasted_SOURCES:.vala=.c}

gpasted_LDADD = \
	$(GTK_LIBS) \
	$(GDK_LIBS) \
	$(GIO_LIBS) \
	$(GLIB_LIBS)

gpaste_SOURCES = \
	src/gpaste.vala

gpaste_VALAFLAGS = \
	--vapidir=vapi \
	$(GIO_VALAFLAGS) \
	--pkg=config \
	--pkg=posix \
	$(VALAFLAGS)

gpaste_CFLAGS = \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	-DPKGLIBEXECDIR=\"${pkglibexecdir}\" \
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
	--pkg=config \
	$(VALAFLAGS)

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

applicationsdir=$(datadir)/applications
nodist_applications_DATA = gpaste-settings.desktop

data/gpaste-settings.desktop.in: data/gpaste-settings.desktop.in.in
	$(AM_V_GEN) sed "s|@pkglibexecdir[@]|$(pkglibexecdir)|" < $< > $@ || rm $@
gpaste-settings.desktop: data/gpaste-settings.desktop.in
	$(AM_V_GEN) sed s/^_// < $< > $@ || rm $@

gpaste.1_parts = data/gpaste.1.part

if ENABLE_APPLET

gpaste_VALAFLAGS += --define=ENABLE_APPLET

pkglibexec_PROGRAMS += gpaste-applet

autostartdir=$(sysconfdir)/xdg/autostart
nodist_autostart_DATA = gpaste-applet.desktop

data/gpaste-applet.desktop.in: data/gpaste-applet.desktop.in.in
	$(AM_V_GEN) sed "s|@pkglibexecdir[@]|$(pkglibexecdir)|" < $< > $@ || rm $@
gpaste-applet.desktop: data/gpaste-applet.desktop.in
	$(AM_V_GEN) sed s/^_// < $< > $@ || rm $@

gpaste_applet_SOURCES = \
	src/applets/legacy/gpaste-applet.vala

gpaste_applet_VALAFLAGS = \
	--vapidir=vapi \
	$(GTK_VALAFLAGS) \
	$(GIO_VALAFLAGS) \
	--pkg=posix \
	--pkg=config \
	$(VALAFLAGS)

gpaste_applet_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GLIB_CFLAGS) \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	-DPKGLIBEXECDIR=\"${pkglibexecdir}\" \
	$(CFLAGS)

gpaste_applet_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS)

EXTRA_DIST += data/gpaste-applet.desktop.in.in \
	      data/gpaste-settings.desktop.in.in

CLEANFILES += \
	data/gpaste-applet.desktop.in \
	gpaste-applet.desktop \
	data/gpaste-settings.desktop.in \
	gpaste-settings.desktop \
	${gpaste_applet_SOURCES:.vala=.c}

gpaste.1_parts += data/gpaste-applet.1.part

endif

gpastemandir = $(datadir)/man/man1
nodist_gpasteman_DATA = gpaste.1
EXTRA_DIST += data/gpaste.1.part data/gpaste-applet.1.part
CLEANFILES += gpaste.1
gpaste.1: $(gpaste.1_parts)
	$(AM_V_GEN) cat $^ > $@

if ENABLE_EXTENSION

gnomeshelldir = $(datadir)/gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org

dist_gnomeshell_DATA = \
	src/applets/gnome-shell/extension.js \
	src/applets/gnome-shell/stylesheet.css

nodist_gnomeshell_DATA = \
	src/applets/gnome-shell/metadata.json

src/applets/gnome-shell/metadata.json: src/applets/gnome-shell/metadata.json.in
	@ mkdir -p src/applets/gnome-shell
	$(AM_V_GEN) sed -e 's,[@]localedir[@],$(localedir),g' \
		-e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' <$< >$@

EXTRA_DIST += \
	src/applets/gnome-shell/metadata.json.in

CLEANFILES += \
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
	$(AM_V_GEN) sed -e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' <$^ >$@

EXTRA_DIST += data/org.gnome.GPaste.service.in

CLEANFILES += $(service_DATA)

noinst_DATA = completions/_gpaste completions/gpaste
EXTRA_DIST += $(noinst_DATA)

# Generate a ChangeLog file from 'git log'
dist-hook:
	@if test -d "$(srcdir)/.git"; \
        then \
                echo Creating ChangeLog && \
                ( cd "$(top_srcdir)" && \
                  echo '# Generated by Makefile. Do not edit.'; echo; \
                  $(top_srcdir)/.aux/missing --run git log --stat \
                ) > ChangeLog.tmp \
                && mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
                || ( rm -f ChangeLog.tmp ; \
                     echo Failed to generate ChangeLog >&2 ); \
        else \
                echo A git clone is required to generate a ChangeLog >&2; \
        fi
