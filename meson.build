project('GPaste', 'c',
  version: '45.6',
  meson_version: '>=1.1',
  default_options: [ 'warning_level=2', 'c_std=gnu17' ],
)

cc = meson.get_compiler('c')
gnome = import('gnome')
i18n = import('i18n')

apiversion = '2'

adwaita_req_version = '1.8'
gcr_req_version = '3.90.0'
gjs_req_version = '1.78.0'
glib_req_version = '2.84.0'
gtk4_req_version = '4.12.0'

gcr_dep = dependency('gcr-4', version: '>= ' + gcr_req_version)
gio_dep = dependency('gio-2.0', version: '>= ' + glib_req_version)
glib_dep = dependency('glib-2.0', version: '>= ' + glib_req_version)
gobject_dep = dependency('gobject-2.0', version: '>= ' + glib_req_version)
gtk4_dep = dependency('gtk4', version: '>= ' + gtk4_req_version)
gtk4x11_dep = dependency('gtk4-x11', version: '>= ' + gtk4_req_version)
libadwaita_dep = dependency('libadwaita-1', version: '>= ' + adwaita_req_version)

glib_major = glib_req_version.split('.')[0].to_int()
glib_minor = glib_req_version.split('.')[1].to_int()
gtk4_major = gtk4_req_version.split('.')[0].to_int()
gtk4_minor = gtk4_req_version.split('.')[1].to_int()

if glib_minor % 2 == 1
  glib_minor = glib_minor + 1
endif
if gtk4_minor % 2 == 1
  gtk4_minor = gtk4_minor + 1
endif

# Don't make dbus/gnome-control-center a build dependency if so desired.
# They're still required at runtime though.
dbus_service_dir = get_option('dbus-services-dir')
if dbus_service_dir == ''
  dbus_service_dir = dependency('dbus-1').get_variable(pkgconfig: 'session_bus_services_dir')
endif

keybindings_dir = get_option('control-center-keybindings-dir')
if keybindings_dir == ''
  keybindings_dir = dependency('gnome-keybindings').get_variable(pkgconfig: 'keysdir')
endif

libgpaste_deps = [ gio_dep, glib_dep, gobject_dep ]

if get_option('gnome-shell')
  gjs_dep = dependency('gjs-1.0', version: '>= ' + gjs_req_version)
  pango_dep = dependency('pango')
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

keybinder_deps = []

if get_option('x-keybinder')
  add_project_arguments(
    '-DENABLE_X_KEYBINDER=1',
    language: 'c',
  )

  x11_dep = dependency('x11')
  xi_dep = dependency('xi')
  keybinder_deps += [ gtk4x11_dep, xi_dep, x11_dep ]
endif

add_project_arguments(
  '-DG_LOG_DOMAIN="GPaste"',
  '-DG_LOG_USE_STRUCTURED=1',
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_@0@_@1@'.format(glib_major, glib_minor),
  '-DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_@0@_@1@'.format(glib_major, glib_minor),
  '-DG_PASTE_COMPILATION',
  '-DG_PASTE_DEFAULT_HISTORY="history"',
  '-DG_PASTE_EXTENSION_NAME="GPaste@gnome-shell-extensions.gnome.org"',
  '-DG_PASTE_ICON_NAME="edit-paste"',
  '-DGDK_DISABLE_DEPRECATED',
  '-DGETTEXT_PACKAGE="GPaste"',
  '-DGTK_DISABLE_DEPRECATED',
  '-DLOCALEDIR="' + (get_option('prefix') / get_option('datadir') / 'locale') + '"',
  '-DPACKAGE="gpaste"',
  '-DPACKAGE_NAME="GPaste"',
  '-DPACKAGE_STRING="GPaste ' + meson.project_version() +'"',
  '-DPACKAGE_VERSION="' + meson.project_version() + '"',
  '-DPKGLIBEXECDIR="' + (get_option('prefix') / get_option('libexecdir')) + '"',
  '-DVERSION="' + meson.project_version() + '"',
  language: 'c',
)

conf = configuration_data()

# Used later on to configure services&gnome-shell extension
conf.set('GETTEXT_PACKAGE', 'GPaste')
conf.set('pkglibexecdir', get_option('prefix') / get_option('libexecdir') / 'gpaste')
conf.set('version', meson.project_version())
conf.set('gettext_domain', 'GPaste')

gpaste_po_dir = meson.current_source_dir() / 'po'

subdir('man')
subdir('po')
subdir('src')
subdir('data')
subdir('tests')

gnome.post_install(
  glib_compile_schemas: true,
  update_desktop_database: true,
)
