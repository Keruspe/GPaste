# This file is part of GPaste.
#
# Copyright 2011-2014 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
AC_INIT([GPaste], [3.14.4.1], [Marc-Antoine@Perennou.com], [gpaste])

INTLTOOL_REQUIRED="0.50.0"
PKGCONFIG_REQUIRED="0.22"

GLIB_MIN_MAJOR="2"
GLIB_MIN_MINOR="40"
GLIB_API_REQUIRED="G_ENCODE_VERSION($GLIB_MIN_MAJOR, $GLIB_MIN_MINOR)"

AC_SUBST([GLIB_TARGET], ["$GLIB_MIN_MAJOR.$GLIB_MIN_MINOR"])

GTK_MIN_MAJOR="3"
GTK_MIN_MINOR="12"
GDK_API_REQUIRED="G_ENCODE_VERSION($GTK_MIN_MAJOR, $GTK_MIN_MINOR)"

GLIB_REQUIRED="$GLIB_MIN_MAJOR.$GLIB_MIN_MINOR.0"
GTK_REQUIRED="$GTK_MIN_MAJOR.$GTK_MIN_MINOR.0"

GIR_REQUIRED="1.42.0"
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

AM_INIT_AUTOMAKE([1.11 subdir-objects check-news foreign no-dist-gzip dist-xz tar-ustar -Wall])
AM_SILENT_RULES([yes])

AC_PROG_AWK
AC_PROG_SED
AC_PROG_MKDIR_P
AC_PROG_LN_S
AC_PROG_INSTALL

AC_PROG_MAKE_SET

AC_PROG_CPP
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O

AC_C_INLINE
AC_TYPE_MODE_T
AC_FUNC_ALLOCA
AC_CHECK_FUNCS([mkdir])

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
                         -Wlogical-op-parentheses \
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
                         -Wno-unknown-warning-option \
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

APPDATA_XML

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

PKG_CHECK_MODULES(GDK_PIXBUF, [gdk-pixbuf-2.0 >= 2.26])

PKG_CHECK_MODULES(X11, [x11 xi])
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

ENABLE_APPLET=0
AC_ARG_ENABLE(applet, AS_HELP_STRING([--enable-applet], [Enable gpaste-applet]), enable_applet=$enableval, enable_applet=no)
if test x$enable_applet = xyes; then
    AC_DEFINE([ENABLE_APPLET], [1], [Whether the applet is built])
    ENABLE_APPLET=1
fi
AC_SUBST(ENABLE_APPLET)
AM_CONDITIONAL(ENABLE_APPLET, test x$enable_applet = xyes)

ENABLE_UNITY=0
AC_ARG_ENABLE(unity, AS_HELP_STRING([--enable-unity], [Enable gpaste-app-indicator]), enable_unity=$enableval, enable_unity=no)
if test x$enable_unity = xyes; then
    PKG_CHECK_MODULES(UNITY, [appindicator3-0.1])
    AC_DEFINE([ENABLE_UNITY], [1], [Whether the app-indicator is built])
    ENABLE_UNITY=1
fi
AC_SUBST(ENABLE_UNITY)
AM_CONDITIONAL(ENABLE_UNITY, test x$enable_unity = xyes)

ENABLE_EXTENSION=0
AC_ARG_ENABLE(gnome-shell-extension, AS_HELP_STRING([--disable-gnome-shell-extension], [Disable gnome-shell extension]), enable_extension=$enableval, enable_extension=yes)
if test x$enable_extension = xyes; then
    if test x$enable_introspection != xyes; then
        AC_MSG_ERROR([*** Introspection support is required to run the gnome-shell extension])
    fi
    PKG_CHECK_MODULES(GNOME_SHELL, [clutter-1.0 pango])
    AC_DEFINE([ENABLE_EXTENSION], [1], [Whether the extension is enabled])
    ENABLE_EXTENSION=1
fi
AC_SUBST(ENABLE_EXTENSION)
AM_CONDITIONAL(ENABLE_EXTENSION, test x$enable_extension = xyes)

AC_ARG_ENABLE(x-keybinder, AS_HELP_STRING([--disable-x-keybinder], [Disable the X keybinder]), enable_x_keybinder=$enableval, enable_x_keybinder=yes)
if test x$enable_x_keybinder = xyes; then
    PKG_CHECK_MODULES(GDK_X11, [gdk-x11-3.0 >= $GTK_REQUIRED])
    AC_DEFINE([ENABLE_X_KEYBINDER], [1], [Whether the X keybinder is built])
fi
AM_CONDITIONAL(ENABLE_X_KEYBINDER, test x$enable_x_keybinder = xyes)

AC_CONFIG_FILES([
    Makefile
    po/Makefile.in
    src/libgpaste/gpaste-config.h
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

    GObject Introspection:  ${enable_introspection}
    Vapi:                   ${enable_vala}

    Status icon:            ${enable_applet}
    App indicator:          ${enable_unity}
    Gnome-Shell extension:  ${enable_extension}

    X keybinder support:    ${enable_x_keybinder}
])
