## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

m4_define([gpaste_version],      [3.22.5])

# Update gpaste_lt_version before each release (and only then)
# The format is C:R:A (current revision age)
# - If any interface has been added, ‘C:R:A’ becomes ‘C+1:0:A+1’.
# - If any interface has been removed or modified, ‘C:R:A’ becomes ‘C+1:0:0’.
# - Otherwise, ‘C:R:A’ becomes ‘C:R+1:A’.
m4_define([gpaste_lt_version],   [7:4:1])

# Build system requirements
m4_define([autoconf_required],   [2.69])
m4_define([automake_required],   [1.15])
m4_define([libtool_required],    [2.2.6])
m4_define([pkgconfig_required],  [0.29])

# Glib and Gtk macros and dependencies
m4_define([glib_major_required], [2])
m4_define([glib_minor_required], [50])
m4_define([glib_api_required],   [G_ENCODE_VERSION(glib_major_required, glib_minor_required)])
m4_define([glib_required],       [glib_major_required.glib_minor_required.0])
m4_define([gtk_major_required],  [3])
m4_define([gtk_minor_required],  [22])
m4_define([gtk_api_required],    [G_ENCODE_VERSION(gtk_major_required, gtk_minor_required)])
m4_define([gtk_required],        [gtk_major_required.gtk_minor_required.0])

# Other dependencies
m4_define([gdk_pixbuf_required], [2.26.0])
m4_define([gir_required],        [1.50.0])
m4_define([vala_required],       [0.32])

AC_PREREQ(autoconf_required)
AC_INIT([GPaste], [gpaste_version], [Marc-Antoine@Perennou.com], [gpaste])

AC_SUBST([PACKAGE_NAME])
AC_SUBST([PACKAGE_VERSION])

LIBGPASTE_LT_VERSION="gpaste_lt_version"
AC_SUBST([LIBGPASTE_LT_VERSION])

AC_DEFINE_UNQUOTED([GLIB_VERSION_MIN_REQUIRED], [glib_api_required], [The lowest GLib API version supported])
AC_DEFINE_UNQUOTED([GLIB_VERSION_MAX_ALLOWED],  [glib_api_required], [The highest GLib API version supported])
AC_DEFINE_UNQUOTED([GDK_VERSION_MIN_REQUIRED],  [gtk_api_required],  [The lowest Gdk API version supported])
AC_DEFINE_UNQUOTED([GDK_VERSION_MAX_ALLOWED],   [gtk_api_required],  [The highest Gdk API version supported])

AC_DEFINE([G_PASTE_DEFAULT_HISTORY], ["history"],                                 [The default hsitory name])
AC_DEFINE([G_PASTE_ICON_NAME],       ["edit-paste"],                              [The name of the icon used by GPaste])
AC_DEFINE([G_PASTE_EXTENSION_NAME],  ["GPaste@gnome-shell-extensions.gnome.org"], [The name of the GPaste extension])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_PREFIX_DEFAULT([/usr])

AC_USE_SYSTEM_EXTENSIONS

AM_PROG_AR
LT_PREREQ(libtool_required)
LT_INIT([disable-static pic-only])

AM_INIT_AUTOMAKE(automake_required [subdir-objects check-news foreign no-dist-gzip dist-xz tar-ustar -Wall])
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

G_PASTE_APPEND_CFLAGS
G_PASTE_APPEND_LDFLAGS

GETTEXT_PACKAGE=${PACKAGE_NAME}
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["${GETTEXT_PACKAGE}"], [Gettext Package])
AC_SUBST([GETTEXT_PACKAGE])
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19.7])

PKG_PREREQ(pkgconfig_required)
PKG_PROG_PKG_CONFIG([pkgconfig_required])
PKG_INSTALLDIR

PKG_CHECK_MODULES(GLIB,       [glib-2.0 >= glib_required gobject-2.0 >= glib_required gio-2.0 >= glib_required])
PKG_CHECK_MODULES(GTK,        [gdk-3.0 >= gtk_required gtk+-3.0 >= gtk_required pango])
PKG_CHECK_MODULES(GDK_PIXBUF, [gdk-pixbuf-2.0 >= gdk_pixbuf_required])
PKG_CHECK_MODULES(X11,        [x11 xi])
PKG_CHECK_MODULES(APPSTREAM,  [appstream-glib])

