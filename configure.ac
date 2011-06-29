AC_PREREQ([2.68])
AC_INIT([GPaste],[1.0],[Marc-Antoine@Perennou.com])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([.aux])

AC_SUBST([PACKAGE_NAME], ["$PACKAGE_NAME"])
AC_SUBST([PACKAGE_VERSION], ["$PACKAGE_VERSION"])

AM_INIT_AUTOMAKE([1.10 no-dist-gzip dist-xz tar-ustar])
AM_MAINTAINER_MODE

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

AC_PROG_CC

AC_PATH_PROG(VALAC, valac-0.12, valac-0.12)
AC_SUBST(VAPIDIR)

GETTEXT_PACKAGE=$PACKAGE_NAME
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [Gettext Package])
AC_SUBST(GETTEXT_PACKAGE)
AM_GLIB_GNU_GETTEXT
IT_PROG_INTLTOOL([0.35.0])

AC_SUBST([bindir], ["$bindir"])
AC_SUBST([libexecdir], ["$libexecdir"])
AC_SUBST([localedir], ["$localedir"])

AC_PROG_INSTALL

GLIB_GSETTINGS

PKG_CHECK_MODULES(GLIB, [glib-2.0 >= 2.27.0])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES(GIO, [gio-2.0 >= 2.27.0])
GIO_VALAFLAGS="--pkg=gio-2.0"
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)
AC_SUBST(GIO_VALAFLAGS)

PKG_CHECK_MODULES(GTK, [gtk+-3.0 >= 3.0.0])
GTK_VALAFLAGS="--pkg=gtk+-3.0"
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)
AC_SUBST(GTK_VALAFLAGS)

AC_ARG_ENABLE(applet, AS_HELP_STRING([--disable-applet], [Disable gpaste-applet]), enable_applet=$enableval, enable_applet=yes)
AM_CONDITIONAL(ENABLE_APPLET, test x$enable_applet = xyes)

AC_ARG_ENABLE(gnome-shell-extension, AS_HELP_STRING([--disable-gnome-shell-extension], [Disable gnome-shell extension]), enable_extension=$enableval, enable_extension=yes)
AM_CONDITIONAL(ENABLE_EXTENSION, test x$enable_extension = xyes)

AC_CHECK_PROGS([POD2MAN], [pod2man])
AC_SUBST(POD2MAN)

AC_CONFIG_FILES([
  Makefile
  po/Makefile.in
  data/org.gnome.GPaste.gschema.xml.in
])
AC_OUTPUT

