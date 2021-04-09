project('GPaste',
  'c',
  version: '3.40.0',
  meson_version: '>=0.50.0',
)

apiversion = '1.0'
gpaste_soversion = '13.0.4'

glib_req='>=2.64.0'
gdk_pixbuf_req='>=2.38.0'
gjs_req='>=1.54.0'
gtk3_req='>=3.24.0'
mutter_clutter_req='>=40.0'

appstream_dep = dependency('appstream-glib')
gdk_dep = dependency('gdk-3.0', version: gtk3_req)
gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0', version: gdk_pixbuf_req)
gio_dep = dependency('gio-2.0', version: glib_req)
glib_dep = dependency('glib-2.0', version: glib_req)
gobject_dep = dependency('gobject-2.0', version: glib_req)
gtk_dep = dependency('gtk+-3.0', version: gtk3_req)
gtk3_dep = dependency('gtk+-3.0', version: '>=3.24')
pango_dep = dependency('pango')

# Don't make dbus/gnome-control-center a build dependency if so desired.
# They're still required at runtime though.
dbus_service_dir = get_option('dbus-services-dir')
if dbus_service_dir == ''
  dbus_service_dir = dependency('dbus-1').get_pkgconfig_variable('session_bus_services_dir')
endif

keybindings_dir = get_option('control-center-keybindings-dir')
if keybindings_dir == ''
  keybindings_dir = dependency('gnome-keybindings').get_pkgconfig_variable('keysdir')
endif

libgpaste_deps = [ gdk_dep, gdk_pixbuf_dep, glib_dep, gtk_dep ]

if get_option('introspection')
  mutter_clutter_dep = dependency('mutter-clutter-8', version: mutter_clutter_req)
  gjs_dep = dependency('gjs-1.0', version: gjs_req)
endif

userunit_dir = get_option('systemd-user-unit-dir')
if userunit_dir == ''
  if get_option('systemd')
    userunit_dir = dependency('systemd').get_pkgconfig_variable('systemduserunitdir')
  endif
endif

if get_option('vapi') and not get_option('introspection')
  error('GIR generation has to be enabled via -Dintrospection=true to build vala bindings!')
endif

if get_option('x-keybinder')
  add_project_arguments(
    '-DENABLE_X_KEYBINDER=1',
    language: 'c',
  )

  gdk_x11_dep = dependency('gdk-x11-3.0', version: gtk3_req)
  x11_dep = dependency('x11')
  xi_dep = dependency('xi')
  libgpaste_deps += [ gdk_x11_dep, xi_dep, x11_dep ]
endif

add_project_arguments(
  '-DG_LOG_DOMAIN="GPaste"',
  '-DG_LOG_USE_STRUCTURED=1',
  '-DG_PASTE_COMPILATION',
  '-DG_PASTE_DEFAULT_HISTORY="history"',
  '-DG_PASTE_EXTENSION_NAME="GPaste@gnome-shell-extensions.gnome.org"',
  '-DG_PASTE_ICON_NAME="edit-paste"',
  '-DGDK_DISABLE_DEPRECATED',
  '-DGETTEXT_PACKAGE="GPaste"',
  '-DGTK_API=3',
  '-DGTK_DISABLE_DEPRECATED',
  '-DLOCALEDIR="' + join_paths(get_option('prefix'), get_option('datadir'), 'locale') + '"',
  '-DPACKAGE="GPaste"',
  '-DPACKAGE_NAME="GPaste"',
  '-DPACKAGE_STRING="GPaste ' + meson.project_version() +'"',
  '-DPACKAGE_VERSION="' + meson.project_version() + '"',
  '-DPKGLIBEXECDIR="' + join_paths(get_option('prefix'), get_option('libexecdir')) +'"',
  '-DVERSION="' + meson.project_version() + '"',
  language: 'c',
)

conf = configuration_data()

# Used later on to configure services&gnome-shell extension
conf.set('GETTEXT_PACKAGE', 'GPaste')
conf.set('pkglibexecdir', join_paths(get_option('prefix'), get_option('libexecdir'), 'gpaste'))
conf.set('version', meson.project_version())
conf.set('gettext_domain', 'GPaste')

gnome = import('gnome')
i18n = import('i18n')

gpaste_po_dir = join_paths(meson.current_source_dir(), 'po')

subdir('man')
subdir('po')
subdir('src')
subdir('data')
subdir('tests')
