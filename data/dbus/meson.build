foreach name : ['org.gnome.GPaste', 'org.gnome.GPaste.Ui', 'org.gnome.GPaste.Preferences']
  configure_file(
    input: name + '.dbus.in',
    configuration: conf,
    output: name + '.service',
    install: true,
    install_dir: dbus_service_dir,
  )
endforeach
