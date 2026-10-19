# `tests/`

Every C test binary calls `g_paste_test_env_setup()` (`tests/gpaste-test-env.c`, linked through `gpaste_test_env_dep`) first in `main()`, before `gtk_init_check()`, `g_test_init()` or any `GSettings`, and ends with `return g_paste_test_env_run ();`. It detaches the process from the desktop session whether meson or a shell launched it: memory `GSettings` on the schemas compiled into the build tree (hence `depends: [gpaste_schemas, test_bus_supervisor]` on each C test), a private `XDG_RUNTIME_DIR` with `XDG_DATA_HOME`, `XDG_CONFIG_HOME` and `XDG_CACHE_HOME` beneath it (set before any library caches them, and inherited by a daemon the test spawns -- but a suite whose `g_test_init()` takes `G_TEST_OPTION_ISOLATE_DIRS` has them, `HOME` and `XDG_RUNTIME_DIR` pointed at `/dev/null` in its environment, GLib keeping per-case ones in process under its own `/tmp/test_*`, which a crashed suite leaves behind), no `DISPLAY`/`WAYLAND_DISPLAY`, and a private `dbus-daemon` serving as both session and system bus. `G_PASTE_TEST_ENV_OWN_BUS` is for a test that brings up its own `GTestDBus` per fixture: both bus addresses then point at a socket that does not exist. `G_PASTE_TEST_ENV_DISPLAY` starts a private `Xvfb` with `GDK_BACKEND=x11`; `g_paste_test_env_has_display()` is FALSE when `Xvfb` is not installed, and such a test skips rather than use the session's display. The server is never killed, since GDK exits the process when its display goes away: `-terminate` ends it when the process disconnects, `PR_SET_PDEATHSIG` when it never connected. Settings and GTK environment therefore do not belong in a test's meson `env`. The default bus has no activation directories and runs under `gpaste-test-bus-supervisor`, an infrastructure helper rather than a test binary, which an `OWN_BUS` test starts too, without a bus. The test holds a pipe to the supervisor: a byte written on it at normal exit, or EOF after a crash, kills and reaps the bus and removes the test's directories, which a crashed test never reaches; it ignores the `SIGTERM` a test runner's timeout sends the whole group, for the same reason. A byte rather than EOF ends a normal run because a process forked without exec, such as `GTestDBus`'s watcher, keeps a copy of the pipe until the test exits. This does not rely on `PR_SET_PDEATHSIG`, which a security-domain transition at dbus-daemon exec can clear. dbus-daemon refuses a socket path past 99 characters, so a temporary directory too long to hold the runtime directory and a socket -- a package build tree's `TMPDIR`, or the `G_TEST_TMPDIR` a trap child of a `G_TEST_OPTION_ISOLATE_DIRS` suite gets inside its parent's, which GLib prefers -- is replaced by a short one under `/tmp` before GLib caches it. `GTestDBus` finalization waits for GIO's session singleton even after `stop()`, so it is reserved for suites that own their fixture connections. `test-env` holds a real `g_bus_get()` connection across teardown, traps an aborting child to verify that it got a bus, that the bus releases inherited output pipes and that its directories are gone (its `TMPDIR` points into the parent's runtime directory, where that can be checked), runs a child under a `TMPDIR`, then a `G_TEST_TMPDIR`, deeper than a socket path can be and checks that the short directory it got instead goes with it, has a supervisor whose `dbus-daemon` cannot start remove its directories all the same, and checks that the three data directories sit inside the runtime directory; it must finish within ten seconds.

Tests live under `tests/`. `tests/history/` unit-tests the `GPasteHistory` model
(add/dedup/size-enforcement/remove/select, and which of those announce a password
leaving it) against an in-memory `GSettings` and a
throwaway `XDG_DATA_HOME`. The `eslint`
test lints the GNOME Shell extension JS, `eslint-launcher` runs
`tools/run-eslint.sh` in scratch checkouts against a fake npm that installs
nothing -- from a path with spaces, from inside `tools/`, as `tools/run-eslint.sh`
under a `CDPATH` with a `tools/` of its own, and over a root `node_modules` link
that does not resolve, again over an install older than its lock, and
under meson's `G_PASTE_LINT_NO_INSTALL` with nothing installed -- and `completions` checks the shell completions and the
man page against `gpaste-client`'s own list of verbs, and what zsh and fish list of an item for control characters. These three are scripts
that touch no settings, bus or display. `tests/clipboard/` exercises the shared update lifecycle and the clipboards manager with a mock provider, including text put on every selection without becoming an item (`select_text`), overlapping reads, superseded callbacks, typed cache commits, cross-selection copy ordering, rich-text refreshes, the selections a password's
history entry leaving is taken off, and password expiry across a persisted handover; it needs no display or desktop session bus; the common test environment still requires `dbus-daemon` for isolation. Its password countdowns are real GLib timeouts, so the suite runs under a 120-second Meson timeout. The test advances the ready time of the production manager’s forced-expiry source, exercising the shipped object under owner churn without waiting sixty seconds. The timeout-edit table names each scenario alongside its timings and expected contents. Its configuration and data directories are isolated, including the file-backend handover test. Every case starts from the schema's defaults (`make_settings()`): the memory backend is shared by the whole binary, so a key one case sets would otherwise reach every case after it. `test-clipboard-gdk` drives the GDK backend's own ownership and read callbacks behind GTK mocks, with no display or live clipboard -- the formats deadline telling a released selection from an owner merely slow to serve `TARGETS` -- and from a display that could not be asked which -- included, the shorter grace a selection X already says nobody owns waits with too, and the fallback that reads the hints off the formats where the workaround cannot convert the selection at all; which of an offer's formats is read (text before an image, a bare `text/plain` read as UTF-8, by a sync too, and an empty one as the empty text), that an image read re-owns nothing, that an update its provider can no longer make still answers its caller (`unmade`), and that a claim GDK refuses leaves the cache unknown -- `test-clipboard-hints` covers the update API the X11 hints workaround reads through -- the sensitive offer read and the special and sensitive halves of the MIME-reading policy -- and `test-clipboard-x11-hints` covers the workaround itself, compiling `src/daemon/gpaste-clipboard-x11-hints.c` in (it belongs to the daemon executable, not to a library) and running it against the private Xvfb and a selection owner of the suite's own on a second connection, driven from the same pump as the main loop: what the selection answers about the hint and what each non-answer -- a refusal, an INCR reply, a silence -- is read as, the ownership query, the requestor window a conversion given up on leaves standing for a late reply, and the timestamps ICCCM has an owner judge a conversion by, the bootstrap case included, which is the one the server's clock is read for -- a read a signal interrupts included, which the suite stages by interposing `g_poll()` and `XCheckIfEvent()`, the workaround being compiled into it. Both go when that workaround does. `test-clipboard-meta`, built with the GNOME Shell extension, drives the mutter backend against a `MetaSelection` of its own (`meta_selection_new (NULL)`) with GTK never initialised, as in gnome-shell: images, colours and file lists offered alone are recorded and an image is published as a PNG that reads back, a NUL-terminated text is the text before it, a read stops past `max-memory-usage` however much its owner serves, and what was published is still served once the clipboard that published it is gone, as when gnome-shell disposes its daemon. The least an update needs of a provider is shared by the two as `gpaste-test-clipboard-provider.c`, which `tests/daemon/` builds its daemon on too.

`test-ordering` checks what the history does with storage writes still out. It interposes `g_paste_storage_backend_write_history()` to hold one history's writes in the worker threads at the backend boundary, and `g_paste_storage_backend_read_history()` to check a read comes after the writes it has to see, while the shipped model and saver run as they are: a history deleted with its writes in flight or queued stays deleted, and deleting another history waits for none of them; a history switched away from and straight back to is read once its writes have landed, while another history is read at once and the one left still gets its queued snapshot written, and counts what that snapshot holds; `BackupHistory` and `EmptyHistory`, run through `g_paste_daemon_methods_*()`, see what a history just left still has out, the second writing another history on a worker (`empty-other-off-main-loop`), and wait for none of the current history's writes, and refuse once the store is handed over; a password renamed while a write holding it is out leaves that write the name and timeout it was recorded with, the interposer noting what it was handed. A load left pending behind a write when the history is flushed stays pending once that write lands, rather than take back the storage lock the case has released (`flush-holds-pending-load`). A case that must not wait has the write released five seconds on, which it then finds released instead of hanging. `test-ordering`, `storage-failures` and `test-portal-provider` replace a libgpaste-daemon function (or, for the portal, the whole client) from the executable, which reaches the library's own calls only through the PLT: `tests/meson.build` leaves the first and the last unregistered when `c_link_args` (`LDFLAGS`) carries `-Bsymbolic`, which binds those calls inside the library. `storage-failures` runs all the same, its cases hiding a storage kind skipping themselves when the interposer is not reached (`kind_hidable()`): the rest interpose calls into other libraries, or the migration's own calls from the copy of it the binary includes. `test-saver` drives `GPasteHistorySaver` alone against a backend of its own whose writes wait to be released, to check that writes coalesce per history name. `test-ordering`'s `remove-image-during-write` deletes an image while a write holding it is out: once the writes land, its file has to be gone, as `test-history`'s `file_*image*` cases check of an image file's other ways out -- evicted while flushed (kept, then swept once recording resumes), deleted from an unreadable history (kept), skipped by a read (kept, whether images are off, its date missing or the document cut short) and then by a save (swept) -- and `file_sweep_bounds` of what a sweep must leave: another history's directory, the other file flavour's images, files named otherwise, and everything a database write leaves. `file_encrypting_migration_*` and `sqlite_migration_takes_source_images` move a plain history the way a migration does and check no screenshot stays in clear, bar one an encrypted entry still falls back on; `file_legacy_images_sweep` writes a history the way GPaste 50 did, into its shared images directory, and sweeps it before and after that history is written again. `file_parser_malformed` reads documents no GPaste wrote -- a value under an unknown mime, values of whitespace alone, elements out of place, an image date or checksum out of range, a stray `.xml` -- with no critical on the way.

`test-storage-failures` reads an alternative representation that decodes to nothing, from a file and from a database, which has to be dropped with the item read from its plain value, and fails a history write's install, which has to sweep no image. It also includes `gpaste-storage-migration.c`, so its cases reach the migration's static helpers, and injects the storage failures each one is about by interposing the functions they go through: `g_paste_storage_backend_write_history()` leaves out the first item of what one backend kind is handed, which makes an import fail after writing to its destination, or cuts the footer off a file one writes, which leaves a destination that does not read back; either way the copy has to be gone afterwards -- with the screenshots an import out of the encrypted file flavour decrypted on its way, and short of a destination history it could not read, which it stops before -- the source still there, detection has to find it again, and the prompt has to be told. `g_paste_prompt_list_storage_backends()` leaves one flavour out, standing for a build without the feature it needs, so that a build with every feature runs those cases too: such a source offers neither migration toggle, a reply ticking them anyway is clamped, and an encrypted one asks for no passphrase. Detection keeping a format this build cannot open as what it is shows only in a build lacking that format, which the identity cases are for. `sqlite3_exec()` fails one statement as a full disk would: a version 1 database whose upgrade fails at a column, at the version write or at the commit has to come out of it unchanged and upgrade on the next open; `sqlite3_step()` makes the version read fail, the first one or the one under the write lock, which has to refuse the open rather than stamp the old tables current, and another connection upgrading the database just before the transaction begins has to leave nothing to do. `sqlite3_prepare_v2()`, `sqlite3_step()` and `sqlite3_exec()` fail each step of a remove, a replace and an emptying, and an add's read of the top rank, each of which has to have the history written in full; the binds refuse a value past a length, as SQLite refuses one past `SQLITE_MAX_LENGTH`, which leaves the item out of its history's store and the rest written around it; and `sqlite3_wal_checkpoint_v2()` counts the checkpoints, none of which an add evicting the oldest item asks for. An encrypted SQLite history with one of its parameters deleted, or whose count fails as a wrong passphrase comes, has to be refused with its salt and key check left as they were, and a wrong passphrase refused while another connection holds the database in exclusive locking mode for a moment. `g_file_move()` fails the moves whose source ends with a given suffix, which is how a re-key is stopped at the history's install, or at that and the putting back of an image: a clean rollback leaves the files' bytes as they were and nothing beside them, a failed one says the history is split, keeps the image's original beside it and has the user told so, and a re-key that goes through leaves nothing behind either. What a process dying in an import or between two histories' re-keys leaves is a journal written by hand: the next start takes the copy back unless the move happened, before a migration as before an unlock, and tells the change of passphrase through the prompt, once; one that ends in the process leaves none, the journal being on the disk while each history is written. What a process dying in a re-key leaves is made by hand, without deriving a key: the next start puts back one that had not made its last rename, through `g_paste_storage_decryption_needed()`, and drops the originals of one that had, through the migration; until then a read refuses the history, deleting the history takes them along, what is left beside a history that is gone is dropped rather than put back, a stray file naming no valid history is left alone, nothing is settled while another open file holds the storage lock, an import and a re-key each count as busy from their worker's start to its report, and a re-key settles them before its own -- one whose images directory cannot be listed failing without calling the history split. The storage lock is held by an open file of the case's own, which `flock()` tells apart even within the process: two waits of one process both get it once it is let go of, a load still waiting when the process lets go of it reads nothing, a flushed history whose name changes takes no load and so no lock until it is resumed, and a history dropped while its load waits goes at once, nothing taking the lock after it (`/storage/lock/*`). `flock()` fails as a home on NFS without lockd fails it, which, like a lock file that cannot be opened and a history directory that cannot be made, has to leave the store to be touched rather than blame another daemon (`/storage/lock/file-unusable`, `/storage/lock/dir-unmade`); and it pauses a wait between taking the lock and the rest of it, a `try_lock()` then having to find the lock the process's own (`/storage/lock/taken-is-ours`). `fsync()` records what it synced and fails by suffix, which is how `synced-before-install` and `rekey/success` check that nothing is renamed into place before it is on the disk, and that an image whose write fails leaves nothing at its name. `crypto_pwhash()` derives every key at libsodium's minimum cost, which is what keeps those cases cheap, and counts them: a change of passphrase reads each history it re-keys once (`/storage/rekey/cost`). `free()` searches what it frees for a secret while one is set, GMarkup's own buffers aside, and `realloc()` moves every block meanwhile, searching each it moves away for half the secret: a read of an encrypted history leaves no copy of a password in freed memory, a growing buffer's included. `gcr_secure_memory_alloc()` and `_free()` check that what gcr hands out goes back wiped, a key and a stream's plaintext buffers alike. An import copies a history past the size cap, its screenshots included while images are off (`/storage/migration/import-copies-whole`). A store holding nothing a wrong passphrase is refused over -- a foreign file, a stream cut short in its first frame, an encrypted database emptied -- is no verifiable one, its passphrase asked for twice. Cases that warn run in a subprocess, since structured logging bypasses `g_test_expect_message()`, and every case gets directories of its own (`G_TEST_OPTION_ISOLATE_DIRS`), detection counting what the history directory holds. A `.pngs` under a name no entry's side file has is left as it is by a re-key (`/storage/rekey/foreign-side-file`). A database row's checksum that is no checksum is computed again rather than written back (`/storage/sqlite/checksum-refused`). Only a key opening nothing refutes a passphrase: a history damaged past its first chunk, a foreign file under the flavour's name or one the user may not read leaves the right one accepted (`/storage/passphrase-survives-*`). An image under a history whose name is not UTF-8 is named by its file name alone (`/storage/image-name-not-utf8`). Histories left on two passphrases open by the one the active history takes, which a change re-keys (`/storage/rekey/split-passphrases`); checking a passphrase decrypts the active document twice and no image (`/storage/passphrase/verify-cost`); and an empty active database proves nothing (`/storage/sqlite/verification-empty-active-database`). A migration's destination already holding histories is written under the passphrase they take (`/storage/migration/destination-passphrase`). An unlock with nothing to unlock asks nothing, a store holding nothing to check a passphrase against has it asked for twice, and one this build cannot open is asked no change of passphrase (`/storage/decryption/*`, `/storage/rekey/unopenable`). Ranks past the compaction threshold come down on open whatever the rows' order (`/storage/sqlite/rank-compaction`). A SQLite write that fails leaves the store diverged, written in full once by the next write carrying the history -- an add's reconciliation, which only drops rows, scanning when its count fails and diverging when a drop fails; a full write failing on a database it created leaves none, a failed backup or emptying answers that it failed, and an import's rollback spares a destination it never installed (`/storage/sqlite/diverged-*`, `failed-write-*`, `reconcile-*`, `/storage/migration/import-spares-unwritten-destination`). An encrypted stream carried on past its FINAL frame is refused rather than read as its first stream alone (`/storage/encrypted-stream-bounds`). Emptying reaches the store whatever state the history is in, and a delete of an unreadable history that fails leaves it unreadable (`/storage/history/empty-reaches-the-store`, `failed-delete-keeps-unreadable`). What is added while a load is out goes on top of what the load read (`/storage/history/add-during-load-kept`). Backing another history up, emptying or deleting one is refused until the lock a daemon taken over from holds is this process's (`/storage/lock/store-methods`). A history is created only under a name the storage in use can keep every file of (`/storage/new-history-name-fits`).

`tests/tools/test-extension-locale.py` installs the build into a scratch
directory and checks that the extension's `locale` leads to the catalogs:
GNOME Shell binds the extension's gettext domain there, for the daemon it hosts
too. It runs only with `gnome-shell`.

`tests/tools/test-release.py` runs `release.sh` in a scratch repository with
meson, ninja, appstreamcli and msgattrib stood in for: the version it checks is
meson.build's, the soversions go in the release commit, a file modified for
anything else and a catalog msgattrib fails on each stop it before it commits,
and nothing stray in `po/` is committed.

`tests/tools/test-gen-keybindings.py` runs the control center's keybindings
generator on headers of its own, under a locale with no UTF-8 in it: a
description holding a typographic apostrophe comes out as UTF-8, and an entry
it cannot read -- `K(` with no space, an escaped quote -- fails it rather
than leave the shortcut out of the file.

`tests/tools/test-gi-constants.py` asks gjs whether every `Gio`, `GLib` and
`GObject` constant the extension names is defined: GJS reads a misspelt one as
`undefined` and passes it on as 0, which the suites' doubles of Gio cannot
tell, defining whatever the code spells. It is skipped without gjs.

`tests/daemon/` builds a whole `GPasteDaemon` and takes it down (and checks, on that daemon, that the extension's state resumes only what its going paused, a pause of the user's own outliving every report). `test-daemon-teardown` includes `gpaste-daemon.c`, standing in for the GlobalShortcuts portal's constructor, which the private bus has no service for, and counting the password expiries a re-exec asks for, and exports the daemon on the private bus over the shared providers and a NOOP history: disposing it twice, then unregistering it, finds no export left to drop, and one disposed before the screensaver and the portal answer it -- both built for real there, the answers counted -- sets nothing on what dispose left, while one exported again follows the screensaver as before. Its startup update is looked up through the field the daemon tracks it in: unregistering destroys that source, a daemon let go of while it is pending is finalized at once, and one that has run reaches a listener on the bus and leaves unregistering nothing to cancel. A re-exec has the passwords taken off and both providers stored before the host's `reexecute-self` runs: the shared provider counts its `store` calls, every vfunc of the interface being required. The in-process daemon `g_paste_daemon_new_meta()` builds asks for no expiry, nothing exec'ing there. An upload of a password is refused before any command runs. An upload answering more than one url, on a second line or in an escape sequence, fails; one still writing is cut off once it has written more than a url can be, and one answering before it has read the whole item -- a megabyte, far past what a pipe holds -- is judged by its exit and its answer. An upload out when the daemon leaves the bus, or re-execs, is answered as cancelled at once, a daemon let go of after it being finalized; the in-process daemon carries its uploads through a re-exec. `test-prompt-adw` compiles the standalone daemon's libadwaita prompt in and drives it through the `GPastePrompt` calls on the private Xvfb, the keyring lookup stood in for: a migration picks the backend in use and offers to keep it, or with that backend off the list picks nothing, offers nothing to keep -- nor names it -- and applies nothing until one is picked, and picking another backend keeps what was ticked; losing the window is a dismissal; a passphrase error and a report are shown as written, a "&" and a "<" in them included, no row taking its text for markup; and a new passphrase is taken, Enter included, only once typed twice alike.

`history-switcher` runs the shipped Shell controller under Node.js with actor
and bus doubles. It checks backup drafts and focus across refresh, source
removal, dialog closure after daemon loss, the current history's count (a
listing that omits it, which updates ask, overtaken replies), that only the
key focus sets the switcher row's `active`, and that a call nothing waits on
reports its refusal to the log. It is omitted when Node.js is
unavailable and exercises no real Shell rendering or session.

