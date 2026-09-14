# A pipe lifeline keeps crash cleanup independent of dbus-daemon's exec label.
test_bus_supervisor = executable('gpaste-test-bus-supervisor', 'gpaste-test-bus-supervisor.c',
  dependencies: [ gio_dep ],
)

# Every C test calls g_paste_test_env_setup() first: its own settings, bus and display.
gpaste_test_env_lib = static_library(
  'gpaste-test-env',
  sources: 'gpaste-test-env.c',
  c_args: [
    '-DG_PASTE_TEST_SCHEMA_DIR="@0@"'.format(meson.project_build_root() / 'data' / 'gsettings'),
    '-DG_PASTE_TEST_BUS_SUPERVISOR="@0@"'.format(test_bus_supervisor.full_path()),
  ],
  dependencies: [ gio_dep ],
)

gpaste_test_env_dep = declare_dependency(
  link_with: gpaste_test_env_lib,
  include_directories: include_directories('.'),
)

test_env = executable('test-env', 'test-env.c', dependencies: [ gio_dep, gpaste_test_env_dep ])
test('test-env', test_env, depends: [gpaste_schemas, test_bus_supervisor], timeout: 10)

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

subdir('client')
subdir('clipboard')
subdir('completions')
subdir('global-shortcut-client')
subdir('gnome-shell')
subdir('history')
subdir('i18n')
subdir('settings')
subdir('ui')
