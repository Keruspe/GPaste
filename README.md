<a href="https://scan.coverity.com/projects/gpaste">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/6230/badge.svg"/>
</a>

GPaste is a clipboard management system.
See <https://www.imagination-land.org/posts/2012-12-01-gpaste-released.html> for more information about what clipboard
managers are.

Translators can use [weblate](https://hosted.weblate.org/projects/gpaste/gpaste/) to contribute.

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

A simple CLI interface is provided: `gpaste-client`, with a subcommands: `gpaste-client ui` which makes the graphical tool pop.

A native gnome-shell extension is provided.

/!\ Don't forget to run `gpaste-client dr` aka `gpaste-client daemon-reexec` after upgrading GPaste to activate new functionalities ;)

You can then run `gpaste-client daemon-version` to check the correct daemon is now running.

Steps to install it after cloning (skip the `./autogen.sh` part if you're building it from a tarball):

    ./autogen.sh
    ./configure --sysconfdir=/etc
    make
    sudo make install
    sudo glib-compile-schemas /usr/share/glib-2.0/schemas/

You can see everything I'll post about GPaste [there](https://www.imagination-land.org/tags/GPaste.html).

Latest release for GNOME 40 is: [GPaste 3.40.0](https://www.imagination-land.org/posts/2021-04-09-gpaste-3.40.0-released.html).

Direct link to download: <https://www.imagination-land.org/files/gpaste/gpaste-3.40.0.tar.xz>
