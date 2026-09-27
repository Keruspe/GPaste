# `tests/`

Every C test binary calls `g_paste_test_env_setup()` (`tests/gpaste-test-env.c`, linked through `gpaste_test_env_dep`) first in `main()`, before `gtk_init_check()`, `g_test_init()` or any `GSettings`, and ends with `return g_paste_test_env_run ();`. It detaches the process from the desktop session whether meson or a shell launched it: memory `GSettings` on the schemas compiled into the build tree (hence `depends: [gpaste_schemas, test_bus_supervisor]` on each C test), a private `XDG_RUNTIME_DIR`, no `DISPLAY`/`WAYLAND_DISPLAY`, and a private `dbus-daemon` serving as both session and system bus. `G_PASTE_TEST_ENV_OWN_BUS` is for a test that brings up its own `GTestDBus` per fixture: both bus addresses then point at a socket that does not exist. `G_PASTE_TEST_ENV_DISPLAY` starts a private `Xvfb` with `GDK_BACKEND=x11`; `g_paste_test_env_has_display()` is FALSE when `Xvfb` is not installed, and such a test skips rather than use the session's display. The server is never killed, since GDK exits the process when its display goes away: `-terminate` ends it when the process disconnects, `PR_SET_PDEATHSIG` when it never connected. Settings and GTK environment therefore do not belong in a test's meson `env`. The default bus has no activation directories and runs under `gpaste-test-bus-supervisor`, an infrastructure helper rather than a test binary. The test holds a pipe to the supervisor: EOF on normal exit or a crash kills and reaps the bus. This does not rely on `PR_SET_PDEATHSIG`, which a security-domain transition at dbus-daemon exec can clear. `GTestDBus` finalization waits for GIO's session singleton even after `stop()`, so it is reserved for suites that own their fixture connections. `test-env` holds a real `g_bus_get()` connection across teardown and traps an aborting child to verify that the bus releases inherited output pipes (its `TMPDIR` points into the parent's runtime directory, since a crashed child never removes its own); it must finish within ten seconds.

Tests live under `tests/`. `tests/history/` unit-tests the `GPasteHistory` model
(add/dedup/size-enforcement/remove/select, and which of those announce a password
leaving it) against an in-memory `GSettings` and a
throwaway `XDG_DATA_HOME`. The `eslint`
test lints the GNOME Shell extension JS, and `completions` checks the shell
completions and the man page against `gpaste-client`'s own list of verbs. The
last two are scripts that touch no settings, bus or display. `tests/clipboard/` exercises the shared update lifecycle and the clipboards manager with a mock provider, including text put on every selection without becoming an item (`select_text`), overlapping reads, superseded callbacks, typed cache commits, cross-selection copy ordering, rich-text refreshes, the selections a password's
history entry leaving is taken off, and password expiry across a persisted handover; it needs no display or desktop session bus; the common test environment still requires `dbus-daemon` for isolation. Its password countdowns are real GLib timeouts, so the suite runs under a 120-second Meson timeout. The test advances the ready time of the production manager’s forced-expiry source, exercising the shipped object under owner churn without waiting sixty seconds. The timeout-edit table names each scenario alongside its timings and expected contents. Its configuration and data directories are isolated, including the file-backend handover test. Every case starts from the schema's defaults (`make_settings()`): the memory backend is shared by the whole binary, so a key one case sets would otherwise reach every case after it. `test-clipboard-gdk` drives the GDK backend's own ownership and read callbacks behind GTK mocks, with no display or live clipboard -- the formats deadline telling a released selection from an owner merely slow to serve `TARGETS` -- and from a display that could not be asked which -- included, the shorter grace a selection X already says nobody owns waits with too, and the fallback that reads the hints off the formats where the workaround cannot convert the selection at all -- `test-clipboard-hints` covers the update API the X11 hints workaround reads through -- the sensitive offer read and the special and sensitive halves of the MIME-reading policy -- and `test-clipboard-x11-hints` covers the workaround itself, compiling `src/daemon/gpaste-clipboard-x11-hints.c` in (it belongs to the daemon executable, not to a library) and running it against the private Xvfb and a selection owner of the suite's own on a second connection, driven from the same pump as the main loop: what the selection answers about the hint and what each non-answer -- a refusal, an INCR reply, a silence -- is read as, the ownership query, the requestor window a conversion given up on leaves standing for a late reply, and the timestamps ICCCM has an owner judge a conversion by, the bootstrap case included, which is the one the server's clock is read for. Both go when that workaround does. The least an update needs of a provider is shared by the two as `gpaste-test-clipboard-provider.c`.

