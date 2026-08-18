# GPaste

GPaste is a clipboard management system for GNOME: a daemon that remembers what
you copy, plus a GTK 4 interface, a GNOME Shell extension and a CLI to get it
back.

New to the idea? [This post](https://www.imagination-land.org/posts/2012-12-01-gpaste-released.html)
explains what clipboard managers are for.

## Features

- **Persistent history** across sessions, with several named histories you can
  switch between, back up and delete independently.
- **Everything you copy**: plain text, rich text and HTML, images (de-duplicated
  by checksum, kept as files or as database blobs depending on the storage
  backend), URIs and colours.
- **Encryption at rest** — an optional libsodium-backed history, with the
  passphrase optionally kept in the keyring.
- **Pluggable storage**: an XML file, a per-history SQLite database that
  persists incrementally, or nothing at all. Switch at any time from the
  preferences or with `gpaste-client migrate`.
- **Passwords**: mark an item as a password to have it shown as a name instead
  of its contents, and excluded from the plain-text history.
- **Favourites**: pin an item and the history never drops it on its own — the
  size and memory limits give way instead. Filter the list down to the pinned
  items from the UI, the extension or `gpaste-client --favourites`.
- **Search, merge and edit** items from the UI, the extension or the CLI.
- **GNOME Shell integration** through a native extension, with an optional
  experimental mode that runs the daemon inside the Shell itself.
- **Global shortcuts** through the XDG portal, so they work the same on Wayland
  and X11.

## Installation

### Fedora

```bash
sudo dnf install gpaste gpaste-ui gnome-shell-extension-gpaste
```

Then enable the extension in the Extensions app.

### From source

```bash
# Build dependencies (Fedora)
sudo dnf install meson ninja-build gcc gettext-devel \
                 glib2-devel gtk4-devel libadwaita-devel gcr-devel \
                 dbus-devel gnome-control-center

# Optional, but on by default when found
sudo dnf install libsodium-devel sqlite-devel libsecret-devel \
                 libpwquality-devel gobject-introspection-devel vala

git clone https://github.com/Keruspe/GPaste.git
cd GPaste
meson setup build
ninja -C build
sudo ninja -C build install
sudo glib-compile-schemas /usr/share/glib-2.0/schemas/
```

GPaste currently needs GLib 2.90, GTK 4.24, libadwaita 1.10 and gcr 4.

#### Build options

Pass these to `meson setup` (or `meson configure build`) as `-Doption=value`.
The `feature` ones default to `auto`: enabled when the dependency is present,
skipped otherwise.

| Option | Default | What it controls |
|---|---|---|
| `encryption` | `auto` | libsodium-based history encryption |
| `sqlite` | `auto` | the SQLite storage backends (needs SQLite ≥ 3.35) |
| `libsecret` | `auto` | remember the encryption passphrase in the keyring |
| `pwquality` | `auto` | rate passphrase strength in the new-history prompt |
| `gnome-shell` | `true` | the GNOME Shell extension and the mutter clipboard backend |
| `introspection` | `true` | GIR data |
| `vapi` | `true` | Vala bindings (requires `introspection`) |
| `systemd` | `true` | the systemd user unit |
| `bash-completion`, `zsh-completion`, `fish-completion` | `true` | shell completions |

For a minimal build:

```bash
meson setup build -Dgnome-shell=false -Dintrospection=false -Dvapi=false
```

## Usage

### Default shortcuts

| Shortcut | Action |
|---|---|
| <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>H</kbd> | Show the history |
| <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>G</kbd> | Launch the graphical tool |
| <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>V</kbd> | Pop the first item off the history |
| <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>S</kbd> | Mark the active item as a password |
| <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>O</kbd> | Sync the clipboard to the primary selection |
| <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>P</kbd> | Sync the primary selection to the clipboard |
| <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>U</kbd> | Upload the active item to a pastebin (needs `wgetpaste`) |

All of them are configurable in the preferences.

### Command line

```bash
gpaste-client                       # print the history
gpaste-client --oneline --use-index # ... one line per item, numbered
gpaste-client search <pattern>      # print matching items
gpaste-client select <uuid>         # put an item back in the clipboard
gpaste-client add <text>            # copy some text
echo hello | gpaste-client          # copy from a pipe
gpaste-client file <path>           # copy a file's contents
gpaste-client delete <uuid>         # forget one item
gpaste-client empty                 # forget everything

gpaste-client ui                    # open the graphical tool
gpaste-client preferences           # open the settings
gpaste-client migrate               # switch storage backend
gpaste-client change-passphrase     # re-key an encrypted history
```

`gpaste-client help` lists every subcommand; see also `man gpaste-client`.

Items are addressed by UUID by default. Pass `--use-index` to use their
position in the history instead.

### After upgrading

Restart the daemon so the new version takes over:

```bash
gpaste-client daemon-reexec
gpaste-client daemon-version   # confirm which one is running
```

## Development

Three libraries are installed, each with its own pkg-config file, headers,
GIR and VAPI:

| Library | pkg-config | GIR | Contents |
|---|---|---|---|
| `libgpaste-3` | `gpaste-3` | `GPaste-3` | Core objects, settings, and the D-Bus client for the daemon |
| `libgpaste-daemon` | `gpaste-daemon` | `GPasteDaemon-1` | The clipboard item hierarchy, the history and the storage backends |
| `libgpaste-gtk4` | `gpaste-gtk4` | `GPasteGtk-4` | GTK 4 / libadwaita widgets and helpers |

Each has a single umbrella header — `<gpaste.h>`, `<gpaste-daemon.h>` and
`<gpaste-gtk4.h>` — which is the only one you may include directly.

Run the test suite from the build directory:

```bash
ninja -C build test
```

[`AGENTS.md`](AGENTS.md) documents the architecture, coding style and
repository conventions in more detail.

## Contributing

Translations go through [Weblate](https://hosted.weblate.org/projects/gpaste/gpaste/).

Bug reports and patches are welcome on
[GitHub](https://github.com/Keruspe/GPaste).

## Releases

The latest release for GNOME 50 is
[GPaste 50.7](https://www.imagination-land.org/posts/2026-08-02-gpaste-50.7-released.html)
([tarball](https://www.imagination-land.org/files/gpaste/GPaste-50.7.tar.xz)).

Release announcements and everything else about GPaste are
[on my blog](https://www.imagination-land.org/tags/GPaste.html). See
[`NEWS`](NEWS) for the full changelog.
