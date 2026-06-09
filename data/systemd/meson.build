foreach name : ['org.gnome.GPaste', 'org.gnome.GPaste.Ui', 'org.gnome.GPaste.Preferences']
  configure_file(
    input: name + '.systemd.in',
    configuration: conf,
    output: name + '.service',
    install: true,
    install_dir: userunit_dir,
  )
endforeach
