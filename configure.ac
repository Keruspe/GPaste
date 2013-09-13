# This file is part of GPaste.
#
# Copyright 2011-2013 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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

AC_PREREQ([2.69])
AC_INIT([GPaste],[3.1],[Marc-Antoine@Perennou.com],[gpaste])

INTLTOOL_REQUIRED="0.50.0"
PKGCONFIG_REQUIRED="0.22"

GLIB_MIN_MAJOR="2"
GLIB_MIN_MINOR="38"
GLIB_API_REQUIRED="G_ENCODE_VERSION($GLIB_MIN_MAJOR, $GLIB_MIN_MINOR)"

AC_SUBST([GLIB_TARGET], ["$GLIB_MIN_MAJOR.$GLIB_MIN_MINOR"])

GTK_MIN_MAJOR="3"
GTK_MIN_MINOR="0"
GDK_API_REQUIRED="G_ENCODE_VERSION($GTK_MIN_MAJOR, $GTK_MIN_MINOR)"

GLIB_REQUIRED="$GLIB_MIN_MAJOR.37.3"
GTK_REQUIRED="$GTK_MIN_MAJOR.$GTK_MIN_MINOR.0"

GIR_REQUIRED="1.31.1"
VALA_REQUIRED="0.14"

AC_SUBST([PACKAGE_NAME], ["$PACKAGE_NAME"])
AC_SUBST([PACKAGE_VERSION], ["$PACKAGE_VERSION"])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_PREFIX_DEFAULT([/usr])

AM_PROG_AR
LT_PREREQ([2.2.6])
LT_INIT([disable-static pic-only])

AM_INIT_AUTOMAKE([1.12 subdir-objects check-news foreign no-dist-gzip dist-xz tar-ustar -Wall])
AM_SILENT_RULES([yes])

AC_PROG_SED
AC_PROG_MKDIR_P
AC_PROG_INSTALL

AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O

AC_CHECK_HEADER_STDBOOL

CC_ATTRIBUTE_UNUSED
CC_ATTRIBUTE_VISIBILITY([default])

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
                       ])

CC_CHECK_LDFLAGS_APPEND([ \
                          -Wl,--as-needed \
                          -Wl,--gc-sections \
                        ])

AC_DEFINE_UNQUOTED([G_PASTE_VISIBLE], [__attribute__((visibility("default")))], [Export symbol])
AC_DEFINE_UNQUOTED([GLIB_VERSION_MIN_REQUIRED], [$GLIB_API_REQUIRED], [The lower GLib API version supported])
AC_DEFINE_UNQUOTED([GLIB_VERSION_MAX_ALLOWED], [$GLIB_API_REQUIRED], [The higher GLib API version supported])
AC_DEFINE_UNQUOTED([GDK_VERSION_MIN_REQUIRED], [$GDK_API_REQUIRED], [The lower Gdk API version supported])
AC_DEFINE_UNQUOTED([GDK_VERSION_MAX_ALLOWED], [$GDK_API_REQUIRED], [The higher Gdk API version supported])

GETTEXT_PACKAGE=$PACKAGE_NAME
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [Gettext Package])
AC_SUBST(GETTEXT_PACKAGE)
IT_PROG_INTLTOOL([$INTLTOOL_REQUIRED])

PKG_PROG_PKG_CONFIG([$PKGCONFIG_REQUIRED])

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= $GLIB_REQUIRED gobject-2.0 >= $GLIB_REQUIRED gio-2.0 >= $GLIB_REQUIRED])
GIO_VALAFLAGS="--pkg=gio-2.0"
AC_SUBST(GIO_VALAFLAGS)

GLIB_GSETTINGS

m4_ifdef([GOBJECT_INTROSPECTION_CHECK],
         [GOBJECT_INTROSPECTION_CHECK([$GIR_REQUIRED])],
         [
             AM_CONDITIONAL([HAVE_INTROSPECTION], [false])
             enable_introspection=no
         ])

AC_ARG_ENABLE(vala, AS_HELP_STRING([--enable-vala], [Build vala bindings]), enable_vala=$enableval, enable_vala=no)
if test x$enable_vala = xyes; then
    if test x$enable_introspection != xyes; then
        AC_MSG_ERROR([*** Introspection support is required to build vala bindings])
    fi
    AM_PROG_VALAC([$VALA_REQUIRED])
    AC_PATH_PROG(VAPIGEN, vapigen, vapigen)
    AC_SUBST(VAPIGEN)
fi
AM_CONDITIONAL(ENABLE_VALA, test x$enable_vala = xyes)

