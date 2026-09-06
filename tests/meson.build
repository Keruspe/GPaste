gpaste_test_bus_lib = static_library(
  'gpaste-test-bus',
  sources: 'gpaste-test-bus.c',
  dependencies: [ gio_dep ],
)

gpaste_test_bus_dep = declare_dependency(
  link_with: gpaste_test_bus_lib,
  include_directories: include_directories('.'),
)

subdir('gnome-shell-client')
subdir('global-shortcut-client')