AC_CHECK_HEADERS([X11/extensions/XInput2.h])

GLIB_GSETTINGS

APPSTREAM_XML

GOBJECT_INTROSPECTION_CHECK([gir_required])
VAPIGEN_CHECK([vala_required])

# For bus_services_dir in pkg-config
PKG_CHECK_MODULES(DBUS, [dbus-1])

G_PASTE_WITH([controlcenterdir],   [Gnome control-center keybindings directory], [`${PKG_CONFIG} --variable keysdir gnome-keybindings`])
G_PASTE_WITH([dbusservicesdir],    [D-Bus system session directory],             [`${PKG_CONFIG} --variable session_bus_services_dir dbus-1`])
G_PASTE_WITH([systemduserunitdir], [systemd user units directory],               [`${PKG_CONFIG} --variable systemduserunitdir systemd`])

G_PASTE_ENABLE([applet],                [Enable gpaste-applet],          [no])
G_PASTE_ENABLE([bash-completion],       [Disable the bash completion],   [yes])
G_PASTE_ENABLE([gnome-shell-extension], [Disable gnome-shell extension], [yes])
G_PASTE_ENABLE([unity],                 [Enable gpaste-app-indicator],   [no])
G_PASTE_ENABLE([x-keybinder],           [Disable the X keybinder],       [yes])
G_PASTE_ENABLE([zsh-completion],        [Disable the zsh completion],    [yes])

AS_IF([test x${enable_gnome_shell_extension} = xyes], [
    AS_IF([test x${enable_introspection} != xyes], [AC_MSG_ERROR([*** Introspection support is required to run the gnome-shell extension])])
    PKG_CHECK_MODULES(JS_EXTENSION, [mutter-clutter-1.0])
])

AS_IF([test x${enable_unity} = xyes], [
    PKG_CHECK_MODULES(UNITY, [appindicator3-0.1])
])

AS_IF([test x${enable_x_keybinder} = xyes], [
    PKG_CHECK_MODULES(GDK_X11, [gdk-x11-3.0 >= gtk_required])
    AC_DEFINE([ENABLE_X_KEYBINDER], [1], [Whether the X keybinder is built])
])

AS_IF([test x${enable_bash_completion} = xyes], [
    G_PASTE_WITH([bashcompletiondir], [Bash completion directory], ['${datadir}/bash-completion/completions'])
])

AS_IF([test x${enable_zsh_completion} = xyes], [
    G_PASTE_WITH([zshcompletiondir], [Zsh completion directory], ['${datadir}/zsh/site-functions'])
])

AC_CONFIG_FILES([
    Makefile
    po/Makefile.in
])
AC_OUTPUT
AC_MSG_RESULT([
    ${PACKAGE_NAME} ${VERSION}
    ===========

    prefix:                 ${prefix}
    exec_prefix:            ${exec_prefix}
    sysconfdir:             ${sysconfdir}
    libdir:                 ${libdir}
    libexecdir:             ${libexecdir}
    includedir:             ${includedir}
    datarootdir:            ${datarootdir}
    datadir:                ${datadir}

    compiler:               ${CC}
    cflags:                 ${CFLAGS}
    ldflags:                ${LDFLAGS}

    vapi generator:         ${VAPIGEN}

    GObject Introspection:  ${enable_introspection}
    Vapi:                   ${enable_vala}

    Status icon:            ${enable_applet}
    App indicator:          ${enable_unity}
    Gnome-Shell extension:  ${enable_gnome_shell_extension}

    X keybinder support:    ${enable_x_keybinder}

    Control Center dir:     ${controlcenterdir}
    DBus user services dir: ${dbusservicesdir}
    systemd user units dir: ${systemduserunitdir}

    Bash completion:        ${enable_bash_completion}
    Bash completion dir:    ${bashcompletiondir}

    Zsh completion:         ${enable_zsh_completion}
    Zsh completion dir:     ${zshcompletiondir}
])
