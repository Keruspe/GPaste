subdir('appstream')
subdir('completions')
subdir('control-center')
subdir('dbus')
subdir('desktop')
subdir('gsettings')

if get_option('gnome-shell')
  subdir('search-provider')
endif

if get_option('systemd')
  subdir('systemd')
endif

pkg = import('pkgconfig')

pkg.generate(
  libgpaste,
  name: 'libgpaste',
  filebase: 'gpaste-' + apiversion,
  subdirs: 'gpaste',
  description: 'Library to handle and communicate with GPaste',
  requires: [ gdk_pixbuf_dep, gio_dep, glib_dep, gobject_dep, gtk_dep ],
)
