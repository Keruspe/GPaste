# `data/`

Non-code resources: D-Bus service files (`dbus/`), `.desktop` entries, GSettings schemas (`gsettings/`), systemd user units, AppStream metadata, shell completions (`completions/`).

**The release notes live outside the translated metainfo.** `data/metainfo/org.gnome.GPaste.Ui.metainfo.xml.in` carries `<releases type="external"/>` and the notes themselves sit in `data/metainfo/org.gnome.GPaste.Ui.releases.xml`, installed to `$datadir/metainfo/releases/`, where AppStream looks for `<component-id>.releases.xml`. That file is deliberately absent from `POTFILES.in`: the notes of a version nobody runs are never shown to anyone, yet every one of them sat in the catalog forever and fuzzied across every language the moment an old entry was touched. A new release adds its `<release>` there and nowhere else.

**Shell completions** (`data/completions/gpaste-client` for bash, `_gpaste-client` for zsh, `gpaste-client.fish` for fish — each behind its own `*-completion` meson option) mirror `gpaste-client`'s `commands[]` table, so a new verb or alias has to be added to all three and to `man/1/gpaste-client.1` — `show_help()` is printed from the table itself and needs nothing. `meson test -C build completions` fails until they agree. Two rules every one of them follows:

- **Never activate the daemon from a completion.** Anything that needs the daemon first checks `org.freedesktop.DBus.NameHasOwner org.gnome.GPaste` with `gdbus` and gives up when nothing owns it — otherwise hitting tab would start a daemon behind the user's back and block the shell while it loads the history.
- **History names come off disk**, not from `list-histories`: one file per history, `<name>.{xml,xmls,db,dbs}` under `$XDG_DATA_HOME/GPaste` (falling back to `.../gpaste`, the same `PACKAGE_NAME`/`PACKAGE` rule as `g_paste_util_get_history_dir_path`), so they complete instantly with no daemon at all.

`gpaste-client` reads stdin whenever it is not a tty, so every invocation from a completion redirects `</dev/null` — completion does not always run with a terminal on stdin, and it would otherwise block waiting for EOF. Item arguments complete to uuids, or to indexes when `--use-index` is in effect.
