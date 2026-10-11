# Ship the interface the daemon exports, the way dbus-1 asks for it: it is what
# lets anything talk to GPaste — another language, another toolkit, a script —
# without a copy of our sources. Only our own interface goes here; gnome-shell
# ships org.gnome.Shell.SearchProvider2 itself.
install_data(
  gpaste_dbus_interface_xml,
  install_dir: dbus_interfaces_dir,
)

# The systemd unit a service activates through is named only when it is
# installed: a dbus-broker, or a dbus-daemon in systemd activation mode, asks
# systemd for the unit a service file names rather than run its Exec=, and a
# unit that is not there starts nothing.
foreach name : ['org.gnome.GPaste', 'org.gnome.GPaste.Ui', 'org.gnome.GPaste.Preferences']
  service_conf = configuration_data()
  service_conf.set('pkglibexecdir', pkglibexecdir)
  service_conf.set('systemd_service', get_option('systemd') ? 'SystemdService=' + name + '.service' : '')

  configure_file(
    input: name + '.dbus.in',
    configuration: service_conf,
    output: name + '.service',
    install: true,
    install_dir: dbus_service_dir,
  )
endforeach
