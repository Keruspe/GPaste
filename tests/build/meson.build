# Without the G_PASTE_COMPILATION every one of GPaste's own C sources has, which
# is what the installed headers' private parts hide behind. The project's other
# C defines -- the feature switches, GETTEXT_PACKAGE, the version pins -- stay:
# meson gives a target no way to drop a project argument, and an -U list of
# them here would drift from meson.build's. cxx-headers below is the one built
# as a consumer builds, with none of them, the project arguments being C's
# alone.
test_c_headers = executable(
  'test-c-headers',
  sources: 'test-c-headers.c',
  c_args: ['-UG_PASTE_COMPILATION'],
  dependencies: [ gpaste_daemon_internal_dep, gtk4_dep, libadwaita_dep, libgpaste_gtk4_internal_dep ],
)

test('c-headers', test_c_headers)

# Only where a C++ compiler is found: GPaste builds no C++, and needing one to
# check its headers compile as C++ would be a build dependency for nothing else.
if add_languages('cpp', required: false, native: false)
  test_cxx_headers = executable(
    'test-cxx-headers',
    sources: 'test-cxx-headers.cc',
    dependencies: [ gpaste_daemon_internal_dep, gtk4_dep, libadwaita_dep, libgpaste_gtk4_internal_dep ],
  )

  test('cxx-headers', test_cxx_headers)
endif