`indicator` runs the shipped indicator the same way: its teardown, run once and
chained up to `PanelMenu.Button`'s even before `_setup()` has built anything --
and, for an indicator built through its real constructor (`_setup()` stubbed),
which reads the extension's settings rather than building its own,
through the one `destroy` connection the button's double makes -- stopping the
client's wait once its handlers are off it, and what the placeholder row says of each
`GPasteClient:daemon-presence` -- a retry offered only once the daemon is
absent, and handed to `retry_daemon()`. An item row's actions open under the row
that asked and fold when it asks again, when a refresh rebinds or drops that
row but not one after it, handing a focus inside them back to the row; a failed
upload is a notification, a done one is announced in one, without the address, the menu
writes no clipboard of its own -- `UploadAndCopy` leaves that to the daemon --
and nothing reaches the screen from an indicator torn down while the upload was
out.

The indicator double also clears focus when a row is destroyed: keyboard-opened
actions on a removed last row must focus a surviving row, as must the row
itself or one of its buttons, or the history switcher when a rebuild empties
the list, and so must a fetch giving up on a daemon still there, the rows going
without a change of presence. The daemon going parks the focus of a row on the
menu and gives it back to that row once the next daemon's rows are built, and
while it is away an update applies nothing and no size is read.
`history-switcher` checks that hiding the switcher -- which the daemon going
does, a handoff to another included -- gives up both its listing and its size
read, and that nothing is sized while the presence is not `READY`, the name
still having an owner. A revocable weak-reference double
models collection during an upload, so its reply is finished without a
notification from an indicator that is gone.

