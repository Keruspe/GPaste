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

AC_PREREQ([2.68])
AC_INIT([GPaste],[2.5],[Marc-Antoine@Perennou.com],[gpaste])

GLIB_REQUIRED="2.30.0"
GTK_REQUIRED="3.0.0"

AC_SUBST([PACKAGE_NAME], ["$PACKAGE_NAME"])
AC_SUBST([PACKAGE_VERSION], ["$PACKAGE_VERSION"])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_PREFIX_DEFAULT([/usr])

LT_PREREQ([2.2.6])
LT_INIT([disable-static pic-only])
AM_INIT_AUTOMAKE([1.11 subdir-objects check-news foreign no-dist-gzip dist-xz tar-ustar silent-rules -Wall])
AM_SILENT_RULES([yes])

AC_PROG_SED
AC_PROG_MKDIR_P
AC_PROG_INSTALL

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O

CC_CHECK_CFLAGS_APPEND([ \
                         -pipe \
                         -pedantic \
                         -DANOTHER_BRICK_IN_THE \
                         -Wall \
                         -W \
                         -Wextra \
                         -Wvla \
                         -Wundef \
                         -Wformat=2 \
                         -Wlogical-op \
                         -Wsign-compare \
                         -Wformat-security \
                         -Wmissing-include-dirs \
                         -Wformat-nonliteral \
                         -Wold-style-definition \
                         -Wpointer-arith \
                         -Winit-self \
                         -Wfloat-equal \
                         -Wmissing-prototypes \
                         -Wstrict-prototypes \
                         -Wredundant-decls \
                         -Wmissing-declarations \
                         -Wmissing-noreturn \
                         -Wshadow \
                         -Wendif-labels \
                         -Wcast-align \
                         -Wstrict-aliasing=2 \
                         -Wwrite-strings \
                         -Werror=overflow \
                         -Wp,-D_FORTIFY_SOURCE=2 \
                         -ffast-math \
                         -fno-common \
                         -fdiagnostics-show-option \
                         -fno-strict-aliasing \
                         -fvisibility=hidden \
                         -ffunction-sections \
                         -fdata-sections \
                         -Wl,--as-needed \
                         -Wl,--gc-sections \
                       ])

AC_DEFINE_UNQUOTED([G_PASTE_VISIBLE], [__attribute__((visibility("default")))], [Export symbol])

GOBJECT_INTROSPECTION_REQUIRE([1.30.0])
AM_PROG_VALAC([0.14.0])
AC_PATH_PROG(VAPIGEN, vapigen, vapigen)
AC_SUBST(VAPIGEN)

GETTEXT_PACKAGE=$PACKAGE_NAME
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [Gettext Package])
AC_SUBST(GETTEXT_PACKAGE)
IT_PROG_INTLTOOL([0.40.0])

GLIB_GSETTINGS

PKG_PROG_PKG_CONFIG([0.22])

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= $GLIB_REQUIRED])
PKG_CHECK_MODULES(GOBJECT, [gobject-2.0 >= $GLIB_REQUIRED])
PKG_CHECK_MODULES(GIO, [gio-2.0 >= $GLIB_REQUIRED])
GIO_VALAFLAGS="--pkg=gio-2.0"
AC_SUBST(GIO_VALAFLAGS)

PKG_CHECK_MODULES(GDK, [gdk-3.0 >= $GTK_REQUIRED])
PKG_CHECK_MODULES(GTK, [gtk+-3.0 >= $GTK_REQUIRED])
GTK_VALAFLAGS="--pkg=gtk+-3.0"
AC_SUBST(GTK_VALAFLAGS)

PKG_CHECK_MODULES(GDK_PIXBUF, [gdk-pixbuf-2.0])

PKG_CHECK_MODULES(XML, [libxml-2.0])

PKG_CHECK_MODULES(XCB, [xcb xcb-keysyms])

PKG_CHECK_MODULES(DBUS, [dbus-1])
AC_ARG_WITH([dbusservicesdir],
        AS_HELP_STRING([--with-dbusservicesdir=DIR], [D-Bus system service directory]),
        [],
        [with_dbusservicesdir=`pkg-config --variable=session_bus_services_dir dbus-1`])
AC_SUBST([dbusservicesdir], [$with_dbusservicesdir])

AC_ARG_ENABLE(applet, AS_HELP_STRING([--enable-applet], [Enable gpaste-applet]), enable_applet=$enableval, enable_applet=no)
AM_CONDITIONAL(ENABLE_APPLET, test x$enable_applet = xyes)

AC_ARG_ENABLE(gnome-shell-extension, AS_HELP_STRING([--disable-gnome-shell-extension], [Disable gnome-shell extension]), enable_extension=$enableval, enable_extension=yes)
AM_CONDITIONAL(ENABLE_EXTENSION, test x$enable_extension = xyes)

AC_ARG_ENABLE(vala, AS_HELP_STRING([--enable-vala], [Enable vapi generation]), enable_vala=$enableval, enable_vala=no)
AM_CONDITIONAL(ENABLE_VALA, test x$enable_vala != xno)

AC_CONFIG_FILES([
    Makefile
    po/Makefile.in
    data/org.gnome.GPaste.gschema.xml.in
    libgpaste/gpaste.pc
])
AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE_NAME $VERSION
        ========

        prefix:                 ${prefix}
        exec_prefix:            ${exec_prefix}
        sysconfdir:             ${sysconfdir}
        libdir:                 ${libdir}
        includedir:             ${includedir}

        compiler:               ${CC}
        cflags:                 ${CFLAGS}
        ldflags:                ${LDFLAGS}

        vala compiler:          ${VALAC}
        vapi generator:         ${VAPIGEN}

        Legacy applet:          ${enable_applet}
        Gnome-Shell extension:  ${enable_extension}
        Rebuild vapi:           ${enable_vala}
])
