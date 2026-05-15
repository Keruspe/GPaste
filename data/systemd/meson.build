foreach name : ['org.gnome.GPaste', 'org.gnome.GPaste.Ui', 'org.gnome.GPaste.Preferences']
  configure_file(
    input: name + '.systemd.in',
    configuration: conf,
    output: name + '.service',
    install: get_option('systemd'),
    install_dir: userunit_dir,
  )
endforeach
