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
  dependencies: [ gio_dep, gpaste_test_env_dep ],
)

gpaste_test_bus_dep = declare_dependency(
  link_with: gpaste_test_bus_lib,
  include_directories: include_directories('.'),
)

# The crypto_pwhash () test-history and test-clipboard derive every key through,
# at libsodium's minimum cost.
gpaste_test_pwhash_sources = (libsodium_dep.found()) ? files('gpaste-test-pwhash.c') : []

# The widget lookups, dialog waits and stand-in saves the suites driving widgets
# share: the graphical tool's, and test-prompt-adw for the daemon's own prompt.
gpaste_test_gtk_lib = static_library(
  'gpaste-test-gtk',
  sources: 'gpaste-test-gtk.c',
  dependencies: [ gtk4_dep, gpaste_test_bus_dep, libadwaita_dep, libgpaste_internal_dep ],
)

gpaste_test_gtk_dep = declare_dependency(
  link_with: gpaste_test_gtk_lib,
  include_directories: include_directories('.'),
  dependencies: [ gtk4_dep, gpaste_test_bus_dep, libadwaita_dep ],
)

# A few suites replace a function of one of our libraries from the executable,
# which reaches the library's own calls only while those go through the PLT:
# -Bsymbolic-functions, in Ubuntu's default LDFLAGS for one, binds them inside
# the library, and those suites cannot run. So does link-time optimisation by a
# compiler assuming no semantic interposition -- Clang's default when it
# optimises, GCC's with -fno-semantic-interposition. Without LTO, such a
# compiler still binds the calls a library makes within one of its files
# inside it -- GCC told so at any optimisation level, -O0 included --, which
# the cases needing those skip themselves for
# (G_PASTE_TEST_LIBRARY_INLINES_OWN_CALLS). A static library has no PLT at all,
# and its objects define what those executables define again, so those are not
# even built then (library_static); one whose own copy of a library's file
# defines everything it would take from that file links and runs all the same,
# binding to its copy at link time.
library_static = get_option('default_library') == 'static' or (get_option('default_library') == 'both' and get_option('default_both_libraries') == 'static')
library_interposable = not library_static
foreach arg : get_option('c_link_args')
  if arg.contains('Bsymbolic')
    library_interposable = false
  endif
endforeach
library_optimised = get_option('optimization') not in ['0', 'g']
library_no_interposition = (library_optimised and cc.get_id() == 'clang') or get_option('c_args').contains('-fno-semantic-interposition')
# Link-time optimisation is as often asked for in the flags -- a distribution's
# CFLAGS and LDFLAGS -- as through b_lto.
library_lto = get_option('b_lto')
foreach arg : get_option('c_args') + get_option('c_link_args')
  if arg.startswith('-flto')
    library_lto = true
  elif arg == '-fno-lto'
    library_lto = false
  endif
endforeach
if library_lto and library_no_interposition
  library_interposable = false
endif
library_own_calls_args = (library_no_interposition) ? ['-DG_PASTE_TEST_LIBRARY_INLINES_OWN_CALLS'] : []
if library_static
  message('Static libraries: skipping the test suites that interpose a library\'s own calls')
elif not library_interposable
  message('Linking with -Bsymbolic, or optimising across files with no semantic interposition: skipping the test suites that interpose a library\'s own calls')
endif

subdir('build')
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
