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
AC_INIT([GPaste],[1.6],[Marc-Antoine@Perennou.com])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([.aux])

AM_INIT_AUTOMAKE([1.11 subdir-objects check-news foreign no-dist-gzip dist-xz tar-ustar -Wall])
AM_SILENT_RULES([yes])

AC_PROG_SED
AC_PROG_MKDIR_P
AC_PROG_INSTALL
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_VALAC([0.13.4])

GETTEXT_PACKAGE=$PACKAGE_NAME
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [Gettext Package])
AC_SUBST(GETTEXT_PACKAGE)
IT_PROG_INTLTOOL([0.40.0])

GLIB_GSETTINGS

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.27.0])
PKG_CHECK_MODULES(GIO, [gio-2.0 >= 2.27.0])
GIO_VALAFLAGS="--pkg=gio-2.0"
AC_SUBST(GIO_VALAFLAGS)

PKG_CHECK_MODULES(GTK, [gtk+-3.0 >= 3.0.0])
GTK_VALAFLAGS="--pkg=gtk+-3.0"
AC_SUBST(GTK_VALAFLAGS)

PKG_CHECK_MODULES(GDK, [gdk-x11-3.0 >= 3.0.0])
GDK_VALAFLAGS="--pkg=gdk-x11-3.0"
AC_SUBST(GDK_VALAFLAGS)

PKG_CHECK_MODULES(X11, [x11])

PKG_CHECK_MODULES(DBUS, [dbus-1])
AC_ARG_WITH([dbusservicesdir],
        AS_HELP_STRING([--with-dbusservicesdir=DIR], [D-Bus system service directory]),
        [],
        [with_dbusservicesdir=`pkg-config --variable=session_bus_services_dir dbus-1`])
AC_SUBST([dbusservicesdir], [$with_dbusservicesdir])

AC_ARG_ENABLE(applet, AS_HELP_STRING([--disable-applet], [Disable gpaste-applet]), enable_applet=$enableval, enable_applet=no)
AM_CONDITIONAL(ENABLE_APPLET, test x$enable_applet = xyes)

AC_ARG_ENABLE(gnome-shell-extension, AS_HELP_STRING([--disable-gnome-shell-extension], [Disable gnome-shell extension]), enable_extension=$enableval, enable_extension=yes)
AM_CONDITIONAL(ENABLE_EXTENSION, test x$enable_extension = xyes)

AC_CONFIG_FILES([
  Makefile
  po/Makefile.in
  data/org.gnome.GPaste.gschema.xml.in
])
AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE_NAME $VERSION

        Legacy applet:         ${enable_applet}
        Gnome-Shell extension: ${enable_extension}
])
