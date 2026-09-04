# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

GPaste uses Meson + Ninja:

```sh
mkdir build && cd build
meson ..
ninja
```

Common build options (`meson .. -Doption=value`):

| Option | Default | Description |
|---|---|---|
| `gnome-shell` | true | Build the GNOME Shell extension |
| `introspection` | true | Generate GIR data |
| `vapi` | true | Generate Vala bindings (requires introspection) |
| `x-keybinder` | true | X11 keybinder support |
| `systemd` | true | systemd user unit |

For a lighter build that skips the GNOME Shell extension, GIR introspection data
and Vala bindings (the daemon, UI and preferences apps are always built):

```sh
meson .. -Dgnome-shell=false -Dintrospection=false -Dvapi=false
```

Run tests from the build directory:

```sh
ninja test
```

`test-keybinding-provider` runs the GNOME Shell provider against a fake Shell on
a private `GTestDBus` bus. It covers complete and partial grabs, duplicate requests
while a reply is pending, disabling shortcuts before a grab completes, and a shell
handing its bus name straight to a replacement — with a grab already held, and with
one still in flight. It requires no desktop session and runs by default; the older
interactive Shell test still skips unless explicitly enabled.

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

Check header include ordering:

```sh
tools/check-includes.sh
```

## Code style

- C standard: GNU17
- Formatting: ClangFormat (see `.clang-format`). Key rules: Allman braces, 4-space indent, no column limit, space before parens, no tabs.
- clang-format is not yet enforced; do not run it automatically.
- **Braces**: Remove braces from `if`/`else if`/`else` branches whose body is a single statement on a single line. Keep braces when the body has multiple statements OR spans multiple lines (e.g. a nested if-else chain). Multi-statement macros that need to appear as a single statement must use the `do { ... } while (0)` idiom — `SWITCH_STATE` in `gpaste-file-backend.c` does this and can safely appear without surrounding braces.

### JavaScript (GNOME Shell extension)

The `src/gnome-shell/` extension follows upstream GNOME Shell's JS conventions, enforced by the **same tooling, layout, and configuration** as upstream:

- The npm project lives in `tools/` (`tools/package.json`, `tools/package-lock.json`, `tools/eslint.config.js`), mirroring gnome-shell. The repo-root `eslint.config.js` is a **symlink** to `tools/eslint.config.js`.
- **ESLint** with [`eslint-config-gnome`](https://gitlab.gnome.org/World/javascript/eslint-config-gnome) (`recommended` + `jsdoc` configs) and the [`ci-run-eslint`](https://gitlab.gnome.org/World/javascript/ci-run-eslint) runner, both pinned to the same commits upstream uses. The config mirrors upstream's custom rule overrides (`camelcase`, `consistent-return`, `eqeqeq: smart`, `key-spacing`, `prefer-arrow-callback`, `prefer-const`, jsdoc tweaks). Shell-extension globals (`global`, `_`, `C_`, `N_`, `ngettext`) are declared for `src/gnome-shell/**`.
- Style basics live in `src/gnome-shell/.editorconfig` (LF, UTF-8, trim trailing whitespace, 4-space indent for `*.js`).
- Run it with `tools/run-eslint.sh` — exactly the upstream wrapper. It `npm clean-install`s into `tools/` on first run, symlinks `node_modules` into the repo root for import resolution, then lints `src/gnome-shell`. Pass `--fix` to auto-fix formatting.
- The same script is the single entry point everywhere: the meson `eslint` test (`meson test -C build eslint`, skipped when `npm` is absent) and the GitHub Actions workflow (`.github/workflows/eslint.yml`, runs on pushes/PRs touching the JS or tooling) both invoke it. Upstream runs lint from GitLab CI; GPaste runs it from GitHub Actions, but the toolchain, config, layout, and `run-eslint.sh` are otherwise identical.
- This tooling applies **only** to the JavaScript code; it does not affect the C/meson sources.

Code conventions (also following upstream):
- **Don't version-pin core `gi://` imports** — write `gi://GObject`, `gi://GLib`, `gi://Gio`, `gi://Pango`, `gi://Clutter`, `gi://St`. Only pin typelibs that genuinely ship multiple versions: `gi://GPaste?version=2`, `gi://GPasteGtk?version=4`.
- **Manage signal lifecycles with `connectObject`/`disconnectObject`** (owner = `this`) for connections to long-lived non-actor GObjects (settings, the `GPaste.Client`), rather than tracking handler ids and disconnecting them by hand. They auto-disconnect when the owner actor is destroyed.
- **Use a standard `constructor()` (calling `super(...)`) in `GObject.registerClass` classes**, not `_init()`/`super._init()`. GJS bridges to the `_init()`-based shell/St/Clutter base classes transparently (positional args like `super(0.0, 'GPaste')` and property dicts like `super({...})` both work).

Async / `Gio._promisify` conventions (these bit us — keep them):
- **`Gio._promisify` works on instance methods (`SomeClass.prototype`) but NOT on a static constructor (`SomeClass.new`) inside gnome-shell.** The wrapper assignment silently fails to stick on the class object (it works in standalone `gjs`, so it is easy to miss), and the *raw* introspected `new` then throws *"At least 1 argument required, but only 0 passed"* when awaited with no args. Promise-wrap the raw `new` + `new_finish` pair by hand instead — see `_connect` in `indicator.js`. `dependencies.js` promisifies only the instance methods.
- **Don't pass a trailing `null` to a promisified async method that has no `cancellable`.** GPaste.Client's async methods are `(…args, callback, user_data)` with no cancellable; a trailing `null` lands in the callback slot and defeats `Gio._promisify` (the call hangs or returns `undefined`). Call them with their real args only: `await client.get_history_size(name)`, not `(name, null)`.
- **`GLib.idle_add_once` / `GLib.timeout_add_once` take `(priority, func)`**, not `(func)`. Passing only the callback makes GJS treat the callback as the priority and dispatch with no function → *"callback is not a function"*. Always pass an explicit priority (e.g. `GLib.PRIORITY_DEFAULT_IDLE`) and keep the `GLib.Source.set_name_by_id(...)` wrapper.

## Memory management

Always use GLib automatic memory management. Apply to every C file touched, not only the file under edit.

| Macro | Use when |
|---|---|
| `g_autofree` | Plain heap allocation: `g_strdup`, `g_malloc`, `g_new` for non-GObject structs |
| `g_autoptr(Type)` | GObject-derived or boxed types with a registered cleanup (`GError`, `GFile`, `GString`, `GMenu`, `GSimpleAction`, `GSimpleActionGroup`, `GSList`, `GList`, …) |
| `g_auto(Type)` | Stack-allocated types with a cleanup function: `GStrv`, `GVariantBuilder`, … |
| `g_autolist(Type)` | `GList *` of **owned** GObject elements — cleanup calls `g_list_free_full(list, g_object_unref)` |
| `g_autoslist(Type)` | `GSList *` of **owned** GObject elements — cleanup calls `g_slist_free_full(list, g_object_unref)` |
| `g_autoptr(GList)` | `GList *` that does **not** own its elements — cleanup calls `g_list_free` only |
| `g_autoptr(GSList)` | `GSList *` that does **not** own its elements — cleanup calls `g_slist_free` only |

Key rules:
- Replace `g_free(old); ptr = g_strdup(new)` with `g_set_str(&ptr, new)` (GLib ≥ 2.76, project requires ≥ 2.84) — but **only when `new` is borrowed**. `g_set_str` does an internal `g_strdup`, so if `new` is a freshly-allocated string you already own (e.g. from `g_strdup_printf`, `g_strconcat`, `g_settings_get_string`), `g_set_str` adds a wasted allocation. In that case use `g_free(ptr); ptr = g_steal_pointer(&new)` instead.
- A setter whose callers always pass a freshly-built string should take ownership of it (`gchar *` arg, annotated `(transfer full)`) and store it directly (`g_free(field); field = arg`) rather than dup it — this is how `g_paste_item_set_display_string` works, and callers hand off with `g_steal_pointer(&local)`.
- Use `g_steal_pointer(&ptr)` when transferring ownership out of an auto-managed variable — including when passing a `g_autofree`/`g_autoptr` local into a container that takes ownership (e.g. `g_ptr_array_add (arr, g_steal_pointer (&local))`).
- In `dispose`: use `g_clear_object`, `g_clear_pointer`, `g_clear_list`, `g_clear_slist` — they null the pointer, making double-dispose safe. Release all reference-counted objects here (GObject, GBytes, GPtrArray, GHashTable, …), not in `finalize`.
- In `finalize`: use `g_free` for plain heap allocations (`gchar *`, plain structs). Do **not** call `g_object_unref` or other ref-counting unrefs here — those belong in `dispose`.
- When a type needs to release GObject refs but currently only has `finalize`, add a `dispose` function and register it with `object_class->dispose`.
- Do **not** use `g_autolist`/`g_autoslist` on lists that do not own their elements. `gdk_file_list_get_files` is **`(transfer container)`** — it returns a fresh `g_slist_copy()` whose `GFile`s stay owned by the `GdkFileList`, so its result must be freed with `g_autoptr(GSList)` (never leaked, never `g_autoslist`'d). `g_hash_table_get_values` is the same shape — use `g_autoptr(GList)`.
- Do **not** use auto-cleanup on a variable whose ownership is intentionally transferred — use `g_steal_pointer` to make the handoff explicit.
- Do **not** use `if (ptr) g_slist_free_full (g_steal_pointer (&ptr), fn)` in dispose — use `g_clear_slist (&ptr, fn)` instead (handles NULL safely).
- When storing a `g_settings_get_string` value (owned, `(transfer full)`) into a `gchar *` field, free the old value and take ownership directly: `g_free(field); field = g_settings_get_string(...)`. Do **not** assign without freeing first (leak on re-assignment), and do **not** route it through `g_set_str` (that would re-`g_strdup` an already-owned string — a wasted allocation).
- For a stored `GSource`/timeout/idle/signal handler id, prefer `g_clear_handle_id (&id, remove_func)` over the hand-rolled `if (id) { remove_func (id); id = 0; }` — it is NULL-safe and nulls the field in one call (e.g. `g_clear_handle_id (&priv->retry_source, g_source_remove)`). Use this in new code and when touching such cleanup.
- A `GSource` whose callback needs a GObject the source does not otherwise hold takes a `g_paste_weak_ref_new (object)` as its data, with `g_paste_weak_ref_free` as its destroy notify (`g_timeout_add_full`, not `g_timeout_add`), and the callback opens it with `g_autoptr (Type) self = g_weak_ref_get (user_data)`. A reference of its own would put the one thing that cancels the source — that object's `dispose()` — out of reach, since `dispose()` only runs once the last reference is dropped; a raw pointer borrows its safety from that same teardown path instead of stating it.
- The same holds for **anything an object tracks so that its own `dispose()` can end it** — an async request in a list, a pending operation in a table — not only for a `GSource`: it holds the object with an embedded `GWeakRef` (`g_weak_ref_init` in the constructor, `g_weak_ref_clear` in the free function, `g_weak_ref_get` at the top of every callback), and anything it carries that would take a reference of its own goes the same way — a `GTask` created for it passes **no source object**, since `g_task_new()` references the one it is given. A strong reference anywhere in that loop is one the object holds on itself, and the teardown it is waiting for can then never run.
- Build NULL-terminated string vectors with `GStrvBuilder` (`g_strv_builder_add` for borrowed strings, `g_strv_builder_take` for owned ones, `g_strv_builder_end` to get the `GStrv`) rather than hand-managing a `GPtrArray` + manual `NULL` terminator + `(GStrv) ->pdata` cast. It also avoids the deep `g_strdupv` copy that returning a `GPtrArray`'s contents requires.
- Prefer `g_signal_connect_object (source, sig, cb, gobject, 0)` over plain `g_signal_connect` when the handler's data is a **GObject** shorter-lived than the signal source and you would otherwise not disconnect — it auto-disconnects when that object is finalized. Do **not** convert sites that pass non-GObject data (a `priv`/struct pointer), pass `NULL` data, or that deliberately disconnect early in `dispose` for ordering reasons (e.g. `GPasteInternalKeybindingProvider` disconnects its `GdkDisplay::xevent` handler before freeing the state that handler uses — `g_signal_connect_object` would only disconnect at finalize and regress that).

## Maintenance rules

- When adding or removing a public function from any library under `src/libgpaste/`, update the corresponding `.sym` file (`libgpaste.sym` or `libgpaste-gtk4.sym`).
- When updating source files in this repository, keep `CLAUDE.md` up to date to reflect any new patterns, rules, or architectural decisions introduced.

## Architecture

GPaste is a GNOME clipboard manager split across several binaries and a shared library.

### `src/libgpaste/` — shared library

The core library used by all other components. Two sub-modules:

- `gpaste/` — daemon-agnostic types: `GpasteClient` (D-Bus client), `GpasteSettings` (GSettings wrapper), `GPasteGnomeShellClient` (GNOME Shell keybinding D-Bus proxy), enums, utilities.
- `gpaste-gtk4/` — GTK4 + Adwaita UI helpers (used by the preferences app), plus `GPasteGtkGlobalShortcutClient` (XDG GlobalShortcuts portal proxy), which lives here because translating a GTK accelerator into a portal trigger needs GTK.

The library exposes a versioned ABI (symbol version scripts in `src/libgpaste/`). GIR and Vala bindings are generated from it.

**`GPasteKeybindingProvider`** is a GObject interface (`G_DECLARE_INTERFACE`) that abstracts keybinding grabbing. It declares `grab_all(accels[])` / `ungrab_all()` vtable methods and a `keybinding-activated(const gchar *id)` signal. Implemented by `GPasteGnomeShellClient`, `GPasteGtkGlobalShortcutClient`, and the daemon-internal `GPasteInternalKeybindingProvider`. The `GPasteKeybindingAccelerator` struct `{ const gchar *id; const gchar *accelerator; const gchar *description; }` is the transfer type between keybinder and provider; arrays are null-terminated by `.id = NULL`. `description` is the translated label the GlobalShortcuts portal shows in its permission dialog. The array the keybinder hands over is the caller's and lives only for the call, so a provider that has to remember what it registered copies it with `g_paste_keybinding_accelerators_copy()` (freed with `g_paste_keybinding_accelerators_free()`; there is deliberately no `g_autoptr` cleanup for the type, since the same struct is also used for arrays whose strings are borrowed — the one the keybinder builds — and nothing distinguishes the two) and asks `g_paste_keybinding_accelerators_match()` whether a new set is the one it already holds — one comparison for every provider, rather than one per storage layout.

The accelerator array helpers and `grab_all()` (including its vfunc) are
C-only and marked `(skip)` for introspection. Their terminator is a struct with
`.id = NULL`, which GIR cannot describe as an ordinary array: Vala allocates
only the supplied elements, so scanning for a sentinel would read past it.
Static inline helpers with automatic cleanup are hidden from `__GI_SCANNER__`,
whose C parser does not understand those cleanup declarations.

**`GPasteGtkGlobalShortcutClient`** wraps the XDG GlobalShortcuts portal (`org.freedesktop.portal.Desktop`). It implements `GPasteKeybindingProvider`; every non-empty `grab_all()` creates a session of its own, for the reason the next paragraph gives. The public API is limited to constructors and the GObject type — all shortcut registration goes through the provider interface. Internally it stores the registered set as an owned `GPasteKeybindingAccelerator` array and handles the portal's Request/Response async pattern transparently. Like the gnome-shell client's, its `grab_all()` and `ungrab_all()` are safe to call on a disposed client: the shortcuts array is then `NULL` — `ungrab_all()` leaves an empty one, which is a set GPaste still holds — and the session `dispose()` closed is already gone.

A portal session binds its shortcuts **once**: the spec says an application "can only attempt to bind shortcuts of a session once", and xdg-desktop-portal rejects a second `BindShortcuts` on the same session. Rebinding is therefore impossible, and `org.freedesktop.portal.Session.Close()` on the session handle is the only way to release what a session bound. So `grab_all()`, `ungrab_all()` and `dispose()` all close the current session, and a non-empty `grab_all()` then creates a fresh one (with a `session_handle_token` of its own, since the old session's object path may not be free yet). A `grab_all()` handed the very set the live session already bound is a no-op, since `GPasteSettings` emits a rebind for any write to an accelerator key — a write of the value already stored included — and closing a session to ask for an identical one puts the portal's permission dialog back in front of the user over a set nothing has changed. That comparison is only possible because the keybinder hands the new set over in a single `grab_all()` and does **not** ungrab first: a provider told to release everything before being handed a set has nothing left to recognise it by. The portal coming back after a restart creates a session for the set still held, the way the gnome-shell client re-grabs from `on_shell_appeared()`: nothing else ever would, and the shortcuts are bound to nothing until it does — but only when no session and no request are alive, since the name appearing because GPaste's own `CreateSession` D-Bus-activated the portal is not a restart, and starting over there would close the session that request is about to be answered with and take the permission dialog the user is looking at down with it. Like the gnome-shell client, it keeps a `generation` counter for the requests the portal has already taken: a `CreateSession` reply that comes back stale closes the session it just got, a stale `BindShortcuts` reply has nothing to do (whoever bumped the generation already closed that session) — including a stale *method* error, which must not close the session that superseded it — and all of them fail their `GTask` with `G_IO_ERROR_CANCELLED`, which the provider callback does not warn about.

What counts as a request still worth waiting for is a request **of the current generation, still to be answered, and not one kept alive for a session alone** (`has_live_request()`), not merely one still in the list: a superseded `CreateSession` is kept only to close the session it may yet be answered with (`retired`), a cancelled request lingers until the callback that frees it runs on a later main-loop iteration, and a request the portal has already answered (`responded`) is finished — its denial dealt with, its method reply merely waiting for a later main-loop iteration to be dispatched on. None of them will ever bind the set now held, so none may stand in for one — as `priv->requests` alone, both the `grab_all()` no-op guard and the portal-restart recovery would be told a session is on its way when nothing is coming, and the shortcuts would stay bound to nothing until the next settings write. A retired `CreateSession` is also given a **10 minute** deadline: a portal that takes the call, replies to it and then never emits `Response` would otherwise keep the request, its subscription and the session it may still be answered with for the rest of the process. A `CreateSession` that has already been answered is not retired that way at all: only its method reply is still to come, and cancelling the call is what brings that forward.

A portal that hands the bus name straight to its replacement never lets go of it, so there is no `NULL` owner in between to say that its sessions went with it. The client therefore records the **owner the session was created under** (`priv->session_owner`) *and the owner every request was issued to* (`data->owner`), and treats any other owner as a loss (`portal_was_replaced()`): left alone, the handle would go on naming a session the portal now on the name knows nothing about — one no `Activated` can ever match, and one the recovery guard would read as a session GPaste still has. The session alone is not enough to notice with, since a `CreateSession` still waiting for its `Response` has no session yet: that request would wait on a `Response` nobody is left to send, holding its subscription for good, while the recovery guard reads it as a bind on its way and refuses to start over. A request issued while **nobody owned the name** — GPaste's own call is what D-Bus-activates the portal — records no owner, and takes the one that turns up, so the handoff after that one has something to be told apart from.

The `Response` subscription names the portal as its **sender**, and the `handle_token`/`session_handle_token` carry a random half as well as the counter that makes them unique. Without both, any peer on the session bus could answer a request on a path it can predict — with a session handle of its own, which is what `BindShortcuts` would then be told to bind and what every `Activated` is matched against. The handle that comes back is checked for being a string (or an object path: portals send both) *and* a valid object path before it is stored, since `g_variant_new_object_path()` on anything else aborts the process rather than failing the session.

Every request in flight is tracked in `priv->requests` and retired when the session it was made for is closed under it: a `GCancellable` if the method call has yet to reply, and the `GTask` failed with `G_IO_ERROR_CANCELLED` if only the `Response` is left. Nothing else would ever free one — no `Response` follows a session that is gone, so a stranded request holds its subscription for the rest of the process. A request holds the client **weakly**, and its `GTask` is created with no source object, for the reason a `GSource` does: `dispose()` is the one thing that retires a request, and a reference of either kind would be one the client holds on itself through the very list that ends it — an in-flight request would keep the client, its session and its global grabs alive for as long as the portal took to answer, up to that 10 minute deadline. Every callback opens the weak reference with `g_weak_ref_get()` and treats a client that is gone as a superseded generation (`portal_request_is_current()`); a session that arrives for one is closed on the request's own connection, since only that `Response` ever carries its handle. `dispose()` empties the list rather than unlinking one request at a time: GObject clears weak locations **before** `dispose()` runs, so a request freed by the supersede it does could not take itself out, and what it did not free is a retired one — neither may be read again, which is why `retire_requests()` walks a copy. A request outliving the client then frees itself, finding no client to unlink from. The portal losing its bus name (`notify::g-name-owner`) retires them all, and takes the session handle with it. The requests that are *not* retired are the `CreateSession`s: cancelling one abandons the wait, never the call the portal has already taken, and the handle of the session it goes on to create appears in that request's `Response` and nowhere else — so it is left to run, and closing the session it is handed is all its handler does once it sees the generation has moved on. Several of those can be outstanding at once, which is why the tracking is a list rather than a single slot. A `CreateSession` whose **method call** comes back with an error is kept for the same reason, and only a cancellation — the one thing that call is ever cancelled by is the portal going away — frees it: the 25 s D-Bus timeout says nothing about whether the portal took the call, a backend held up behind a dialog of its own answers long after it, and dropping the subscription there would leave the session that `Response` carries bound to whatever it binds for the rest of the process. The failure is reported to the `GTask` straight away and the request lives on under the retired deadline, which is why **every task return goes through `portal_request_take_error()` / `portal_request_return_boolean()`**: a `GTask` answered twice is a critical, and this is the one request that outlives its own answer. A `Response` that does arrive for it is only good for **closing** the session it carries, never for binding it — `on_session_created()` treats a retired request the way it treats a superseded one — since the bind would have that spent task to report on.

The **method reply of a portal call is only the request's object path** — the outcome arrives later on that request's `org.freedesktop.portal.Request::Response` signal. Both `CreateSession` and `BindShortcuts` subscribe to it; treating the method reply as the result reports a user denying the permission dialog as a success.

That subscription goes in **before the call**, not when the reply lands: the portal is free to emit `Response` before the method reply is dispatched, and a `Response` emitted first is simply missed — the `GTask` then never completes and the request is stranded. Each call therefore passes a `handle_token` of its own and the request path is predicted from it (`/org/freedesktop/portal/desktop/request/<unique name, no leading colon, dots as underscores>/<token>`), which is exactly what the token is for. A connection with no unique name has no path to predict, and the call is failed rather than made — subscribing to nothing would strand the request. Both tokens are built by the same helper from a counter of their own, the request's and the session's. The method reply still names the request the portal actually created, and the subscription moves if that is not the predicted path — a portal that ignored the token answers on its own choice. `CreateSession` passes both tokens: `handle_token` for the request, `session_handle_token` for the session.

A `Response` that arrives first also means the method call is still outstanding with the request as its `user_data`, so **whichever of the two runs last frees the request**. Freeing it from the `Response` handler leaves the method callback on freed memory — and, once the block has been handed to the next request, on somebody else's.

**`GPasteGnomeShellClient`** implements `GPasteKeybindingProvider` using GNOME Shell's `GrabAccelerators` D-Bus API. It stores a `GHashTable` mapping shortcut id → GNOME Shell action id, retries on `G_DBUS_ERROR_UNKNOWN_METHOD` (up to 10 times), and watches the shell bus name to re-grab on shell restart.

The shell answers `GrabAccelerators` with **one action id per accelerator, `0` for each one it refused** (already held by another application, or unparseable): storing that `0` claims a grab GPaste does not have, so it is warned about and skipped. Ids past the accelerators that were asked about are handed straight back — nothing could ever look them up again. Releases go through `UngrabAccelerators (au)`, one call for the whole set, rather than one `UngrabAccelerator` per action. That method is **newer than `UngrabAccelerator`**, so its reply is looked at rather than dropped: a shell that does not have it answers `UnknownMethod`, and with nothing watching for that every release — a rebind, a stale hand-back, `dispose()` — would silently leave the shell holding GPaste's accelerators, as dead keys stolen from every other application. The `false` it answers when one entry names nothing it holds is only logged at debug level: the stale hand-back can legitimately ask for one.

Only **one `GrabAccelerators` is ever outstanding**. The shell refuses an accelerator that is already grabbed — by GPaste just as much as by anyone else — with an action id of `0`, and the grabs a call it has yet to answer is about to be granted cannot be handed back yet, since only that answer carries their ids. A second call for the same set would therefore collide with the first accelerator for accelerator and come back all zeros, leaving no global shortcut at all until the next rebind. A `grab_all()` that lands while a call is in flight only stores its set and raises `pending_grab`; the reply hands its own ids back and then asks for the stored set, in that order on the same connection, so the shell has released them before it looks at the new request. `ungrab_all()`, an empty `grab_all()` and the shell vanishing all drop a pending grab. What is tracked is the **id of the call in flight**, not a plain boolean, because the shell vanishing *abandons* that call rather than waiting it out: a shell replaced mid-flight may never answer, and the replacement would be asked for nothing at all until the 25 s D-Bus timeout said so. A late reply then no longer carries the id in flight, so it cannot report the call issued since as done — without that, the next grab would go out alongside one that is still outstanding and collide with it accelerator for accelerator, which is the very thing one-call-at-a-time exists to prevent. For the same reason the pending grab is drained only by the reply of the call that is actually in flight. `gnome_shell_client_regrab_stored()` issues the stored set through `gnome_shell_client_issue_grab()` rather than back through `grab_all()`: the set asked for is the one already held, so going round would only copy the stored set over itself, string by string, on every retry and every shell restart. Both go through **`gnome_shell_client_restart_grab()`** — drop the queued retry, bump the generation, hand back what is held, then either raise `pending_grab` or issue — which is every way into the grab there is; `grab_all()` is left responsible for the set alone, and its empty case *is* `ungrab_all()`, down to the retry budget and the pending grab, so it calls it rather than spelling it out a second time. Two copies of that tail had already drifted apart over what they reset.

A `grab_all()` handed the set the client already holds — or the one the call still out is about to bring — is a **no-op** (`g_paste_keybinding_accelerators_match()`, the comparison both D-Bus providers share), for the reason the portal provider's is: `GPasteSettings` emits a rebind for any write to an accelerator key, a write of the value already stored included, and going through with it costs an ungrab and a grab with no global shortcut at all in between. A set the client holds only partially, or nothing of (a grab that failed, or accelerators the shell refused), is asked for again: a refused shortcut may have become available since the last attempt, so only a complete set of held grabs can make an unchanged set a no-op once no call is in flight. What decides that is provider state — a partial grab here, a live session there — so the comparison is shared but the decision is not, and `GPasteInternalKeybindingProvider` deliberately makes none: its accelerator strings parse to the *keymap's* keycodes, and re-grabbing an unchanged set is the only thing that ever picks up a layout change under a running daemon.

The retry `GSource` holds a **weak** reference to the client: `dispose()` is what cancels it, and a strong one would put that out of reach, since `dispose()` only runs once the last reference is dropped.

`GrabAccelerators` is asynchronous and there is no way to cancel one in flight, so the client keeps a `generation` counter, bumped by `grab_all()`, `ungrab_all()`, the shell vanishing and `dispose()`. Each in-flight grab remembers the generation it was issued for, and a reply that comes back stale hands the action ids the shell just granted straight back instead of storing them — without that, disabling the shortcuts (or toggling them off and on) during the round trip leaves the shell holding grabs GPaste believes it released, as dead keys stolen from every other application. `grab_all()` releases the grabs it currently holds before registering the new set, so it honours the interface contract for an empty array too, and `dispose()` releases them as well: a client dropped without a prior `ungrab_all()` would otherwise leave the shell holding every accelerator for the rest of the session. The stale hand-back skips the `0` slots for the same reason the success path does — `0` is the shell refusing an accelerator, not an action id it granted, and `UngrabAccelerators` walks the array it is handed and comes back `false` for the whole set when one entry names nothing it holds — and it clears the retry budget when the reply carried no error, the shell having answered being what the budget was waiting for. It goes to the shell that granted those ids and to no other, which is why each call records **which shell it was issued to** (`ctx->shell_epoch`, against `priv->shell_epoch`): a shell allocates action ids from a counter that starts over with it, so the ids in a reply from one that has since been replaced name the grabs the shell now on the name has just granted GPaste — handing those back would release the very accelerators the regrab that replacement triggered asked for. What the old shell held went with it, and needs no releasing. That epoch is bumped by `on_shell_vanished()` and by nothing else, so it is in step with the very subscription that drives the regrab. A name owner read off the proxy is **not**: the proxy tracks `NameOwnerChanged` under a subscription of its own, dispatched independently of the watch, so a grab issued from the appeared handler can record an owner the proxy has yet to catch up with, and its reply — by which time it has — would be taken for a stale one and its grabs left held for the rest of the session. A shell handing the name straight to its replacement — `gnome-shell --replace` — needs nothing further of its own, unlike the portal client's `portal_was_replaced()`: `g_bus_watch_name()` calls the vanished handler for **any** old owner that is not empty before it calls the appeared one, so `on_shell_vanished()` has already dropped the action ids, the call in flight and the retry budget by the time the regrab goes out. The portal client cannot lean on that — it watches `notify::g-name-owner` on its proxy, which reports a handoff as one new owner and nothing else.

**GObject type macros** — use these in `.c` files:

| Macro | Use when |
|---|---|
| `G_PASTE_DEFINE_TYPE` | Simple concrete type, no private data, no interface |
| `G_PASTE_DEFINE_TYPE_WITH_PRIVATE` | Concrete type with a `Private` struct |
| `G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE` | Concrete type with private data **and** one interface implementation |
| `G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE` | Abstract base class with private data |

`G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE(TypeName, type_name, ParentType, IFACE_TYPE, iface_init)` expands to `G_DEFINE_TYPE_WITH_CODE` + `G_ADD_PRIVATE` + `G_IMPLEMENT_INTERFACE` and also generates the const-safe `_g_paste_<type_name>_get_instance_private` accessor (annotated `G_GNUC_UNUSED` so it does not warn when not called).

### `src/daemon/` — `gpaste-daemon`

The background service. Owns the clipboard history and exposes it over D-Bus (`org.gnome.GPaste`). Handles:

- Clipboard watching (primary + clipboard selections)
- Item types: text, password, image, URI
- Keybinding registration via a three-level fallback: XDG GlobalShortcuts portal → GNOME Shell → internal X11/Wayland (`GPasteInternalKeybindingProvider`); all three implement `GPasteKeybindingProvider`
- The `keybindings-enabled` setting is a master switch: `GPasteKeybinder` gates `g_paste_keybinder_activate_all()` on it, caches in `priv->enabled` the value it last applied (not whether the provider ended up holding anything — a grab can fail on the far side), and rebinds on `changed::keybindings-enabled` only when the setting no longer matches it (GSettings emits `changed` for any write, including one that re-sets the value it already holds). `priv->enabled` is seeded from the setting in `g_paste_keybinder_new()`, before anything can notify about it: zero-initialised it reads as off while the setting defaults to on, and a change landing before `activate_all()` would compare against that. A switch flipped and flipped back inside the debounce window leaves the rebind the first write armed standing, and it costs nothing: the set it hands over is the one every provider already holds, and both D-Bus providers recognise it and stay put. That is the general fix for a redundant rebind — an accelerator written back to the value it already had is the same waste — and it is why the keybinder has no special case of its own for the switch
- Rebinds are **debounced** on a 250 ms timeout (`keybinder_rebind_all()`): an accelerator typed into the preferences writes its GSettings key on every keystroke, and each write would otherwise cost a full ungrab and grab — a D-Bus round trip each for the gnome-shell and portal providers — and reopen the window with nothing grabbed in between. A pending timeout is re-armed rather than left alone, so the delay is measured from the *last* write of the burst; throttling instead would fire mid-edit and cost a rebind per 250 ms. The source holds the keybinder **weakly**, `dispose()` being what cancels it
- `g_paste_keybinder_activate_all()` deactivates every keybinding before it activates them, and that pair is load-bearing: `g_paste_keybinding_activate()` caches the parsed keycodes and `_keybinding_activate()` skips a binding that is already active, so without the deactivate an edited accelerator never reaches the internal provider. It deactivates the *keybindings* only — the `_keybinding_deactivate()` walk it opens with — not the provider's grabs, and it does so whatever the master switch says — a binding left armed under a switch that has just gone off would go on firing through the internal provider, which does its own listening, and `keybinder_do_rebind_all()` has nothing of its own left to do about it. The provider is then handed the new set in one `grab_all()` — an empty one when the switch is off, which is how a rebind that turns the shortcuts off releases them — and a provider asked to ungrab first would have nothing left to tell a set that has not changed from a new one. Every provider's `grab_all()` replaces whatever it holds, so the ungrab was only ever redundant — which is why there is no public `deactivate_all()` counterpart to `g_paste_keybinder_activate_all()` any more: nothing was left calling it once the rebind path stopped, and it was the one place that moved `priv->enabled` without a grab to match
- History persistence to disk

### `src/client/` — `gpaste-client`

CLI tool for scripting and shell integration. Talks to the daemon via `GpasteClient`. Entry point for subcommands like `ui` (launches the GTK UI) and `daemon-reexec`.

### `src/ui/` — `gpaste-ui` (GTK4 + libadwaita)

The main graphical history browser. Launched via `gpaste-client ui`. Contains widgets for the history list, item actions, search, and settings panel. Uses libadwaita widgets extensively:

- `AdwApplicationWindow` (via `G_PASTE_GTK_INIT_APPLICATION` which uses `AdwApplication`)
- `AdwHeaderBar` with `AdwWindowTitle` (current history name shown as subtitle)
- `AdwShortcutsDialog` / `AdwShortcutsSection` / `AdwShortcutsItem` (keyboard shortcut help). The dialog is a snapshot of the settings it was built from and can neither drop a section nor update an accelerator, so `GPasteUiWindow` closes the one it presented on `changed::keybindings-enabled` and on `rebind`, and builds a new one on the next request. The close is **coalesced on the same 250 ms delay the daemon rebinds over**, and for the same reason: `rebind` comes one write per keystroke, and closing on the first of them takes the dialog out from under an edit that has not settled. Closing drops the weak pointer itself rather than waiting for the close transition to unparent the dialog and clear it: a present in between would raise the very snapshot being retired. A request for the dialog with a close still pending **retires whatever is up first** (`ui_window_retire_shortcuts()`) and builds a fresh one: the pending close says what is up has gone out of date, and the user asking for the dialog is asking for one that tells the truth — raising the stale snapshot and letting the close shut it 250 ms later reads as the accelerator dismissing the dialog. The same call is what cancels a close armed for a dialog the user shut themselves before any further `rebind` arrived, which would otherwise shut the fresh snapshot that is everything that close was making room for; a `rebind` that does arrive with nothing up disarms it there instead, so a source is armed only for a dialog that is up. The dialog constructor returns a **floating** reference, and the caller sinks it into a `g_autoptr` for the length of the present rather than leaving `adw_dialog_present()` to: a present that does not take the dialog would leak it and leave a weak pointer that never clears, and every later request would then re-present a dialog that is not up. A notice can only be a **section title**, and the title of a section that has items: an `AdwShortcutsItem` with an empty accelerator renders a "No Shortcut" chip next to its text, one with no accelerator at all is not rendered, and a section with no items at all is outside libadwaita's contract — so the master switch being off is said in the title of the one section there is, rather than in an empty section of its own
- `AdwToastOverlay` (wraps main content for future toast notifications)
- `AdwBanner` (shown on daemon connection failure)
- `AdwNavigationSplitView` (responsive split: history selector panel + history list)
- `AdwStatusPage` (shown when history is empty or search has no results)
- `AdwEntryRow` (in panel's "Switch to history" entry, inside a `boxed-list` GtkListBox)
- `AdwSidebar` / `AdwSidebarSection` / `AdwSidebarItem` (history selector list in the panel sidebar, libadwaita 1.9)
- `AdwAboutDialog` (about dialog)

**Subclassing notes:** GTK4 made many widget classes final (`G_DECLARE_FINAL_TYPE`), preventing subclassing. Libadwaita re-enables subclassing for its own derivable types. Within the ui, internal widgets subclass derivable types: `GtkBox` (for `GPasteUiPanel`, `GPasteUiHistory`), `AdwApplicationWindow` (for `GPasteUiWindow`), `AdwSidebarItem` (for `GPasteUiPanelHistory` — a GObject, not a widget), etc. `GtkStack` is final in GTK4 and cannot be subclassed — use `GtkBox` with manual visibility toggling instead.

**Label widgets in list rows** use `GtkInscription` (not `GtkLabel`) for the main text display. `GtkInscription` is optimised for list-item cells and avoids overhead from markup/accessibility features not needed there. Set overflow explicitly with `gtk_inscription_set_text_overflow(GTK_INSCRIPTION_OVERFLOW_ELLIPSIZE_END)` — the default is `CLIP`. To display bold text, use `pango_parse_markup` to convert a markup string into `PangoAttrList` and pass it to `gtk_inscription_set_attributes`; call `gtk_inscription_set_attributes(NULL)` before `set_text` when switching back to plain text.

**AdwSidebar** (libadwaita 1.9) is used in `GPasteUiPanel` to list available histories. `GPasteUiPanelHistory` subclasses `AdwSidebarItem` (a `GObject`, not a widget) to represent each history entry. The right-click context menu (backup/empty/delete) is driven by a `GMenuModel` set on the sidebar, with `GSimpleAction`s installed on the panel widget under the `panel.` prefix.

### `src/preferences/` — GTK4 + Adwaita preferences application

Standalone preferences window. Uses GTK4 and libadwaita for a modern look.

A page registers itself with the `GPasteGtkPreferencesManager` **after it has
built its rows**, not before: `setting_changed()` is handed every `changed` from
the moment of the register, and a page that registers first reaches it with its
row pointers still unset. The entry path tolerates that (`entry` is also `NULL`
for a key the page does not handle), but a row handed straight to a setter —
`adw_switch_row_set_active()` on the `keybindings-enabled` switch — would take a
`NULL` and log a critical.

### `src/gnome-shell/` — GNOME Shell extension (JavaScript)

Native shell integration. Provides the panel indicator and quick-access popover. Communicates with the daemon via D-Bus. Lives under the standard GNOME Shell extension layout with `extension.js` as entry point.

### `data/`

Non-code resources: D-Bus service files (`dbus/`), `.desktop` entries, GSettings schemas (`gsettings/`), systemd user units, AppStream metadata.

### `po/`

Translations managed via Weblate. Add new strings to the relevant `.c` source with `_()` / `N_()` and update `po/POTFILES.in` if adding a new file.

## Key dependencies

- GLib/GObject/Gio ≥ 2.84
- GTK4 ≥ 4.18 + libadwaita ≥ 1.9 (UI and preferences; 1.9 required for `AdwSidebar`)
- GCR (`gcr-4`) ≥ 3.90 (password item storage)
- gjs ≥ 1.78 (GNOME Shell extension runtime)
- Optional: libX11 + libXI (x-keybinder)

Image items use `GdkTexture` from GTK4 directly — there is no longer a GdkPixbuf dependency.

On Fedora: `dnf install meson ninja-build glib2-devel gtk4-devel gcr-devel libadwaita-devel gjs-devel`