PKG_CHECK_MODULES(GTK, [gdk-3.0 >= $GTK_REQUIRED gtk+-3.0 >= $GTK_REQUIRED])
GTK_VALAFLAGS="--pkg=gtk+-3.0"
AC_SUBST(GTK_VALAFLAGS)

PKG_CHECK_MODULES(GDK_PIXBUF, [gdk-pixbuf-2.0])

PKG_CHECK_MODULES(XML, [libxml-2.0])

PKG_CHECK_MODULES(X11, [x11 xi xtst])
AC_CHECK_HEADERS([X11/extensions/XInput2.h])

AC_ARG_WITH([controlcenterdir],
            AS_HELP_STRING([--with-controlcenterdir=DIR], [Gnome control-center keybindings directory]),
            [],
            [with_controlcenterdir=`$PKG_CONFIG --variable keysdir gnome-keybindings`])
AC_SUBST([controlcenterdir], [$with_controlcenterdir])

PKG_CHECK_MODULES(DBUS, [dbus-1])
AC_ARG_WITH([dbusservicesdir],
            AS_HELP_STRING([--with-dbusservicesdir=DIR], [D-Bus system service directory]),
            [],
            [with_dbusservicesdir=`$PKG_CONFIG --variable=session_bus_services_dir dbus-1`])
AC_SUBST([dbusservicesdir], [$with_dbusservicesdir])

AC_ARG_ENABLE(systemd, AS_HELP_STRING([--enable-systemd], [Enable systemd activation support]), [], enable_systemd=no)
AC_ARG_WITH(systemduserunitdir, AS_HELP_STRING([--with-systemduserunitdir=DIR], [Directory for systemd user unit files]), [], with_systemduserunitdir=auto)
if test x$enable_systemd = xyes; then
    case "$with_systemduserunitdir" in
        no|"")
            AC_MSG_ERROR([*** You must specify systemduserunitdir when enabling systemd support])
            ;;
        auto)
            PKG_CHECK_MODULES(SYSTEMD, [systemd])
            systemduserunitdir="`$PKG_CONFIG --variable=systemduserunitdir systemd`"
            if test x$systemduserunitdir = x; then
                AC_MSG_ERROR([*** Could not detect systemduserunitdir when enabling systemd support])
            fi
            ;;
        *)
            systemduserunitdir="$with_systemduserunitdir"
            ;;
    esac
else
    systemduserunitdir="/dev/null"
fi
AC_SUBST(systemduserunitdir)
AM_CONDITIONAL(ENABLE_SYSTEMD, test x$enable_systemd = xyes)

AC_ARG_ENABLE(applet, AS_HELP_STRING([--enable-applet], [Enable gpaste-applet]), enable_applet=$enableval, enable_applet=no)
if test x$enable_applet = xyes; then
    if test x$enable_vala != xyes; then
        AC_MSG_ERROR([*** Vala support is required to build the applet])
    fi
    AC_DEFINE([ENABLE_APPLET], [1], [Whether the applet is built])
fi
AM_CONDITIONAL(ENABLE_APPLET, test x$enable_applet = xyes)

AC_ARG_ENABLE(gnome-shell-extension, AS_HELP_STRING([--disable-gnome-shell-extension], [Disable gnome-shell extension]), enable_extension=$enableval, enable_extension=yes)
if test x$enable_extension = xyes; then
    if test x$enable_introspection != xyes; then
        AC_MSG_ERROR([*** Introspection support is required to run the gnome-shell extension])
    fi
    AC_DEFINE([ENABLE_EXTENSION], [1], [Whether the extension is enabled])
fi
AM_CONDITIONAL(ENABLE_EXTENSION, test x$enable_extension = xyes)

AC_ARG_ENABLE(x-keybinder, AS_HELP_STRING([--disable-x-keybinder], [Disable the X keybinder]), enable_x_keybinder=$enableval, enable_x_keybinder=yes)
if test x$enable_x_keybinder = xyes; then
    PKG_CHECK_MODULES(GDK_X11, [gdk-x11-3.0 >= $GTK_REQUIRED])
    AC_DEFINE([ENABLE_X_KEYBINDER], [1], [Whether the X keybinder is built])
    AC_SUBST([KEYBINDER_DEFINES], [-DENABLE_X_KEYBINDER])
else
    AC_SUBST([KEYBINDER_DEFINES], [-UENABLE_X_KEYBINDER])
fi
AM_CONDITIONAL(ENABLE_X_KEYBINDER, test x$enable_x_keybinder = xyes)

AC_CONFIG_FILES([
    Makefile
    po/Makefile.in
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
    systemd support:        ${enable_systemd} (${systemduserunitdir})
])
