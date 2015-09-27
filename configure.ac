# This file is part of GPaste.
#
# Copyright 2011-2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
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
AC_INIT([GPaste],[3.19],[Marc-Antoine@Perennou.com],[gpaste])

# Update LIBGPASTE_LT_VERSION before each release (and only then)
# The format is C:R:A (current revision age)
# - If any interface has been added, ‘C:R:A’ becomes ‘C+1:0:A+1’.
# - If any interface has been removed or modified, ‘C:R:A’ becomes ‘C+1:0:0’.
# - Otherwise, ‘C:R:A’ becomes ‘C:R+1:A’.
LIBGPASTE_LT_VERSION=4:0:0
AC_SUBST(LIBGPASTE_LT_VERSION)

INTLTOOL_REQUIRED="0.50.1"
PKGCONFIG_REQUIRED="0.22"

GLIB_MIN_MAJOR="2"
GLIB_MIN_MINOR="46"
GTK_MIN_MAJOR="3"
GTK_MIN_MINOR="16"

GLIB_REQUIRED="$GLIB_MIN_MAJOR.$GLIB_MIN_MINOR.0"
GTK_REQUIRED="$GTK_MIN_MAJOR.$GTK_MIN_MINOR.0"
GDK_PIXBUF_REQUIRED="2.26.0"
GIR_REQUIRED="1.42.0"
VALA_REQUIRED="0.30"

GLIB_API_REQUIRED="G_ENCODE_VERSION($GLIB_MIN_MAJOR, $GLIB_MIN_MINOR)"
GDK_API_REQUIRED="G_ENCODE_VERSION($GTK_MIN_MAJOR, $GTK_MIN_MINOR)"

AC_DEFINE_UNQUOTED([GLIB_VERSION_MIN_REQUIRED], [$GLIB_API_REQUIRED], [The lower GLib API version supported])
AC_DEFINE_UNQUOTED([GLIB_VERSION_MAX_ALLOWED], [$GLIB_API_REQUIRED], [The higher GLib API version supported])
AC_DEFINE_UNQUOTED([GDK_VERSION_MIN_REQUIRED], [$GDK_API_REQUIRED], [The lower Gdk API version supported])
AC_DEFINE_UNQUOTED([GDK_VERSION_MAX_ALLOWED], [$GDK_API_REQUIRED], [The higher Gdk API version supported])

AC_DEFINE([G_PASTE_DEFAULT_HISTORY], ["history"], [The default hsitory name])
AC_DEFINE([G_PASTE_EXTENSION_NAME], ["GPaste@gnome-shell-extensions.gnome.org"], [The name of the GPaste extension])

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

AM_INIT_AUTOMAKE([1.15 subdir-objects check-news foreign no-dist-gzip dist-xz tar-ustar -Wall])
AM_MAINTAINER_MODE([enable])
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

gl_VISIBILITY

G_PASTE_APPEND_CFLAGS
G_PASTE_APPEND_LDFLAGS

GETTEXT_PACKAGE=$PACKAGE_NAME
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [Gettext Package])
AC_SUBST(GETTEXT_PACKAGE)
IT_PROG_INTLTOOL([$INTLTOOL_REQUIRED])

PKG_PROG_PKG_CONFIG([$PKGCONFIG_REQUIRED])

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= $GLIB_REQUIRED gobject-2.0 >= $GLIB_REQUIRED gio-2.0 >= $GLIB_REQUIRED])
PKG_CHECK_MODULES(GTK, [gdk-3.0 >= $GTK_REQUIRED gtk+-3.0 >= $GTK_REQUIRED pango])
PKG_CHECK_MODULES(GDK_PIXBUF, [gdk-pixbuf-2.0 >= $GDK_PIXBUF_REQUIRED])
PKG_CHECK_MODULES(X11, [x11 xi])
AC_CHECK_HEADERS([X11/extensions/XInput2.h])

GLIB_GSETTINGS

APPSTREAM_XML

GOBJECT_INTROSPECTION_CHECK([$GIR_REQUIRED])
VAPIGEN_CHECK([$VALA_REQUIRED])

PKG_CHECK_MODULES(DBUS, [dbus-1])
G_PASTE_WITH([dbusservicesdir],  [D-Bus system session directory],             [`$PKG_CONFIG --variable session_bus_services_dir dbus-1`])
G_PASTE_WITH([controlcenterdir], [Gnome control-center keybindings directory], [`$PKG_CONFIG --variable keysdir gnome-keybindings`])

G_PASTE_ENABLE([applet],                [Enable gpaste-applet],                    [no])
G_PASTE_ENABLE([cinnamon-applet],       [Enable cinnamon applet],                  [no])
G_PASTE_ENABLE([gnome-shell-extension], [Disable gnome-shell extension],           [yes])
G_PASTE_ENABLE([legacy-cli],            [Disable the legacy cli wrapper (gpaste)], [yes])
G_PASTE_ENABLE([unity],                 [Enable gpaste-app-indicator],             [no])
G_PASTE_ENABLE([x-keybinder],           [Disable the X keybinder],                 [yes])

AS_IF([test x$enable_gnome_shell_extension = xyes -o x$enable_cinnamon_applet = xyes], [
    AS_IF([test x$enable_introspection != xyes], [AC_MSG_ERROR([*** Introspection support is required to run the javascript extensions])])
    PKG_CHECK_MODULES(JS_EXTENSION, [clutter-1.0])
])

AS_IF([test x$enable_unity = xyes], [
    PKG_CHECK_MODULES(UNITY, [appindicator3-0.1])
    AC_DEFINE([ENABLE_UNITY], [1], [Whether the app-indicator is built])
])

AS_IF([test x$enable_x_keybinder = xyes], [
    PKG_CHECK_MODULES(GDK_X11, [gdk-x11-3.0 >= $GTK_REQUIRED])
    AC_DEFINE([ENABLE_X_KEYBINDER], [1], [Whether the X keybinder is built])
])

AC_CONFIG_FILES([
    Makefile
    po/Makefile.in
])
AC_OUTPUT
AC_MSG_RESULT([
    $PACKAGE_NAME $VERSION
    ===========

    prefix:                 ${prefix}
    exec_prefix:            ${exec_prefix}
    sysconfdir:             ${sysconfdir}
    libdir:                 ${libdir}
    includedir:             ${includedir}

    compiler:               ${CC}
    cflags:                 ${CFLAGS}
    ldflags:                ${LDFLAGS}

    vapi generator:         ${VAPIGEN}

    GObject Introspection:  ${enable_introspection}
    Vapi:                   ${enable_vala}

    Status icon:            ${enable_applet}
    App indicator:          ${enable_unity}
    Gnome-Shell extension:  ${enable_gnome_shell_extension}
    Cinnamon applet:        ${enable_cinnamon_applet}

    X keybinder support:    ${enable_x_keybinder}
])
