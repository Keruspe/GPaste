GPaste is a clipboard management system.
See <http://www.imagination-land.org/posts/2012-12-01-gpaste-released.html> for more informations about what clipboards
manager are. 

Some libraries are available for development purpose:

* `libgpaste-core` which contains all basic objects used by GPaste
* `libgpaste-settings` which allows you to handle GPaste preferences over dconf
* `libgpaste-keybinder` provides functionnalities to add custom keybindings to GPaste
* `libgpaste-daemon` allows you to write your own GPaste daemon
* `libgpaste-client` helps you integrate GPaste in your application

A default daemon named `gpasted` is provided, with two keybindings to show history and paste + pop the item from the
history.

A simple CLI interface is provided: `gpaste`, with two subcommands: `gpaste settings` which makes the preferences
utility pop, and `gpaste-applet` which starts the legacy applet in your notification area.

A native gnome-shell extension is provided.

/!\ Don't forget to run `gpaste dr` aka `gpaste daemon-reexec` after upgrading GPaste to activate new functionalities ;)

Steps to install it after cloning (skip the `./autogen.sh` part if you're building it from a tarball):

    ./autogen.sh
    ./configure --sysconfdir=/etc --enable-systemd
    make
    sudo make install
    sudo glib-compile-schemas /usr/share/glib-2.0/schemas/

If you also want to build the legacy applet, you'll have to pass `--enable-vala --enable-applet` to configure.

You can see everything I'll post about GPaste [there](http://www.imagination-land.org/tags/GPaste.html).

### If you use GNOME 3.9.90 or above

Latest release is: [GPaste 3.6](http://www.imagination-land.org/posts/2013-10-17-gpaste-3.6-released.html).

Direct link to download: <http://www.imagination-land.org/files/gpaste-3.6.tar.xz>

### If you use GNOME 3.9.5 or below

Latest release is: [GPaste 3.2.1](http://www.imagination-land.org/posts/2013-10-16-gpaste-3.2.1-released.html).

Direct link to download: <http://www.imagination-land.org/files/gpaste-3.2.1.tar.xz>
