## This file is part of GPaste.
##
## Copyright (c) 2010-2018, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

m4_define([gpaste_version],      [3.40.0])

# Update gpaste_lt_version before each release (and only then)
# The format is C:R:A (current revision age)
# - If any interface has been added, ‘C:R:A’ becomes ‘C+1:0:A+1’.
# - If any interface has been removed or modified, ‘C:R:A’ becomes ‘C+1:0:0’.
# - Otherwise, ‘C:R:A’ becomes ‘C:R+1:A’.
m4_define([gpaste_lt_version],   [13:4:0])

# Build system requirements
m4_define([autoconf_required],   [2.69])
m4_define([automake_required],   [1.15])
m4_define([libtool_required],    [2.2.6])
m4_define([pkgconfig_required],  [0.29])

# Glib and Gtk macros and dependencies
m4_define([glib_major_required], [2])
m4_define([glib_minor_required], [64])
m4_define([glib_api_required],   [G_ENCODE_VERSION(glib_major_required, glib_minor_required)])
m4_define([glib_required],       [glib_major_required.glib_minor_required.0])
m4_define([gtk3_major_required], [3])
m4_define([gtk3_minor_required], [24])
m4_define([gtk3_api_required],   [G_ENCODE_VERSION(gtk3_major_required, gtk3_minor_required)])
m4_define([gtk3_required],       [gtk3_major_required.gtk3_minor_required.0])
m4_define([gtk4_major_required], [3])
m4_define([gtk4_minor_required], [94])
m4_define([gtk4_api_required],   [G_ENCODE_VERSION(gtk4_major_required, gtk4_minor_required)])
m4_define([gtk4_required],       [gtk4_major_required.gtk4_minor_required.0])

# Other dependencies
m4_define([gdk_pixbuf_required], [2.38.0])
m4_define([gir_required],        [1.58.0])
m4_define([gjs_required],        [1.54.0])
m4_define([vala_required],       [0.42])

AC_PREREQ(autoconf_required)
AC_INIT([GPaste], [gpaste_version], [Marc-Antoine@Perennou.com], [gpaste])

AC_SUBST([PACKAGE_NAME])
AC_SUBST([PACKAGE_VERSION])

LIBGPASTE_LT_VERSION="gpaste_lt_version"
AC_SUBST([LIBGPASTE_LT_VERSION])

AC_DEFINE_UNQUOTED([GLIB_VERSION_MIN_REQUIRED], [glib_api_required], [The lowest GLib API version supported])
AC_DEFINE_UNQUOTED([GLIB_VERSION_MAX_ALLOWED],  [glib_api_required], [The highest GLib API version supported])

AC_ARG_WITH([gtk], AS_HELP_STRING([--with-gtk=3.0|4.0], [GTK API to user]), [], [with_gtk=3.0])
case "${with_gtk}" in
    3.0)
        AC_DEFINE_UNQUOTED([GDK_VERSION_MIN_REQUIRED], [gtk3_api_required], [The lowest Gdk API version supported])
        AC_DEFINE_UNQUOTED([GDK_VERSION_MAX_ALLOWED],  [gtk3_api_required], [The highest Gdk API version supported])
        GTK_REQUIRED=gtk3_required
        GTK_DEP="gtk+-3.0"
        GDK_DEP="gdk-3.0"
        GDK_X11_DEP="gdk-x11-3.0"
        ;;
#    4.0)
#        AC_DEFINE_UNQUOTED([GDK_VERSION_MIN_REQUIRED], [gtk4_api_required], [The lowest Gdk API version supported])
#        AC_DEFINE_UNQUOTED([GDK_VERSION_MAX_ALLOWED],  [gtk4_api_required], [The highest Gdk API version supported])
#        GTK_REQUIRED=gtk4_required
#        GTK_DEP="gtk+-4.0"
#        GDK_DEP="gtk+-4.0"
#        GDK_X11_DEP="gtk+-x11-4.0"
#        ;;
    *)
        AC_MSG_ERROR([Wrong gtk+ version specified: ${with_gtk}])
        ;;
esac

AC_SUBST([GTK_API], "${with_gtk:0:1}")

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
PKG_CHECK_MODULES(GTK,        [${GDK_DEP} >= ${GTK_REQUIRED} ${GTK_DEP} >= ${GTK_REQUIRED} pango])
PKG_CHECK_MODULES(GTK3,       [gtk+-3.0 >= 3.24])
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

G_PASTE_ENABLE([bash-completion],       [Disable the bash completion],   [yes])
G_PASTE_ENABLE([gnome-shell-extension], [Disable gnome-shell extension], [yes])
G_PASTE_ENABLE([x-keybinder],           [Disable the X keybinder],       [yes])
G_PASTE_ENABLE([zsh-completion],        [Disable the zsh completion],    [yes])

AS_IF([test x${enable_gnome_shell_extension} = xyes], [
    AS_IF([test x${enable_introspection} != xyes], [AC_MSG_ERROR([*** Introspection support is required to run the gnome-shell extension])])
    PKG_CHECK_MODULES(JS_EXTENSION, [mutter-clutter-8 gjs-1.0 >= gjs_required])
])

AS_IF([test x${enable_x_keybinder} = xyes], [
    PKG_CHECK_MODULES(GDK_X11, [${GDK_X11_DEP} >= ${GTK_REQUIRED}])
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

    gtk+ API:               ${with_gtk}

    vapi generator:         ${VAPIGEN}

    GObject Introspection:  ${enable_introspection}
    Vapi:                   ${enable_vala}

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
