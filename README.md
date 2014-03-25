GPaste is a clipboard management system.
See <http://www.imagination-land.org/posts/2012-12-01-gpaste-released.html> for more informations about what clipboards
manager are. 

Some libraries are available for development purpose:

* `libgpaste-core` which contains all basic objects used by GPaste
* `libgpaste-settings` which allows you to handle GPaste preferences over dconf
* `libgpaste-keybinder` provides functionalities to add custom keybindings to GPaste
* `libgpaste-daemon` allows you to write your own GPaste daemon
* `libgpaste-client` helps you integrate GPaste in your application
* `libgpaste-gnome-shell-client` helps you integrate the gnome-shell dbus api in your application
* `libgpaste-applet` allows you to write your own GPaste applet

A default daemon named `gpasted` is provided, with four keybindings:

* show history
* pop the item from the history
* sync primary selection with clipboard
* sync clipboard with primary selection

A simple CLI interface is provided: `gpaste`, with two subcommands: `gpaste settings` which makes the preferences
utility pop, `gpaste applet` which starts the status icon in your notification area and `gpaste app-indicator` which
starts the unity application indicator.

A native gnome-shell extension is provided.

/!\ Don't forget to run `gpaste dr` aka `gpaste daemon-reexec` after upgrading GPaste to activate new functionalities ;)

You can then run `gpaste daemon-version` to check the correct daemon is now running.

Steps to install it after cloning (skip the `./autogen.sh` part if you're building it from a tarball):

    ./autogen.sh
    ./configure --sysconfdir=/etc --enable-systemd
    make
    sudo make install
    sudo glib-compile-schemas /usr/share/glib-2.0/schemas/

If you also want to build the status icon, you'll have to pass `--enable-applet` to configure.

If you also want to build the unity application indicator, you'll have to pass `--enable-unity` to configure.

You can see everything I'll post about GPaste [there](http://www.imagination-land.org/tags/GPaste.html).

### If you use GNOME 3.9.90 or above

Latest release is: [GPaste 3.10](http://www.imagination-land.org/posts/2014-03-25-gpaste-3.10-released.html).

Direct link to download: <http://www.imagination-land.org/files/gpaste/gpaste-3.10.tar.xz>

### If you use GNOME 3.9.5 or below

Latest release is: [GPaste 3.3.1](http://www.imagination-land.org/posts/2014-03-22-gpaste-3.3.1-released.html).

Direct link to download: <http://www.imagination-land.org/files/gpaste/gpaste-3.3.1.tar.xz>
