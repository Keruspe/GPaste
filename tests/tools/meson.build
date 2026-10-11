test(
  'eslint-launcher',
  find_program('test-eslint-launcher.py'),
  suite: 'gpaste',
)

test(
  'release',
  find_program('test-release.py'),
  suite: 'gpaste',
)

test(
  'gen-keybindings',
  find_program('test-gen-keybindings.py'),
  suite: 'gpaste',
)

test(
  'search-provider',
  find_program('test-search-provider.py'),
  suite: 'gpaste',
)

# The extension's GPaste names are checked against the build's typelibs.
if get_option('introspection')
  test(
    'gi-constants',
    find_program('test-gi-constants.py'),
    args: [meson.project_build_root() / 'src/libgpaste'],
    depends: [libgpaste_gir, libgpaste_daemon_gir, libgpaste_gtk4_gir],
    suite: 'gpaste',
  )
else
  test(
    'gi-constants',
    find_program('test-gi-constants.py'),
    suite: 'gpaste',
  )
endif

if get_option('gnome-shell')
  test(
    'extension-locale',
    find_program('test-extension-locale.py'),
    args: [meson.project_build_root(), find_program('meson')],
    suite: 'gpaste',
  )
endif

if get_option('introspection')
  test(
    'gir-finish-funcs',
    find_program('test-gir-finish-funcs.py'),
    args: [libgpaste_gir[0], libgpaste_gtk4_gir[0], libgpaste_daemon_gir[0]],
    suite: 'gpaste',
  )
endif