`daemon` runs the shipped in-shell daemon runner over bus, daemon, storage and
main loop doubles: a takeover, a failed export and a dropped connection all
flush and dispose the daemon, and a takeover, a failed export and a failed
build all shut the prompt down and wipe the passphrase with the lock, which a
shutdown during a storage rewrite defers until the store is idle; a runner acquiring the name meanwhile
keeps both and settles only then, or stops waiting when it is shut down; an
export failing stands it down from inside the first `add_object ()`, nothing
exported after it; a concern queued behind another flushes no daemon a
reconnect has let go of; and a re-exec the daemon asks for runs once, from an
idle.

`extension` runs the shipped extension over panel and session-mode doubles:
the indicator it builds reads the extension's `GPaste.Settings` through its
`settings` getter, which answers it until the extension is disabled.

`prompt` runs the shipped GNOME Shell prompt over St, dialog and session-mode
doubles: the strength meter and its hint wear classes the shell's theme styles;
locking hides a dialog up and unlocking shows it again; a question or a report
asked while locked waits for the unlock; a shutdown dismisses a question
still waiting, logs a report, and stops watching the session; and the
migration dialog names "Keep as is" only where it offers it.

`state-switch` runs the shipped tracking switch over a `PopupSwitchMenuItem`
double that clicks through `toggle()`, as GNOME Shell's does: a `SetActive` the
daemon refuses puts it back where the daemon still is, without asking again on
the way, an accepted one leaves it where the click put it, the daemon moving it
asks nothing back, and a refusal reaching a switch the menu has destroyed is
only logged.
`test-history`'s `/history/set_active_refused` refuses the `track-changes` write
the way a locked key does, by interposing `g_settings_set_boolean()`: the
daemon's side of that call has to fail, the tracking left as it was, unless it
was asked for the state it already has. `/history/set_active_unstored` takes
the write instead and gives it back once `g_settings_sync()` waits for it, from
another thread while the main context is held, as dconf does with a database it
cannot write: the read-back has to see the setting given back before the main
loop announces it. `/history/migration_refused_backend`
refuses `storage-backend` the same way, through `g_settings_set_enum()`: a
migration whose new backend cannot be stored keeps the old history and the
revision, while a "Keep as is" reply still settles under the lock;
`/history/migration_unstored_backend` and
`/history/migration_unstored_backend_is_current` have dconf give it back.
`/history/switch_refused` refuses `history-name` through `g_settings_set_string()`,
and `/history/switch_unstored` has dconf give it back: SwitchHistory fails, and
DeleteHistory of the current history deletes nothing.
`/history/sqlite_save_elsewhere_keeps_connection` interposes `sqlite3_open_v2()`
to count the connections opened on a history saved under another name.

