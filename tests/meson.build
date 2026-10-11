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

test_env = executable('test-env', 'test-env.c',
  c_args: '-DG_PASTE_TEST_BUS_SUPERVISOR="@0@"'.format(test_bus_supervisor.full_path()),
  dependencies: [ gio_dep, gpaste_test_env_dep ],
)
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

# A few suites replace a libgpaste-daemon function from the executable, which
# reaches the library's own calls only while those go through the PLT:
# -Bsymbolic-functions, in Ubuntu's default LDFLAGS for one, binds them inside
# the library, and those suites cannot run. A static library has no PLT at all,
# and its objects define what those executables define again, so they are not
# even built then.
library_static = get_option('default_library') == 'static' or (get_option('default_library') == 'both' and get_option('default_both_libraries') == 'static')
library_interposable = not library_static
foreach arg : get_option('c_link_args')
  if arg.contains('Bsymbolic')
    library_interposable = false
  endif
endforeach
if library_static
  message('Static libraries: skipping the test suites that interpose libgpaste-daemon')
elif not library_interposable
  message('Linking with -Bsymbolic: skipping the test suites that interpose libgpaste-daemon')
endif

subdir('client')
subdir('clipboard')
subdir('completions')
subdir('daemon')
subdir('global-shortcut-client')
subdir('gnome-shell')
subdir('history')
subdir('i18n')
subdir('settings')
subdir('tools')
subdir('ui')
