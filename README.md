<a href="https://scan.coverity.com/projects/gpaste">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/6230/badge.svg"/>
</a>

GPaste is a clipboard management system.
See <http://www.imagination-land.org/posts/2012-12-01-gpaste-released.html> for more information about what clipboard
managers are.

A library is available for development purposes:

* `libgpaste` contains all the basic objects used by GPaste and allows you to manage preferences and the GPaste daemon.

A default daemon named `gpaste-daemon` is provided, with seven keybindings:

* show history
* pop the item from the history
* sync primary selection with clipboard
* sync clipboard with primary selection
* mark the active item as being a password
* upload the active item to a pastebin service (using wgetpaste)
* launch the graphical tool

A simple CLI interface is provided: `gpaste-client`, with three subcommands: `gpaste-client ui` which makes the graphical
tool pop, `gpaste-client applet` which starts the status icon in your notification area and `gpaste-client app-indicator` which
starts the unity application indicator.

A native gnome-shell extension is provided.

/!\ Don't forget to run `gpaste-client dr` aka `gpaste-client daemon-reexec` after upgrading GPaste to activate new functionalities ;)

You can then run `gpaste-client daemon-version` to check the correct daemon is now running.

Steps to install it after cloning (skip the `./autogen.sh` part if you're building it from a tarball):

    ./autogen.sh
    ./configure --sysconfdir=/etc
    make
    sudo make install
    sudo glib-compile-schemas /usr/share/glib-2.0/schemas/

If you also want to build the status icon, you'll have to pass `--enable-applet` to configure.

If you also want to build the unity application indicator, you'll have to pass `--enable-unity` to configure.

You can see everything I'll post about GPaste [there](http://www.imagination-land.org/tags/GPaste.html).

Latest release for GNOME 3.18 is: [GPaste 3.18.2](http://www.imagination-land.org/posts/2015-10-16-gpaste-3.18.2-released.html).

Direct link to download: <http://www.imagination-land.org/files/gpaste/gpaste-3.18.2.tar.xz>