`test-ui-shortcuts` opens the shortcut-help dialog and verifies that both the
master switch and accelerator edits retire its snapshot. It also covers a
refused storage migration, both with the preferences window still up (an alert
dialog) and without one — the latter in a `g_test_trap_subprocess ()`, the
warning it must emit being fatal under `g_test`. It uses memory
GSettings and isolated configuration directories, and runs on a private Xvfb,
skipping when `Xvfb` is not installed.

The portal suite also exercises `GPasteKeybinder` with real settings changes:
starting enabled or disabled, toggling both ways, preserving unchanged sessions,
and disposal with a rebind pending.

`test-client-reexec` covers the command line tool by `#include`-ing
`src/client/gpaste-client.c` with `main` renamed, so a case calls a verb's own
handler directly. Anything that would leave the test — the re-exec call,
libgpaste's re-exec of an older daemon, the version the proxy caches — is a
`#define` over the name the file uses, which is how the empty cases (a daemon that has not answered for
its version, one that refused a re-exec) are reached without a daemon to go
missing, and so is `g_paste_client_upload_and_copy_sync ()`, recorded rather
than sent. It covers the re-exec fallback, the migration gate across it,
an item's control characters shown on a terminal rather than acted on
(`/client/terminal-controls`),
`daemon-version` with and without an answer, `upload-and-copy` making
`UploadAndCopy` and not `Upload`, and which command lines
`dispatch_reads_stdin ()` takes near stdin -- the flag actions, which carry no
verb and so have the verb-less add's shape, and the listing's own flags
included. The client's constructor is `#define`d too, counting the clients
built: none for help, version or a verb nobody knows. So are the daemon's
lookups, answering from a history the case sets, for what `--use-index`
resolves: a malformed index -- a sign, a space or a vertical tab in front,
anything after the digits, past `guint64` -- refused before any lookup or
client, a single index on its own, every argument of a merge off one listing
even when a copy lands after the first answer, a merge index past the end
refused with the error the daemon gives a single one, and text and patterns
left alone on an empty history. A case that succeeds runs its command line
through `gpaste_client_main ()`, `optind` rewound; a refusal goes through
`g_paste_dispatch ()`, where its error comes back to the case rather than out
of `main ()` as a critical, which `g_test` makes fatal. Every argument of
every verb that talks to the daemon, walked off `commands[]`, the text piped in
for one and merge's decoration and separator are refused when not UTF-8, before
any client is built, while UTF-8 beyond ASCII goes through.

