[![Coverity Scan Build Status](https://scan.coverity.com/projects/6230/badge.svg)](https://scan.coverity.com/projects/gpaste)
[![Build Status](https://travis-ci.org/Keruspe/GPaste.svg?branch=master)](https://travis-ci.org/Keruspe/GPaste)

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

Steps to install it after cloning:

    mkdir builddir
    cd builddir
    meson ..
    ninja
    sudo ninja install
    sudo glib-compile-schemas /usr/share/glib-2.0/schemas/

You can see everything I'll post about GPaste [there](https://www.imagination-land.org/tags/GPaste.html).

Latest release for GNOME 42 is: [GPaste 42.0](https://www.imagination-land.org/posts/2022-03-21-gpaste-42.1-released.html).

Direct link to download: <https://www.imagination-land.org/files/gpaste/GPaste-42.1.tar.xz>
