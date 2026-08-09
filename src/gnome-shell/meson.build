shell_extension_files = [
  'actionButton.js',
  'actions.js',
  'daemon.js',
  'deleteButton.js',
  'dependencies.js',
  'dummyHistoryItem.js',
  'extension.js',
  'indicator.js',
  'item.js',
  'prefs.js',
  'prompt.js',
  'searchItem.js',
  'stateSwitch.js',
]

npm = find_program('npm', required: false)
run_eslint = find_program(
  meson.project_source_root() / 'tools/run-eslint.sh',
  required: false,
)
if npm.found() and run_eslint.found()
  test(
    'eslint',
    run_eslint,
    workdir: meson.project_source_root(),
  )
endif

configured_shell_ext_metadata = configure_file(
  input: 'metadata.json.in',
  configuration: conf,
  output: 'metadata.json',
)

install_data(
  shell_extension_files,
  configured_shell_ext_metadata,
  install_dir: get_option('datadir') / 'gnome-shell/extensions/GPaste@gnome-shell-extensions.gnome.org',
)