`test-client-presence` follows `GPasteClient:daemon-presence` on the private
bus, which has nothing to activate, with the generated skeleton standing in for
a daemon on a connection of its own: owning the name before it exports anything
reads as starting, a `History` as ready, and a daemon going away as starting for
the grace second and absent after it -- as does a retry the bus cannot answer.
A daemon serving only `org.gnome.GPaste2` is told apart from one serving this
interface and from one exporting nothing yet, and only it is signalled to
re-execute, the pid file naming the process owning the name. A
client built through `GInitable`/`GAsyncInitable` rather than its constructors
starts out with the right presence, a handler connected before a two-step init
hearing of it once -- the GetAll reply saying so of a daemon that serves, the
seed alone of an owner that serves nothing yet (`seed-announced`), and one dropped while its request for a
daemon is still out -- to a stand-in registered by hand, which never answers --
is finalized there and then. A serving stand-in taking the name over from
another makes the client starting at once, and ready again only once its proxy
addresses the successor (`handoff`); built after a stand-in already serves
(asynchronously, the stand-in answering on the same context), the client
follows a handoff too, a successor owning the name before it serves keeping it
starting until it does, and one replacing that making it ready once
(`handoff-after-init`); one built through the initables for a name of its own
follows that name alone, GPaste's own changing hands leaving it ready
(`handoff-other-name`). What `g_paste_client_is_daemon_gone_error()` counts as a
daemon gone is checked error by error, an unknown method left out.

