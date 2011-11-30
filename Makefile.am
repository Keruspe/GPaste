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

DISTCHECK_CONFIGURE_FLAGS = \
	--with-dbusservicesdir=$$dc_install_base/$(dbusservicesdir) \
	--enable-applet
ACLOCAL_AMFLAGS  = -I m4 ${ACLOCAL_FLAGS}
AM_MAKEFLAGS = --no-print-directory
SUBDIRS = po

LIBGPASTE_CURRENT=0
LIBGPASTE_REVISION=0
LIBGPASTE_AGE=0

# Directories

applicationsdir = $(datadir)/applications
autostartdir = $(sysconfdir)/xdg/autostart
gnomeshelldir = $(datadir)/gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org

# Data stuff

gsettings_SCHEMAS = data/org.gnome.GPaste.gschema.xml

nodist_dbusservices_DATA = data/org.gnome.GPaste.service

noinst_DATA = data/completions/_gpaste \
	      data/completions/gpaste

nodist_man_MANS = man/gpaste.1
gpaste_1_parts = data/gpaste.1.part
man/gpaste.1: $(gpaste_1_parts)
	$(AM_V_GEN) $(MKDIR_P) man && cat $^ > $@

nodist_applications_in_files = data/gpaste-settings.desktop.in
nodist_applications_DATA = $(nodist_applications_in_files:.desktop.in=.desktop)

nodist_autostart_in_files = 
if ENABLE_APPLET
gpaste_1_parts += data/gpaste-applet.1.part
nodist_autostart_in_files += data/gpaste-applet.desktop.in
endif

if ENABLE_EXTENSION
dist_gnomeshell_DATA = \
	src/applets/gnome-shell/extension.js
nodist_gnomeshell_DATA = src/applets/gnome-shell/metadata.json
endif

libgpaste_libgpaste_la_public_headers = libgpaste/gpaste.h libgpaste/gpaste-item.h
libgpaste_libgpaste_la_private_headers = libgpaste/gpaste-item-private.h
pkginclude_HEADERS = $(libgpaste_libgpaste_la_public_headers)
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libgpaste/gpaste.pc

# Binaries

bin_PROGRAMS = gpaste
pkglibexec_PROGRAMS = gpasted gpaste-settings
if ENABLE_APPLET
pkglibexec_PROGRAMS += gpaste-applet
endif
lib_LTLIBRARIES = libgpaste/libgpaste.la

# Sources

gpaste_SOURCES = \
	src/gpaste.vala

gpasted_SOURCES = \
	vapi/gpaste-1.0.vapi \
	src/gpasted/clipboard.vala \
	src/gpasted/gpasted.vala \
	src/gpasted/history.vala \
	src/gpasted/keybinder.vala \
	src/gpasted/settings.vala

gpaste_applet_SOURCES = \
	src/applets/legacy/gpaste-applet.vala

gpaste_settings_SOURCES = \
	src/gpaste-settings/gpaste-settings.vala

libgpaste_libgpaste_la_SOURCES = \
	libgpaste/gpaste-item.c

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
	$(XML_VALAFLAGS) \
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
	-include $(top_builddir)/config.h \
	-I$(top_srcdir)/libgpaste \
	-ffunction-sections \
	-fdata-sections \
	-DLOCALEDIR=\"${localedir}\" \
	-DPKGLIBEXECDIR=\"${pkglibexecdir}\" \
	$(GLIB_CFLAGS) \
	$(GOBJECT_CFLAGS) \
	$(GIO_CFLAGS)
#	-fvisibility=hidden

gpaste_CFLAGS = \
	$(AM_CFLAGS)

gpasted_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GDK_CFLAGS) \
	$(X11_CFLAGS) \
	$(XML_CFLAGS) \
	$(AM_CFLAGS)

gpaste_applet_CFLAGS = \
	$(GTK_CFLAGS) \
	$(AM_CFLAGS)

gpaste_settings_CFLAGS = \
	$(GTK_CFLAGS) \
	$(AM_CFLAGS)

libgpaste_libgpaste_la_CFLAGS = \
	$(GDK_PIXBUF_CFLAGS) \
	$(AM_CFLAGS)

# LDADD

AM_LIBS = \
	$(GLIB_LIBS) \
	$(GOBJECT_LIBS) \
	$(GIO_LIBS)

AM_LDFLAGS = \
	-Wl,--gc-sections \
	-Wl,--as-needed

gpaste_LDADD = \
	$(AM_LIBS)

gpasted_LDADD = \
	$(GTK_LIBS) \
	$(GDK_LIBS) \
	$(X11_LIBS) \
	$(XML_LIBS) \
	$(AM_LIBS) \
	libgpaste/libgpaste.la

gpaste_settings_LDADD = \
	$(GTK_LIBS) \
	$(AM_LIBS)

