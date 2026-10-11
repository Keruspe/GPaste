subdir('metainfo')
subdir('completions')
subdir('control-center')
subdir('dbus')
subdir('desktop')
subdir('gsettings')
subdir('icons')

# The daemon exports the overview's search provider whether or not the
# extension is built, the standalone one included.
subdir('search-provider')

if get_option('systemd')
  subdir('systemd')
endif