`test-ui-panel-history` puts one sidebar history row on the private Xvfb and
drives its state flags directly: the delete button shows for the pointer and for
a visible keyboard focus, but not for the focus a click leaves, and the row's
shortcut is Delete without BackSpace.

`test-ui-daemon-presence` sets the window up on the private Xvfb against the
same kind of stand-in daemon, answering `ListHistories`, `GetHistorySize` and
`GetItemAtIndex`: with none there the banner says it is connecting then offers
Retry, the daemon's actions, the sidebar and the list are insensitive and the
list shows a spinner then says the history is unavailable; a daemon turning up
has the sidebar and the list ask it again and the banner slide away still
worded; one going away takes the offers back but not the search, read from
inside the notify that says so, since the end of the client's grace second may
come first. A
filter on the client's own connection counts the Search and GetFavourites that
leave it, which is the one place to see them while nobody owns the name: none
does while the daemon is away, whether a search reaches the list or Escape
closes the search bar kept open (`search-closed-away`: emptying the entry is a
search of its own, an empty entry closing emitting none, and the next daemon
is asked none), and typing does not open the search. The sidebar's entry holding the focus when the daemon
goes is let go of before the sidebar turns insensitive: GTK would otherwise
warn, which the test aborts on. So is the Merge popover's entry when the picks
drop below two, checked on the entry's own focus controller -- the popover
itself making GDK warn about frame timings on Xvfb, warnings are not fatal
while it is up. A listing answered after a switch keeps the row of the history
current now selected. A
daemon back with fewer histories drops the others' rows, and a list that had
the focus has it again once its rows are back -- but not from a widget the
focus was moved to meanwhile. Two overlapping history listings answer in reverse
order; only the newer one may keep or remove sidebar rows, one still out when
its daemon goes is given up on rather than reported, and one out when the
window closes lets the sidebar go. A window presented
for a failed connection and set up again into a daemon already there takes its
banner down. A daemon serving only `org.gnome.GPaste2` gets the banner offering to
restart it, which signals it, this process standing in for it and the pid file
naming it.
A window closed and let go of while the list's size request is out leaves the
list standing, held by the reply, and the reply lands without reaching for the
sidebar that went with the window. With the widest window buttons (KDE's
icon, minimize, maximize and close), the window at its minimum width opens on
the history page, the list keeping the focus, and fits in each of its states --
selection mode, the search bar, the banner -- and at text scales up to 1.5; it
is collapsed at the collapse width and fits side by side one pixel above, at a
text scale below 1 and at Large Text's -- libadwaita's warning of content wider
than its window being fatal. On Xvfb, with no window manager, a window takes
the size asked of it once its first frame is painted, so these tests resize it
for real, measuring its frame rather than assuming the theme's: narrowed with
the list focused, it keeps the
focus on the history page, and narrowed again after the user went back to the
histories, it shows the history once more. The stand-in also
answers `UploadAndCopy`, which is all a row's upload calls -- what the daemon
keeps of the address is its own business, `/clipboard/copy_uploaded`'s -- and
the window toasts how it went, even for a row taken off the list while its
upload is out, the outcome going through the window the upload started from
rather than through the row.
A direct bus-name handoff goes through a successor that owns the name and sits
on the proxy's `GetAll`, which holds the client in the handoff window: there
the old daemon's listing, answered late, prunes nothing, what the old daemon
emits reaches nobody and the client announcing the same changes itself asks
it nothing, a context menu up on a disabled row acts on nothing, and every
sidebar row bar the default is disabled -- the current history's of the
successor, named by its `History`, bar nothing but its listing. A
serving successor then takes the name over and is listed and sized once; a
history it lists again keeps its sidebar row -- held by reference, so that a
row dropped and re-added cannot pass for it -- enabled once listed. The item
list starts with two rows, drops
them while the successor's size answer is held, then shows its one row when
that answer arrives. An image item whose image cannot be read is read again on
each binding without a warning (`image-preview-unreadable`). A search open when
the daemon goes stays open with its text and is asked of the next daemon, once
(`follow`). A refused successor size answer shows an error page with
Retry; another refusal shows it again, and a successful retry restores the row
and keyboard focus to the visible list. A daemon whose owner the client learns
only once it is ready is asked once. A daemon answering the list's size read
and the sidebar's listing with NoReply, as the bus answers for a recipient gone,
gets neither a warning nor a critical nor the error page, the page waiting for
rows staying as it was, and a pinned-items read answered so is no "No Pinned
Items". A stand-in stops by
closing its connection first, as a real daemon goes: unexported while still
owning the name, it would answer a call with an error no real daemon gives.
One allowing its replacement is not queued for the name once replaced, a real
daemon quitting then. The focus restoration case
checks that its wait predicate rejects focus still on the list.
The version offer is checked across a current-to-old-to-current handoff, the
last read answered before teardown so it
cannot outlive the case, and is made only once the proxy knows the daemon's
owner -- an in-shell daemon's restart and toast stay withheld while it does
not. A direct comparison case orders every pair both
ways, and checks equality, across older three-component versions, a bare major after
its prereleases and equal to its `.0`, two-digit numeric minors, all named
prereleases from `meson.build`, and the stable `51.0` and later releases; it
also reads `meson.build`'s prerelease table itself, in the order it ranks the
names (`G_PASTE_TEST_PRE_RELEASES`), so a name added there is checked too.
A daemon of another version than the window's gets a toast offering to restart
it, whether it was there first or the window watched it start -- the stand-in
setting its version once exported, as the real daemon does -- which times out as
any toast does and is dismissed early with that daemon; a `51.beta` daemon gets
one in a `51.0` window, while one of the window's own gets none, nor does one
newer than the window, nor the experimental in-shell
daemon -- a stand-in owning the Shell's name on its own connection -- whose
restart the menu withholds too, while a Shell owning its name apart leaves the
standalone daemon its offer. Both paths assert that the watch's first answer
has not arrived just after setup: no toast is offered yet, and the in-shell
daemon's Restart action is disabled then. Some twenty seconds of it all, the
suite runs under a 120-second Meson timeout.

