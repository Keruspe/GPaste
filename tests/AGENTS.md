# `tests/`

Every C test binary calls `g_paste_test_env_setup()` (`tests/gpaste-test-env.c`, linked through `gpaste_test_env_dep`) first in `main()`, before `gtk_init_check()`, `g_test_init()` or any `GSettings`, and ends with `return g_paste_test_env_run ();`. It detaches the process from the desktop session whether meson or a shell launched it: memory `GSettings` on the schemas compiled into the build tree (hence `depends: [gpaste_schemas, test_bus_supervisor]` on each C test), a private `XDG_RUNTIME_DIR`, no `DISPLAY`/`WAYLAND_DISPLAY`, and a private `dbus-daemon` serving as both session and system bus. `G_PASTE_TEST_ENV_OWN_BUS` is for a test that brings up its own `GTestDBus` per fixture: both bus addresses then point at a socket that does not exist. `G_PASTE_TEST_ENV_DISPLAY` starts a private `Xvfb` with `GDK_BACKEND=x11`; `g_paste_test_env_has_display()` is FALSE when `Xvfb` is not installed, and such a test skips rather than use the session's display. The server is never killed, since GDK exits the process when its display goes away: `-terminate` ends it when the process disconnects, `PR_SET_PDEATHSIG` when it never connected. Settings and GTK environment therefore do not belong in a test's meson `env`. The default bus has no activation directories and runs under `gpaste-test-bus-supervisor`, an infrastructure helper rather than a test binary. The test holds a pipe to the supervisor: EOF on normal exit or a crash kills and reaps the bus. This does not rely on `PR_SET_PDEATHSIG`, which a security-domain transition at dbus-daemon exec can clear. `GTestDBus` finalization waits for GIO's session singleton even after `stop()`, so it is reserved for suites that own their fixture connections. `test-env` holds a real `g_bus_get()` connection across teardown and traps an aborting child to verify that the bus releases inherited output pipes (its `TMPDIR` points into the parent's runtime directory, since a crashed child never removes its own); it must finish within ten seconds.

Tests live under `tests/`. `tests/history/` unit-tests the `GPasteHistory` model
(add/dedup/size-enforcement/remove/select) against an in-memory `GSettings` and a
throwaway `XDG_DATA_HOME`. The `eslint`
test lints the GNOME Shell extension JS, and `completions` checks the shell
completions and the man page against `gpaste-client`'s own list of verbs. The
last two are scripts that touch no settings, bus or display. `tests/clipboard/` exercises the shared update lifecycle and the clipboards manager with a mock provider, including overlapping reads, superseded callbacks, typed cache commits, cross-selection copy ordering, rich-text refreshes and password expiry across a persisted handover; it needs no display or desktop session bus; the common test environment still requires `dbus-daemon` for isolation. Its password countdowns are real GLib timeouts, so the suite runs under a 120-second Meson timeout. The test advances the ready time of the production manager’s forced-expiry source, exercising the shipped object under owner churn without waiting sixty seconds. The timeout-edit table names each scenario alongside its timings and expected contents. Its configuration and data directories are isolated, including the file-backend handover test. Every case starts from the schema's defaults (`make_settings()`): the memory backend is shared by the whole binary, so a key one case sets would otherwise reach every case after it.

`history-switcher` runs the shipped Shell controller under Node.js with actor
and bus doubles. It checks backup drafts and focus across refresh, source
removal, dialog closure after daemon loss, the current history's count (a
listing that omits it, which updates ask, overtaken replies), and that only the
key focus sets the switcher row's `active`. It is omitted when Node.js is
unavailable and exercises no real Shell rendering or session.

`test-ui-shortcuts` opens the shortcut-help dialog and verifies that both the
master switch and accelerator edits retire its snapshot. It uses memory
GSettings and isolated configuration directories, and runs on a private Xvfb,
skipping when `Xvfb` is not installed.

The portal suite also exercises `GPasteKeybinder` with real settings changes:
starting enabled or disabled, toggling both ways, preserving unchanged sessions,
and disposal with a rebind pending.

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
`g_paste_test_bus_barrier()` synchronizes both ends. The barrier is three rounds
addressed to the server's *unique* name: a reply behind the calls on the client's
connection makes a no-op assertion deterministic, the later rounds drain what a
reply's own callback issues, and the unique name is what answers while the
well-known one is unowned or has just changed hands.
