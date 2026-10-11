test(
  'eslint-launcher',
  find_program('test-eslint-launcher.py'),
  suite: 'gpaste',
)

if get_option('gnome-shell')
  test(
    'extension-locale',
    find_program('test-extension-locale.py'),
    args: [meson.project_build_root(), find_program('meson')],
    suite: 'gpaste',
  )
endif