`test-ui-preferences` compiles the preferences' group and shortcut row in
and drives them on the private Xvfb: a range row emptied, or holding no
number, keeps its setting even where the range starts at 0; a text row writes
its setting once applied, not as typed; a row destroyed with its reset menu up
takes the menu with it; Shift alone makes no global shortcut, bar on a
function key; an armed shortcut row the focus leaves is disarmed; and a
disposed one still holds its accelerator.

`test-ui-text-dialog` covers what the item composer will open: a line past
`MAX_COMPOSABLE_LINE` is refused and puts no dialog up, the same number of
characters spread over many lines opens, and a line that long cannot be pasted
into a view that did open -- whether it is the first line of the paste or one
below it, while a paste of many short lines still goes in. Lines broken by
`\r`, `\r\n` or U+2029 count as lines, the way the view breaks them, and
deleting the break between two lines is refused when the line it would make is
past the limit. It uses real GTK on the private Xvfb and skips without one.

`test-settings` uses the memory GSettings backend and isolated configuration
directories to check ordinary and detailed `rebind` signal key arguments and
detail filtering, and that a write GSettings refuses leaves a non-default value
in place and announces nothing -- a property write announcing the setting again
once it is over -- without reading or writing user preferences.

`test-shell-settings` supplies a minimal test-local Shell schema and real memory
and non-writable GSettings backends. It compiles the shipped settings source,
substituting only the optional Shell detection and settings construction, and
checks effective extension membership after accepted and refused writes, that
other extensions' changes do not notify `extension-enabled`, and that a row
bound to it comes back after a refused flip. This settings-only suite uses
`G_PASTE_TEST_ENV_OWN_BUS` to leave both bus addresses unreachable.

`test-util` compiles `gpaste-util.c` with the session bus proxy's constructors
and calls faked, and checks how both `g_paste_util_activate_ui()` flavours take
their argument, whether the proxy comes up or is refused: a floating one is
consumed, a caller's own reference left to the caller -- who may drop it before
the asynchronous flavour's proxy is up, the call still made with it. It also
checks what a client draws for an item: one line, the Unicode line breaks
folded too; each kind's decoration, a kind it does not know shown as it
arrived; and a files item's paths percent-decoded, a home file from "~" --
a whole leading component only, a `$HOME` ending in a slash included, set in a
subprocess since GLib reads it once -- and a name not in UTF-8 shown as text.

`test-util` also reads back pid files: the positive decimal pid written in one,
and -1 for anything else, a 0 above all, which `kill ()` takes for the caller's
own process group (`/util/pid-file`).

`test-portal-provider` runs the GlobalShortcuts client against a fake portal on
private buses, without GTK initialization or a desktop session. It controls
method replies and `Response` signals independently, including both reply
orders, superseded operations, pending and repeated disposal, actual request
paths differing from predictions, invalid session handles, untrusted senders,
partial/empty grants, retry exhaustion and cancellation -- a portal appearing
after the budget was spent against an unowned name getting it whole --, and
retired-request deadlines. The keybinder hands the portal its bindings in the
order they were added, a re-added key taking its place; a keypad key's
trigger names the keypad key. Name handoffs leave the old server alive and assert that its known
and late-created sessions are closed on its unique connection; separate tests
actually disconnect that server. `Closed` immediately following a create
response is queued on the I/O thread before main-context dispatch to test the
subscription race. Close failures are retried after the client is dropped,
with a bounded budget. A removed-session test unregisters the real GDBus object
and verifies that cleanup does not retry its UnknownMethod reply. Unexpected
warnings are fatal; retry exhaustion explicitly expects one warning, while
intermediate failures only log at debug level. Each fixture has a ten-second
watchdog naming the stalled case, with a 120-second Meson suite timeout.
A D-Bus service file activates this test executable in
`--activated-portal` mode to cover startup without an owner, including disposing
the client before activation completes. The test target compiles the same
client source with a 50 ms retry interval and a 1 s retirement deadline;
production uses one second and ten minutes respectively.

