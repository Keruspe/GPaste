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

You can see everything I'll post about GPaste [there](https://www.imagination-land.org/tags/GPaste.html).

Latest release for GNOME 45 to 48 is: [GPaste 45.3](https://www.imagination-land.org/posts/2025-04-14-gpaste-45.3-released.html).

Direct link to download: <https://www.imagination-land.org/files/gpaste/GPaste-45.3.tar.xz>

## Installation

### `dnf` on Fedora

```bash
sudo dnf install gpaste-ui gpaste
```

### Building from Source

```bash
# Install build dependencies
# Fedora 42
$ sudo dnf install meson ninja-build glib2-devel gtk3-devel cmake libgdk* gcr libadwaita-devel gjs-devel

# General build instructions
$ git clone git@github.com:Keruspe/GPaste.git
$ cd GPaste
$ mkdir builddir
$ cd builddir
$ meson ..
$ ninja
$ sudo ninja install
$ sudo glib-compile-schemas /usr/share/glib-2.0/schemas/
```
