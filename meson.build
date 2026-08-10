project('GPaste', 'c',
  version: '51.0',
  meson_version: '>=1.1',
  default_options: [ 'warning_level=2', 'c_std=gnu17' ],
)

cc = meson.get_compiler('c')
gnome = import('gnome')
i18n = import('i18n')

apiversion = '3'

adwaita_req_version = '1.10.alpha'
gcr_req_version = '4.0.0'
glib_req_version = '2.89.0'
gtk4_req_version = '4.23.2'

gcr_dep = dependency('gcr-4', version: '>= ' + gcr_req_version)
gio_dep = dependency('gio-2.0', version: '>= ' + glib_req_version)
glib_dep = dependency('glib-2.0', version: '>= ' + glib_req_version)
gobject_dep = dependency('gobject-2.0', version: '>= ' + glib_req_version)
gtk4_dep = dependency('gtk4', version: '>= ' + gtk4_req_version)
gtk4x11_dep = dependency('gtk4-x11', version: '>= ' + gtk4_req_version)
libadwaita_dep = dependency('libadwaita-1', version: '>= ' + adwaita_req_version)
pango_dep = dependency('pango')

glib_major = glib_req_version.split('.')[0].to_int()
glib_minor = glib_req_version.split('.')[1].to_int()
gtk4_major = gtk4_req_version.split('.')[0].to_int()
gtk4_minor = gtk4_req_version.split('.')[1].to_int()

# GLib and GTK only define version macros for their stable (even) minors, so a
# development requirement has to name the stable release it leads to.
if glib_minor % 2 == 1
  glib_minor = glib_minor + 1
endif
if gtk4_minor % 2 == 1
  gtk4_minor = gtk4_minor + 1
endif

# libadwaita, on the other hand, releases every minor as stable and defines a
# macro for each, so this one must NOT be rounded: doing so would silently
# require the next release.
adwaita_major = adwaita_req_version.split('.')[0].to_int()
adwaita_minor = adwaita_req_version.split('.')[1].to_int()

# Don't make dbus/gnome-control-center a build dependency if so desired.
# They're still required at runtime though.
dbus_service_dir = get_option('dbus-services-dir')
dbus_interfaces_dir = get_option('dbus-interfaces-dir')
if dbus_service_dir == '' or dbus_interfaces_dir == ''
  dbus_dep = dependency('dbus-1')
  if dbus_service_dir == ''
    dbus_service_dir = dbus_dep.get_variable(pkgconfig: 'session_bus_services_dir')
  endif
  if dbus_interfaces_dir == ''
    dbus_interfaces_dir = dbus_dep.get_variable(pkgconfig: 'interfaces_dir')
  endif
endif

keybindings_dir = get_option('control-center-keybindings-dir')
if keybindings_dir == ''
  keybindings_dir = dependency('gnome-keybindings').get_variable(pkgconfig: 'keysdir')
endif

libgpaste_deps = [ gio_dep, glib_dep, gobject_dep ]

# Optional libsodium-based history encryption (used by the daemon's storage backend).
libsodium_dep = dependency('libsodium', required: get_option('encryption'))
if libsodium_dep.found()
  add_project_arguments('-DG_PASTE_ENABLE_ENCRYPTION', language: 'c')
endif

# Optional SQLite-based incremental history storage backend.
# 3.35 for RETURNING and UPDATE ... FROM.
sqlite_dep = dependency('sqlite3', version: '>= 3.35', required: get_option('sqlite'))
if sqlite_dep.found()
  add_project_arguments('-DG_PASTE_ENABLE_SQLITE', language: 'c')
endif

# Optional libsecret support to remember the encryption passphrase in the
# keyring. Only useful alongside encryption.
libsecret_dep = dependency('libsecret-1', required: get_option('libsecret'))
if libsodium_dep.found() and libsecret_dep.found()
  add_project_arguments('-DG_PASTE_ENABLE_LIBSECRET', language: 'c')
endif

# Optional libpwquality support to rate the passphrase strength (GNOME-style) in
# the new-encrypted-history prompt. Only useful alongside encryption.
libpwquality_dep = dependency('pwquality', required: get_option('pwquality'))
if libsodium_dep.found() and libpwquality_dep.found()
  add_project_arguments('-DG_PASTE_ENABLE_PWQUALITY', language: 'c')
endif

# The mutter (MetaSelection) clipboard backend is built alongside the extension;
# this lets the daemon expose g_paste_daemon_new_meta () for the in-shell glue.
if get_option('gnome-shell')
  add_project_arguments('-DG_PASTE_ENABLE_GNOME_SHELL', language: 'c')
endif

userunit_dir = get_option('systemd-user-unit-dir')
if userunit_dir == ''
  if get_option('systemd')
    userunit_dir = dependency('systemd').get_variable(pkgconfig: 'systemduserunitdir')
  endif
endif

if get_option('vapi') and not get_option('introspection')
  error('GIR generation has to be enabled via -Dintrospection=true to build vala bindings!')
endif

# Where the helper binaries go, relative to the prefix, and the same path made
# absolute for the compiled-in define.
pkglibexecdir_rel = get_option('libexecdir') / 'gpaste'
pkglibexecdir = get_option('prefix') / pkglibexecdir_rel

add_project_arguments(
  '-DG_LOG_DOMAIN="GPaste"',
  '-DG_LOG_USE_STRUCTURED=1',
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_@0@_@1@'.format(glib_major, glib_minor),
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_@0@_@1@'.format(glib_major, glib_minor),
  '-DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_@0@_@1@'.format(gtk4_major, gtk4_minor),
  '-DGDK_VERSION_MAX_ALLOWED=GDK_VERSION_@0@_@1@'.format(gtk4_major, gtk4_minor),
  '-DADW_VERSION_MIN_REQUIRED=ADW_VERSION_@0@_@1@'.format(adwaita_major, adwaita_minor),
  '-DADW_VERSION_MAX_ALLOWED=ADW_VERSION_@0@_@1@'.format(adwaita_major, adwaita_minor),
  '-DG_PASTE_COMPILATION',
  '-DG_PASTE_DEFAULT_HISTORY="history"',
  '-DG_PASTE_EXTENSION_NAME="GPaste@gnome-shell-extensions.gnome.org"',
  '-DG_PASTE_ICON_NAME="edit-paste"',
  '-DGETTEXT_PACKAGE="GPaste"',
  '-DLOCALEDIR="' + (get_option('prefix') / get_option('datadir') / 'locale') + '"',
  '-DPACKAGE="gpaste"',
  '-DPACKAGE_NAME="GPaste"',
  '-DPACKAGE_STRING="GPaste ' + meson.project_version() +'"',
  '-DPACKAGE_VERSION="' + meson.project_version() + '"',
  '-DPKGLIBEXECDIR="' + pkglibexecdir + '"',
  '-DVERSION="' + meson.project_version() + '"',
  language: 'c',
)

conf = configuration_data()

# Used later on to configure services&gnome-shell extension
conf.set('pkglibexecdir', pkglibexecdir)
conf.set('version', meson.project_version())
conf.set('gettext_domain', 'GPaste')

gpaste_po_dir = meson.current_source_dir() / 'po'

# The daemon's D-Bus interface, declared here because src/ is configured before
# data/: gdbus-codegen turns it into both sides of the wire under src/libgpaste,
# and data/dbus installs it.
gpaste_dbus_interface_xml = files('data/dbus/org.gnome.GPaste2.xml')

subdir('man')
subdir('po')
subdir('src')
subdir('data')
subdir('tests')

gnome.post_install(
  glib_compile_schemas: true,
  update_desktop_database: true,
)
