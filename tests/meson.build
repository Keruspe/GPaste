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

# A few suites replace a function of one of our libraries from the executable,
# which reaches the library's own calls only while those go through the PLT:
# -Bsymbolic-functions, in Ubuntu's default LDFLAGS for one, binds them inside
# the library, and those suites cannot run. So does an optimised link-time
# optimisation by a compiler assuming no semantic interposition -- Clang's
# default, GCC's with -fno-semantic-interposition. Without LTO, such a compiler
# optimising still inlines the calls a library makes within one of its files,
# which the cases needing those skip themselves for
# (G_PASTE_TEST_LIBRARY_INLINES_OWN_CALLS). A static library has no PLT at all,
# and its objects define what those executables define again, so they are not
# even built then.
library_static = get_option('default_library') == 'static' or (get_option('default_library') == 'both' and get_option('default_both_libraries') == 'static')
library_interposable = not library_static
foreach arg : get_option('c_link_args')
  if arg.contains('Bsymbolic')
    library_interposable = false
  endif
endforeach
library_optimised = get_option('optimization') not in ['0', 'g']
library_no_interposition = library_optimised and (cc.get_id() == 'clang' or get_option('c_args').contains('-fno-semantic-interposition'))
library_lto = get_option('b_lto')
if library_lto and library_no_interposition
  library_interposable = false
endif
library_own_calls_args = (library_no_interposition) ? ['-DG_PASTE_TEST_LIBRARY_INLINES_OWN_CALLS'] : []
if library_static
  message('Static libraries: skipping the test suites that interpose our libraries')
elif not library_interposable
  message('Linking with -Bsymbolic, or optimising across files with no semantic interposition: skipping the test suites that interpose our libraries')
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