`history-switcher` runs the shipped Shell controller under Node.js with actor
and bus doubles. It checks backup drafts and focus across refresh, source
removal, dialog closure after daemon loss, the current history's count (a
listing that omits it, which updates ask, overtaken replies), and that only the
key focus sets the switcher row's `active`. It is omitted when Node.js is
unavailable and exercises no real Shell rendering or session.

`indicator` runs the shipped indicator the same way: its teardown, run once and
chained up to `PanelMenu.Button`'s even before `_setup()` has built anything --
and, for an indicator built through its real constructor (`_setup()` stubbed),
through the one `destroy` connection the button's double makes -- stopping the
client's wait once its handlers are off it, and what the placeholder row says of each
`GPasteClient:daemon-presence` -- a retry offered only once the daemon is
absent, and handed to `retry_daemon()`. `history-switcher` checks that hiding
the switcher -- which the daemon going does, a handoff to another included --
gives up both its listing and its size read, and that nothing is sized while
the presence is not `READY`, the name still having an owner.

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
handler directly. Anything that would leave the test — `kill ()`, the re-exec
call, the pid file, the version the proxy caches — is a `#define` over the name
the file uses, which is how the empty cases (a daemon that has not answered for
its version, one that refused a re-exec) are reached without a daemon to go
missing, and so is `g_paste_client_upload_and_copy_sync ()`, recorded rather
than sent. It covers the re-exec fallback, the migration gate across it,
`daemon-version` with and without an answer, `upload-and-copy` making
`UploadAndCopy` and not `Upload`, and which command lines
`dispatch_reads_stdin ()` takes near stdin -- the flag actions, which carry no
verb and so have the verb-less add's shape, and the listing's own flags
included.

`test-client-presence` follows `GPasteClient:daemon-presence` on the private
bus, which has nothing to activate, with the generated skeleton standing in for
a daemon on a connection of its own: owning the name before it exports anything
reads as starting, a `History` as ready, and a daemon going away as starting for
the grace second and absent after it -- as does a retry the bus cannot answer. A
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
banner down.
A window closed and let go of while the list's size request is out leaves the
list standing, held by the reply, and the reply lands without reaching for the
sidebar that went with the window. The stand-in also
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
that answer arrives. A search open when
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
detail filtering, without reading or writing user preferences.

`test-portal-provider` runs the GlobalShortcuts client against a fake portal on
private buses, without GTK initialization or a desktop session. It controls
method replies and `Response` signals independently, including both reply
orders, superseded operations, pending and repeated disposal, actual request
paths differing from predictions, invalid session handles, untrusted senders,
partial/empty grants, retry exhaustion and cancellation, and retired-request
deadlines. Name handoffs leave the old server alive and assert that its known
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

The clipboard suite also checks unlisted password capture/merge twins against
unrelated named entries, strips and automatic synchronization after an
unanswered sensitive hint, and queued strips across failed identifying reads.
Neither a repeated strip request nor a refused one may keep a deleted history
snapshot alive, and neither may flood an owner with identifying reads. After the
cooldown, another strip request must retry even when the owner has not changed.
A refused strip keeps its retry slot across a completely unanswered read.
Renaming a captured password merged into the history head must preserve deletion
cleanup even during failed reads.
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
