configure_file(
  input: 'org.gnome.GPaste.Ui.systemd.in',
  configuration: conf,
  output: 'org.gnome.GPaste.Ui.service',
  install: get_option('systemd'),
  install_dir: userunit_dir,
)

configure_file(
  input: 'org.gnome.GPaste.Preferences.systemd.in',
  configuration: conf,
  output: 'org.gnome.GPaste.Preferences.service',
  install: get_option('systemd'),
  install_dir: userunit_dir,
)

configure_file(
  input: 'org.gnome.GPaste.systemd.in',
  configuration: conf,
  output: 'org.gnome.GPaste.service',
  install: get_option('systemd'),
  install_dir: userunit_dir,
)
