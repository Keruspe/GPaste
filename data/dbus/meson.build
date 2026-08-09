# Ship the interface the daemon exports, the way dbus-1 asks for it: it is what
# lets anything talk to GPaste — another language, another toolkit, a script —
# without a copy of our sources. Only our own interface goes here; gnome-shell
# ships org.gnome.Shell.SearchProvider2 itself.
install_data(
  gpaste_dbus_interface_xml,
  install_dir: dbus_interfaces_dir,
)

foreach name : ['org.gnome.GPaste', 'org.gnome.GPaste.Ui', 'org.gnome.GPaste.Preferences']
  configure_file(
    input: name + '.dbus.in',
    configuration: conf,
    output: name + '.service',
    install: true,
    install_dir: dbus_service_dir,
  )
endforeach
