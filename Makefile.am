# This file is part of GPaste.
#
# Copyright 2011 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
#
# GPaste is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# GPaste is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GPaste.  If not, see <http://www.gnu.org/licenses/>.

DISTCHECK_CONFIGURE_FLAGS = --enable-applet
ACLOCAL_AMFLAGS  = -I m4 ${ACLOCAL_FLAGS}
SUBDIRS = po

# Directories

applicationsdir = $(datadir)/applications
autostartdir = $(sysconfdir)/xdg/autostart
gnomeshelldir = $(datadir)/gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org

# Data stuff

gsettings_SCHEMAS = data/org.gnome.GPaste.gschema.xml

dist_dbusservices_in_in_files = data/org.gnome.GPaste.service.in.in
nodist_dbusservices_DATA = $(dist_dbusservices_in_in_files:.service.in.in=.service)
%.service: %.service.in
	$(AM_V_GEN) sed -e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' <$^ >$@

noinst_DATA = data/completions/_gpaste \
	      data/completions/gpaste

nodist_man_MANS = man/gpaste.1
gpaste_1_parts = data/gpaste.1.part
man/gpaste.1: $(gpaste_1_parts)
	$(AM_V_GEN) $(MKDIR_P) man && cat $^ > $@

%.desktop.in: %.desktop.in.in
	$(AM_V_GEN) sed "s|@pkglibexecdir[@]|$(pkglibexecdir)|" < $< > $@ || rm $@

if ENABLE_APPLET
gpaste_1_parts += data/gpaste-applet.1.part
nodist_autostart_in_files = data/gpaste-applet.desktop.in
nodist_autostart_DATA = $(nodist_autostart_in_files:.desktop.in=.desktop)
endif

nodist_applications_in_files = data/gpaste-settings.desktop.in
nodist_applications_DATA = $(nodist_applications_in_files:.desktop.in=.desktop)

if ENABLE_EXTENSION
dist_gnomeshell_DATA = \
	src/applets/gnome-shell/extension.js \
	src/applets/gnome-shell/stylesheet.css
nodist_gnomeshell_DATA = src/applets/gnome-shell/metadata.json

%.json: %.json.in
	$(AM_V_GEN) $(MKDIR_P) src/applets/gnome-shell && \
	    sed -e 's,[@]localedir[@],$(localedir),g' \
		-e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' <$< >$@
endif

# Binaries

bin_PROGRAMS = gpaste
pkglibexec_PROGRAMS = gpasted gpaste-settings
if ENABLE_APPLET
pkglibexec_PROGRAMS += gpaste-applet
endif

# Sources

gpaste_SOURCES = \
	src/gpaste.vala

gpasted_SOURCES = \
	src/gpasted/clipboard.vala \
	src/gpasted/gpasted.vala \
	src/gpasted/history.vala \
	src/gpasted/item.vala \
	src/gpasted/keybinder.vala \
	src/gpasted/settings.vala

gpaste_applet_SOURCES = \
	src/applets/legacy/gpaste-applet.vala

gpaste_settings_SOURCES = \
	src/gpaste-settings/gpaste-settings.vala

# VALAFLAGS

AM_VALAFLAGS = \
	--vapidir=vapi \
	--pkg=config \
	--pkg=posix \
	$(GIO_VALAFLAGS)

gpaste_VALAFLAGS = \
	$(AM_VALAFLAGS)

gpasted_VALAFLAGS = \
	$(GTK_VALAFLAGS) \
	$(GDK_VALAFLAGS) \
	$(AM_VALAFLAGS)

gpaste_applet_VALAFLAGS = \
	$(GTK_VALAFLAGS) \
	$(AM_VALAFLAGS)

gpaste_settings_VALAFLAGS = \
	$(GTK_VALAFLAGS) \
	$(AM_VALAFLAGS)

if ENABLE_APPLET
gpaste_VALAFLAGS += \
	--define=ENABLE_APPLET
endif

# CFLAGS

AM_CFLAGS = \
	--include config.h \
	-DLOCALEDIR=\"${localedir}\" \
	-DPKGLIBEXECDIR=\"${pkglibexecdir}\" \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS)

gpaste_CFLAGS = \
	$(AM_CFLAGS)

gpasted_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GDK_CFLAGS) \
	$(X11_CFLAGS) \
	$(AM_CFLAGS)

gpaste_applet_CFLAGS = \
	$(GTK_CFLAGS) \
	$(AM_CFLAGS)

gpaste_settings_CFLAGS = \
	$(GTK_CFLAGS) \
	$(AM_CFLAGS)

# LDADD

AM_LIBS = \
	$(GLIB_LIBS) \
	$(GIO_LIBS)

gpaste_LDADD = \
	$(AM_LIBS)

gpasted_LDADD = \
	$(GTK_LIBS) \
	$(GDK_LIBS) \
	$(X11_LIBS) \
	$(AM_LIBS)

gpaste_settings_LDADD = \
	$(GTK_LIBS) \
	$(AM_LIBS)

gpaste_applet_LDADD = \
	$(GTK_LIBS) \
	$(AM_LIBS)

# Distribution stuff

EXTRA_DIST = \
	$(noinst_DATA) \
	autogen.bash FIXME \
	data/org.gnome.GPaste.gschema.xml.in.in \
	data/gpaste-applet.desktop.in.in \
	data/gpaste-settings.desktop.in.in \
	data/gpaste.1.part data/gpaste-applet.1.part \
	src/applets/gnome-shell/metadata.json.in

CLEANFILES = \
	${gpaste_SOURCES:.vala=.c} \
	${gpasted_SOURCES:.vala=.c} \
	${gpaste_applet_SOURCES:.vala=.c} \
	${gpaste_settings_SOURCES:.vala=.c} \
	$(nodist_applications_in_files) \
	$(nodist_applications_DATA) \
	$(nodist_autostart_in_files) \
	$(nodist_autostart_DATA) \
	$(nodist_man_MANS) \
	$(gsettings_SCHEMAS:.xml=.xml.in) \
	$(gsettings_SCHEMAS) \
	$(nodist_dbusservices_DATA) \
	src/applets/gnome-shell/metadata.json \
	gschemas.compiled

# Special rules

@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_XML_NOMERGE_RULE@
@GSETTINGS_RULES@

gschemas.compiled: $(gsettings_SCHEMAS:.xml=.valid)
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --targetdir=. .

update-po:
	$(MAKE) -C po $@

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
