GPaste is a clipboard management system.
See <http://www.imagination-land.org/posts/2012-12-01-gpaste-released.html> for more information about what clipboard
managers are.

Some libraries are available for development purposes:

* `libgpaste-core` contains all the basic objects used by GPaste
* `libgpaste-settings` allows you to handle GPaste preferences over dconf
* `libgpaste-keybinder` provides functionalities to add custom keybindings to GPaste
* `libgpaste-daemon` allows you to write your own GPaste daemon
* `libgpaste-client` helps you integrate GPaste in your application
* `libgpaste-gnome-shell-client` helps you integrate the gnome-shell dbus API in your application
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
    ./configure --sysconfdir=/etc
    make
    sudo make install
    sudo glib-compile-schemas /usr/share/glib-2.0/schemas/

If you also want to build the status icon, you'll have to pass `--enable-applet` to configure.

If you also want to build the unity application indicator, you'll have to pass `--enable-unity` to configure.

If your distribution does not provide a appdata-tools package (like ubuntu 14.04), you can use `ubuntu-patches/0001-ubuntu-disable-appdata-stuff.patch`

If your distribution ships with gnome-settings 3.8 and thus a patched version of gnome-shell 3.10 (like ubuntu 14.04), you can use `0002-ubuntu-fix-for-ubuntu-breaking-gnome-shell-API-compa.patch`

You can see everything I'll post about GPaste [there](http://www.imagination-land.org/tags/GPaste.html).

Latest release for GNOME 3.12 is: [GPaste 3.12](http://www.imagination-land.org/posts/2014-05-02-gpaste-3.12-released.html).

Direct link to download: <http://www.imagination-land.org/files/gpaste/gpaste-3.12.tar.xz>