gpaste_applet_LDADD = \
	$(GTK_LIBS) \
	$(AM_LIBS)

libgpaste_libgpaste_la_LIBADD = \
	$(GDK_PIXBUF_LIBS) \
	$(AM_LIBS)

# LDFLAGS

libgpaste_libgpaste_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-version-info $(LIBGPASTE_CURRENT):$(LIBGPASTE_REVISION):$(LIBGPASTE_AGE) \
	-Wl,--version-script=$(top_srcdir)/libgpaste/libgpaste.sym
libgpaste_libgpaste_la_DEPENDENCIES = ${top_srcdir}/libgpaste/libgpaste.sym

# Distribution stuff

EXTRA_DIST = \
	$(noinst_DATA) \
	autogen.sh FIXME \
	data/org.gnome.GPaste.gschema.xml.in.in \
	data/org.gnome.GPaste.service.in \
	data/gpaste-applet.desktop.in.in \
	data/gpaste-settings.desktop.in.in \
	data/gpaste.1.part data/gpaste-applet.1.part \
	src/applets/gnome-shell/metadata.json.in \
	$(libgpaste_libgpaste_la_private_headers) \
	libgpaste/libgpaste.sym \
	vapi/config.vapi

CLEANFILES = \
	${gpaste_SOURCES:.vala=.c} \
	${gpasted_SOURCES:.vala=.c} \
	${gpaste_applet_SOURCES:.vala=.c} \
	${gpaste_settings_SOURCES:.vala=.c} \
	$(nodist_applications_in_files) \
	$(nodist_applications_DATA) \
	$(nodist_autostart_in_files) \
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

SUFFIXES = .service .service.in .json .json.in .desktop.in .desktop.in.in
.service.in.service:
	$(AM_V_GEN) $(SED) -e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' <$^ >$@
.json.in.json:
	$(AM_V_GEN) $(MKDIR_P) src/applets/gnome-shell && \
	    $(SED) -e 's,[@]localedir[@],$(localedir),g' \
		-e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' <$< >$@
.desktop.in.in.desktop.in:
	$(AM_V_GEN) $(SED) -e 's,[@]pkglibexecdir[@],$(pkglibexecdir),g' < $< > $@

gschemas.compiled: $(gsettings_SCHEMAS:.xml=.valid)
	$(AM_V_GEN) $(GLIB_COMPILE_SCHEMAS) --targetdir=. .

update-po:
	$(MAKE) -C po $@

release:
	git commit -asm "Release $(PACKAGE_NAME) $(PACKAGE_VERSION)"
	git tag -sm "Release $(PACKAGE_NAME) $(PACKAGE_VERSION)" v$(PACKAGE_VERSION)
	$(MAKE) distcheck

# Generate a ChangeLog file from 'git log'
dist-hook:
	@if test -d "$(srcdir)/.git"; \
        then \
                echo Creating ChangeLog && \
                ( cd "$(top_srcdir)" && \
                  echo '# Generated by Makefile. Do not edit.'; echo; \
                  $(top_srcdir)/.aux/missing --run git log --stat v2.0.. \
                ) > ChangeLog.tmp \
                && mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
                || ( rm -f ChangeLog.tmp ; \
                     echo Failed to generate ChangeLog >&2 ); \
        else \
                echo A git clone is required to generate a ChangeLog >&2; \
        fi

# Introspection

-include $(INTROSPECTION_MAKEFILE)

INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --c-include=gpaste.h
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

introspection_sources = \
	$(libgpaste_libgpaste_la_SOURCES) \
	$(libgpaste_libgpaste_la_public_headers) \
	$(libgpaste_libgpaste_la_private_headers)

GPaste-1.0.gir: libgpaste/libgpaste.la
GPaste_1_0_gir_INCLUDES = GdkPixbuf-2.0 GObject-2.0
GPaste_1_0_gir_CFLAGS = $(INCLUDES)
GPaste_1_0_gir_LIBS = libgpaste/libgpaste.la
GPaste_1_0_gir_SCANNERFLAGS = --warn-all
GPaste_1_0_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += GPaste-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)

# Vapi

vapi/gpaste-1.0.deps:
	$(MKDIR_P) vapi
	$(AM_V_GEN) echo -e "gdk-pixbuf-2.0\ngobject-2.0" > $@
vapi/gpaste-1.0.vapi: GPaste-1.0.gir vapi/gpaste-1.0.deps
	$(AM_V_GEN) $(VAPIGEN) --directory vapi --library gpaste-1.0 --pkg gdk-pixbuf-2.0 --pkg gobject-2.0 $<
VAPI_FILES = vapi/gpaste-1.0.vapi vapi/gpaste-1.0.deps

vapidir = $(datadir)/vala/vapi
vapi_DATA = $(VAPI_FILES)

CLEANFILES += $(VAPI_FILES)