A test that needs a fake service on a private bus links `gpaste_test_bus_dep`
(`tests/gpaste-test-bus.c`) rather than rolling its own: `g_paste_test_bus_new_server()`
puts a second connection on the bus with an object registered on it — a connection
of its own being what lets a test hand the well-known name over the way a restart
does — `g_paste_test_bus_name_call()` drives `RequestName`/`ReleaseName`, and
`g_paste_test_bus_barrier()` synchronizes both ends. `g_paste_test_bus_connect()` is
that second connection alone, to any address -- the bus `gpaste-test-env` set up
included, for a stand-in daemon under a client that follows it -- and
`g_paste_test_bus_wait_until()`, `_wait_for_enum()`, `_wait_for_count()` and
`_wait_for_owner()` iterate the main context until a condition, a property, a
counter a fake service keeps or a proxy's name owner gets there, failing after
`G_PASTE_TEST_BUS_WAIT_SECONDS`; `g_paste_test_bus_count_emission()`, connected
swapped, is the counter for a signal.
`g_paste_test_bus_barrier()` is three rounds addressed to the server's *unique*
name: a reply behind the calls on the client's connection makes a no-op
assertion deterministic, the later rounds drain what a reply's own callback
issues, and the unique name is what answers while the well-known one is
unowned or has just changed hands. `g_paste_test_bus_round_trip()` is a lighter
one, for a server with nothing but a generated skeleton on it: a single
`Peer.Ping` behind its replies, then a drain, without those later rounds.
`g_paste_test_bus_pump()` runs the main context for a fixed time, for what
nothing announces -- a warning *not* coming, or a timer the code under test
runs on its own; anything with a state to wait for waits on it instead.

`test-ui-password` controls the edit dialog's two asynchronous replies while using
real GTK on the private Xvfb. It checks both late-reply orders, closure between
replies, a closed parent retained by another operation, and an intentionally
hidden parent. The edit must not hold the window alive and thereby prevent the
`destroy` signal cancelling its reads. `test-ui-edit-item` holds Edit Item's one
read to the same: a closed parent and a retained closed one get no dialog, an
open one does, and a window closed under the dialog goes and frees what the edit
held, even while something else holds the dialog; a text dialog refuses a
parent libadwaita would not host it in, answering once as a cancellation, and
Edit Item refuses such a window before its read goes out.
`test-ui-new-item` checks that a window closed under the New Item composer
goes and frees what the composer held, and that a window the dialog would
refuse is refused on the way in.

Clipboard regressions also cover synchronous password eviction invalidating a
source cache during synchronization, distinct named passwords sharing a value,
strips requested after an inconclusive read, provider refusals, and release of
refused strips on disposal. An empty marked-sensitive selection stays excluded
from bare-text synchronization until an ordinary read identifies new content.
A plain-text sync whose reply lands after MakePassword turned the source's text
into a password puts the password, countdown included, on the other selection,
and its expiry takes it off both.

The clipboard suite also checks unlisted password capture/merge twins against
unrelated named entries, strips and automatic synchronization after an
unanswered sensitive hint, and queued strips across failed identifying reads.
Neither a repeated strip request nor a refused one may keep a deleted history
snapshot alive, and neither may flood an owner with identifying reads. After the
cooldown, another strip request must retry even when the owner has not changed.
A refused strip keeps its retry slot across a completely unanswered read.
Renaming a captured password merged into the history head must preserve deletion
cleanup even during failed reads, the rename putting a new object in its place,
and a timeout edited back through the history while a read is out is still an
edit (`/clipboard/timeout_edit/reenable_through_history`).
Bootstrap captures are linked too and use the stored timeout, including one
whose read concludes before the history has loaded or while it is loading -- an
add made during the load does not spend that link, linking leaves an unconfirmed
record so, and a link landing past the stored deadline asks for the identifying
read the countdown would. A password leaving its selection with nothing the
synchronization carries over in its place -- another password with the
synchronization off included, and a removal only a later identifying read finds
-- takes the synchronized copy off the other selection; a text carried over, a
password the user selected even over a synchronized copy, or what an owner put
there itself, that very value as plain text included, does not, and an edited
timeout, the copy's own or a same-value twin's, leaves the copy the
synchronization's. A hint served with no text leaves the record unconfirmed
rather than retiring it, but an empty text is an answer, hinted or not, and
retires it. The GDK suite checks that the targets answering the bootstrap read
are not taken for a copy.
An owner slow past the formats deadline still has its copy recorded when its
targets arrive under an identifying read (`/gdk/formats/owed-copy`), and the
targets of an owner slow past the bootstrap read's own deadline answer that read
and are not a copy (`/gdk/formats/bootstrap-late`). The X11 hints suite checks
that a requestor window retired from a conversion given up on is destroyed when
the hints object goes with its last reference.
Merged captures use the captured duration on both selections; their expiry
promotes the plain successor in the history as well as replacing both
selections.
A mocked read must deliver each counted reply even after a publication supersedes
it; the superseded conclusion releases content, while those replies free the update.

`test-enums` has 32 threads ask for each enum type GPaste registers at once, in
a fresh subprocess per type so that theirs is the first use, with
`g_enum_register_static()` interposed to widen the window a racing registration
needs: every caller must get the same type back.
