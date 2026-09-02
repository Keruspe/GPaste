# In-memory GSettings backed by the schema compiled into the build tree.
test_history_env = environment()
test_history_env.set('GSETTINGS_BACKEND', 'memory')
test_history_env.set('GSETTINGS_SCHEMA_DIR', meson.project_build_root() / 'data' / 'gsettings')

# A private bus and the boilerplate around it, shared by the tests that need one.
gpaste_test_bus_lib = static_library(
  'gpaste-test-bus',
  sources: 'gpaste-test-bus.c',
  dependencies: [ gio_dep ],
)

gpaste_test_bus_dep = declare_dependency(
  link_with: gpaste_test_bus_lib,
  include_directories: include_directories('.'),
)

subdir('clipboard')
subdir('completions')
subdir('global-shortcut-client')
subdir('history')
subdir('i18n')
subdir('settings')
subdir('ui')
