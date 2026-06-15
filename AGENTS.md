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
| `systemd` | true | systemd user unit |

For a lighter build that skips the GNOME Shell extension, GIR introspection data
and Vala bindings (the daemon, UI and preferences apps are always built):

```sh
meson .. -Dgnome-shell=false -Dintrospection=false -Dvapi=false
```

Run tests from the build directory:

```sh
ninja test          # or: meson test -C build
```

Tests live under `tests/`. `tests/history/` unit-tests the `GPasteHistory` model
(add/dedup/size-enforcement/remove/select) against an in-memory `GSettings`
(`GSETTINGS_BACKEND=memory` + the schema compiled into the build tree) and a
throwaway `XDG_DATA_HOME`, so they need no display server or dconf. The `eslint`
test lints the GNOME Shell extension JS.

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
- Do **not** use `g_autolist`/`g_autoslist` on borrowed lists (e.g. `gdk_file_list_get_files` is `(transfer none)`). `g_hash_table_get_values` also returns a borrowed list — use `g_autoptr(GList)`.
- Do **not** use auto-cleanup on a variable whose ownership is intentionally transferred — use `g_steal_pointer` to make the handoff explicit.
- Do **not** use `if (ptr) g_slist_free_full (g_steal_pointer (&ptr), fn)` in dispose — use `g_clear_slist (&ptr, fn)` instead (handles NULL safely).
- When storing a `g_settings_get_string` value (owned, `(transfer full)`) into a `gchar *` field, free the old value and take ownership directly: `g_free(field); field = g_settings_get_string(...)`. Do **not** assign without freeing first (leak on re-assignment), and do **not** route it through `g_set_str` (that would re-`g_strdup` an already-owned string — a wasted allocation).
- For a stored `GSource`/timeout/idle/signal handler id, prefer `g_clear_handle_id (&id, remove_func)` over the hand-rolled `if (id) { remove_func (id); id = 0; }` — it is NULL-safe and nulls the field in one call (e.g. `g_clear_handle_id (&priv->retry_source, g_source_remove)`). Use this in new code and when touching such cleanup.
- Build NULL-terminated string vectors with `GStrvBuilder` (`g_strv_builder_add` for borrowed strings, `g_strv_builder_take` for owned ones, `g_strv_builder_end` to get the `GStrv`) rather than hand-managing a `GPtrArray` + manual `NULL` terminator + `(GStrv) ->pdata` cast. It also avoids the deep `g_strdupv` copy that returning a `GPtrArray`'s contents requires.
- Prefer `g_signal_connect_object (source, sig, cb, gobject, 0)` over plain `g_signal_connect` when the handler's data is a **GObject** shorter-lived than the signal source and you would otherwise not disconnect — it auto-disconnects when that object is finalized. Do **not** convert sites that pass non-GObject data (a `priv`/struct pointer), pass `NULL` data, or that deliberately disconnect early in `dispose` for ordering reasons (where `g_signal_connect_object` would only disconnect at finalize and regress that ordering).

## Maintenance rules

- When adding or removing a public function from any of the libraries, update the corresponding `.sym` file: `src/libgpaste/libgpaste.sym`, `src/libgpaste/libgpaste-gtk4.sym`, or `src/daemon/libgpaste-daemon.sym`.
- When updating source files in this repository, keep `CLAUDE.md` up to date to reflect any new patterns, rules, or architectural decisions introduced.

## Architecture

GPaste is a GNOME clipboard manager split across several binaries and a shared library.

### `src/libgpaste/` — shared libraries

The core libraries used by all other components. Two sub-modules:

- `gpaste/` → **libgpaste** — daemon-agnostic types: `GPasteClient` (D-Bus client), `GPasteClientItem` (the lightweight item representation transferred over D-Bus), `GPasteSettings` (GSettings wrapper), enums, utilities.
- `gpaste-gtk4/` → **libgpaste-gtk4** — GTK4 + Adwaita helpers (the preferences widgets).

A third library, **libgpaste-daemon**, lives under `src/daemon/` (see below) and holds the rich clipboard item hierarchy along with the rest of the daemon objects. Each library exposes a versioned ABI via a symbol version script, and GIR + Vala bindings are generated from it.

**GObject type macros** — use these in `.c` files:

| Macro | Use when |
|---|---|
| `G_PASTE_DEFINE_TYPE` | Simple concrete type, no private data, no interface |
| `G_PASTE_DEFINE_TYPE_WITH_PRIVATE` | Concrete type with a `Private` struct |
| `G_PASTE_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE` | Abstract base class with private data |

### `src/daemon/` — `gpaste-daemon` + **libgpaste-daemon**

The background service. Owns the clipboard history and exposes it over D-Bus (`org.gnome.GPaste`). All of its objects live in the installed, introspectable **libgpaste-daemon** library (sources under `src/daemon/gpaste-daemon/`, umbrella header `src/daemon/gpaste-daemon.h`, version script `src/daemon/libgpaste-daemon.sym`, `GPasteDaemon-1` GIR/typelib); `src/daemon/gpaste-daemon.c` is the thin executable entry point that links it. Its types keep the `GPaste`/`g_paste_` prefix, so the GIR passes an explicit `identifier_prefix: 'GPaste'` / `symbol_prefix: 'g_paste'` to place them in the `GPasteDaemon` namespace. Handles:

- Clipboard watching (primary + clipboard selections), through the backend-agnostic `GPasteClipboardProvider` interface
- The rich clipboard item type hierarchy: the abstract `GPasteItem` base, the plain `GPasteTextItem`/`GPastePasswordItem`, the GTK4-backed `GPasteColorItem`/`GPasteImageItem`/`GPasteUrisItem`, and the `GPasteSpecialAtom`/`GPasteBinaryData` helpers (the UI/client instead use libgpaste's lightweight `GPasteClientItem`)
- Keybinding registration through the XDG GlobalShortcuts portal (`GPasteGlobalShortcutClient`, used directly by the keybinder); if the portal is unavailable, keyboard shortcuts are simply disabled
- History persistence to disk: `GPasteHistory` owns the in-memory model and dedup/size policy, but delegates the asynchronous I/O to `GPasteHistorySaver`, which writes snapshots handed to it (coalescing concurrent requests) and loads histories in the background. The saver never touches the live list, so it shares no locking with the model; it hands load results back through a callback.

**Storage backends & migration.** *Where* the history is persisted is abstracted behind `GPasteStorageBackend` (`gpaste-storage-backend.{c,h}`), a derivable GObject with `read_history_file`/`write_history_file`/`get_extension` plus optional `delete_history`/`list_histories` vfuncs. `g_paste_storage_backend_new (GPasteStorage kind, settings)` is the factory; `GPasteStorage` enumerates `NOOP` (`GPasteNoopBackend`, "no storage" — reads empty, drops writes, lists nothing, so the history lives only in memory for the session), `FILE` (the XML on-disk backend, `GPasteFileBackend`) and `ENCRYPTED_FILE` (the same `GPasteFileBackend` created via `g_paste_file_backend_new_encrypted (settings, passphrase)` — an encrypted ".xmls" history; only offered when the encryption feature is built). `GPasteHistory` picks its backend from the `storage-backend` GSettings key (a gschema `<enum>` of string nicks, read via `g_paste_settings_get_storage_backend`). The encrypted backend needs a passphrase, which the factory takes from a process-wide gcr-secure global (`g_paste_storage_backend_set_passphrase`/`get_passphrase`) set once at startup, so no constructor signature has to thread the secret; with no passphrase set it degrades to `NOOP` rather than writing plaintext. To add a backend (e.g. SQLite): add an enum value + nick, a `GPasteStorageBackend` subclass, and a `case` in the factory. **Migration** (`gpaste-storage-migration.{c,h}`): on startup `g_paste_storage_migration_prepare()` (called from the `gpaste-daemon.c` entry point, before the `GPasteDaemon` is created) compares the stored `storage-backend-revision` against `G_PASTE_STORAGE_BACKEND_REVISION`; if they differ it shows an Adwaita dialog (in the daemon's own `AdwApplication`, gated by a nested `GMainLoop` *before* the `GPasteDaemon` is created so the chosen backend is in effect) letting the user pick a backend, optionally import the existing file history into it (only enabled when the target both stores data and differs from the previous `file` default) and optionally delete the old on-disk data afterwards (with a warning banner when cleanup is ticked without import). Confirming writes `storage-backend` + bumps the revision; dismissing leaves the revision so the dialog returns next start (forcing a deliberate choice — the privacy default of #357), using a suggested backend for the session (`file` if histories already exist, else `none`). The same dialog is reachable on demand through the `storage-migration` `GAction` registered on the application. When **encrypted file** is chosen, applying first raises a reusable passphrase prompt (`g_paste_storage_migration_prompt_passphrase`, `confirm=TRUE`: two fields + a data-loss warning) and sets the global passphrase before importing/storing; an already-configured encrypted history is unlocked at startup with the same prompt (`confirm=FALSE`). The prompt lives in the daemon, not the UI. When the `libsecret` feature is built (`G_PASTE_ENABLE_LIBSECRET`, which also requires encryption), the prompt grows a "Remember this passphrase" toggle that stores it in the keyring (`gpaste-storage-keyring.{c,h}`, a single attribute-less `SecretSchema`); at startup `g_paste_storage_migration_prepare()` calls `g_paste_storage_keyring_apply()` first and only prompts if nothing was remembered.

**History encryption** (`gpaste-secret-stream-converter.{c,h}`, optional — built only when the `encryption` meson feature finds libsodium, which also defines `G_PASTE_ENABLE_ENCRYPTION`): a bidirectional `GConverter` (the same final type does both directions via a `GPasteSecretStreamDirection` ctor argument) built on libsodium's `crypto_secretstream` (XChaCha20-Poly1305). The symmetric key is derived from a user passphrase with `crypto_pwhash` (Argon2id, MODERATE limits); the random salt and the Argon2 parameters are written into the stream header (`MAGIC · salt · opslimit · memlimit · secretstream-header`) so decryption reproduces the key. Plaintext is split into ≤4 KiB chunks, each emitted as a `u32` length prefix + ciphertext, the last carrying the secretstream FINAL tag (so truncation is caught). Both directions buffer internally (a `GByteArray` for pending input and one for produced output) to cope with the arbitrary buffer splits `GConverter::convert` hands them; "no progress" maps to `G_IO_ERROR_NO_SPACE`/`PARTIAL_INPUT` like GLib's own converters. The passphrase and derived key live in **gcr secure memory** (`gcr_secure_memory_*`, non-swappable, wiped on free); the passphrase is retained for the converter's lifetime because the key is salt-dependent and `reset()` must re-derive it for a fresh stream. `GPasteFileBackend` uses it for the `ENCRYPTED_FILE` backend: it wraps its read/write file streams with the converter, uses the ".xmls" extension, and (unlike the plain backend) persists password entries — writing their real value (`g_paste_item_get_real_value`) plus the `name` attribute, since the file is unreadable without the passphrase.

**`GPasteGlobalShortcutClient`** (daemon-private, in `src/daemon/`) wraps the XDG GlobalShortcuts portal (`org.freedesktop.portal.Desktop`) as a `GDBusProxy` subclass. It is the daemon's only keybinding mechanism: `g_paste_global_shortcut_client_grab_all(accels[])` / `_ungrab_all()` register/release shortcuts and the `keybinding-activated(const gchar *id)` signal fires when one is pressed. The portal session is created lazily on the first `grab_all` call. The `GPasteKeybindingAccelerator` struct `{ const gchar *id; const gchar *accelerator; const gchar *description; }` is the transfer type between keybinder and client; arrays are null-terminated by `.id = NULL`. Internally it stores registered shortcuts as a `GPtrArray` of private `_Shortcut` structs and handles the portal's Request/Response async pattern transparently.

**Clipboard backends.** `GPasteClipboardProvider` (`gpaste-clipboard-provider.{c,h}`) is a GObject *interface* that abstracts a single selection (clipboard or primary): it caches the current content, exposes `update`/`select_text`/`select_item`/`sync_text`/`ensure_not_empty`/`store`/`get_text`/`get_image_checksum`/`is_clipboard` as vfuncs, and declares the `changed` signal (emitted by implementations via `g_paste_clipboard_provider_emit_changed`). Crucially, none of its method signatures expose toolkit types, so the `GPasteClipboardsManager` drives every backend through this one contract. Both concrete backends hold their current selection in a shared `GPasteClipboardContent` (`gpaste-clipboard-content.{c,h}`): a content-kind enum plus a tagged union (text/image-checksum string, file list, or RGBA) with shared `clear`/`get_text`/`get_image_checksum` helpers, so only the field matching the kind is ever live. The same module owns the cross-backend policy helpers — `g_paste_clipboard_content_classify_text` (the trim/min-max/dedup decision both backends apply to incoming text) and `g_paste_clipboard_file_list_equal` (file-list dedup). The nine interface thunks each backend needs are generated by the `G_PASTE_CLIPBOARD_PROVIDER_DEFINE_VFUNCS (lc, UC)` macro; `ensure_not_empty` is a concrete method on the interface itself (driven by a per-backend `is_empty` vfunc), not reimplemented in each backend. The daemon stays backend-agnostic: `g_paste_daemon_new (GPasteSettings *settings, GPasteClipboardProvider *clipboard, GPasteClipboardProvider *primary)` feeds the two providers the caller built to the manager, so the choice of backend lives entirely in the caller. A single `GPasteSettings` is threaded all the way through — the caller creates it once and the daemon (history, clipboards manager) and both providers share that instance rather than each making their own. Two convenience wrappers build the providers from that `settings` and call it: `g_paste_daemon_new_gdk (settings)` (GDK backend, used by the `gpaste-daemon.c` entry point) and — when built with `gnome-shell` — `g_paste_daemon_new_meta (settings, selection)` (mutter backend, used by the in-shell `daemon.js`). `new_meta`'s `selection` is the `MetaSelection`, typed `GObject *` so the introspected header needs no libmutter; GJS passes a `Meta.Selection` straight in. `GPasteClipboardGdk` (`gpaste-clipboard-gdk.{c,h}`) is the GDK implementation (the former `GPasteClipboard`), built on `GdkClipboard`; it requires the X11/XWayland backend because only X11 delivers *global* selection-ownership notifications. Construct it with `g_paste_clipboard_gdk_new_clipboard()` / `_new_primary()` (both returning a `GPasteClipboardProvider *`). A colour item is published as both `application/x-color` and a `text/plain` fallback (its `gdk_rgba_to_string()` form) so it can also be pasted into plain-text targets; the meta backend does the same.

`GPasteClipboardMeta` (`gpaste-clipboard-meta.{c,h}`) is the experimental mutter implementation, built on `MetaSelection`. Unlike the GDK backend it is not a display client: it talks to mutter's server-side selection tracker, so it sees *every* selection ownership change globally (no keyboard-focus gating) — but that object is only reachable from inside the gnome-shell process, so the constructors (`g_paste_clipboard_meta_new_clipboard()` / `_new_primary()`) take the `MetaSelection` handed in by the shell glue (`global.display.get_selection()`). Reads go through `meta_selection_transfer_async()` into an in-memory stream; writes publish a source we keep a ref to so the resulting `owner-changed` is recognised as ours and skipped. Since mutter's `MetaSelectionSourceMemory` only ever advertises a single mimetype, writes use a file-local `GPasteClipboardMetaSource` (a `MetaSelectionSource` subclass holding an ordered mimetype list, a mimetype→`GBytes` map for byte payloads, and an optional typed `GValue` for GDK-serialised payloads). Plain text is stored as bytes under both `text/plain;charset=utf-8` and `text/plain`, and a text/uris item's special values (rich-text/HTML/XML, `x-special/gnome-copied-files`) are appended via `g_paste_special_atom_get()`, matching the `GdkContentProvider` union the GDK backend builds. Everything else is bridged through GDK rather than hardcoded mimetypes/byte formats: images (`GDK_TYPE_TEXTURE`), colors (`GDK_TYPE_RGBA`) and file lists (`GDK_TYPE_FILE_LIST`) are held as a `GValue`, the source advertises every format `gdk_content_formats_union_serialize_mime_types(type)` lists, and `read_async` lazily serialises the value (via `gdk_content_serialize_async`, closing the memory stream before `steal_as_bytes`) only for the format actually requested. On read, `g_paste_clipboard_meta_pick_mime()` matches the offered mimetypes against the cached `gdk_content_formats_union_deserialize_mime_types(type)` set (preferring the canonical `image/png` / `text/uri-list`) and one `update_on_value`/`on_value_deserialized` pair decodes through `gdk_content_deserialize_async(.., type)`. So format coverage tracks GDK's installed loaders automatically, colors and uris are byte-identical to the GDK backend (fixing an `application/x-color` endianness mismatch the old hand-rolled ICCCM codec had, and gaining the `application/vnd.portal.filetransfer`/`files` mimetypes sandboxed apps need for file paste), and `images-support` gates capture in `update()` exactly as in the GDK backend. Only the text path keeps literal mimetypes (its GDK set is already exactly those two, and it needs the backend's own trim/length/dedup logic); `image/png`/`text/uri-list` survive solely as read-preference hints. It is gated on the `gnome-shell` build option and pulls `libmutter-18`; when that option is on it is compiled into `libgpaste-daemon` (which then links libmutter). The in-shell glue is `src/gnome-shell/daemon.js` (`GPasteDaemonRunner`): it runs the daemon inside gnome-shell — `g_paste_daemon_new_meta (settings, global.display.get_selection ())`, plus a `GPasteBus`/`GPasteSearchProvider` — with none of the standalone daemon's GApplication, signal handling, re-exec or storage-migration dialog (its lifetime is the extension's). It is not yet wired into `extension.js`. The same `MetaSelection` is used for both the clipboard and the primary provider: there is one per display and the two constructors select the `MetaSelectionType` (`CLIPBOARD` vs `PRIMARY`) internally.

To define a GObject that implements an interface with private data, use `G_PASTE_DEFINE_TYPE_WITH_PRIVATE_AND_INTERFACE (TypeName, type_name, ParentType, IFACE_TYPE, iface_init)` (in `gpaste-macros.h`), as both clipboard implementations do.

The D-Bus method surface lives in `gpaste-daemon-methods.{c,h}`: free functions that take a small `GPasteDaemonMethods` context `{ connection, history, settings, clipboards_manager }` rather than the daemon's instance-private struct. `gpaste-daemon.c` keeps the object lifecycle, the signal emitters, the controller actions that need the GObject (`upload`, `reexecute`, `show_history`), and a dispatcher that builds the context and forwards. Everything but the `gpaste-daemon.c` entry point is compiled into the installed `libgpaste-daemon` shared library (exposed to in-tree consumers as `gpaste_daemon_internal_dep`), which both the `gpaste-daemon` executable and the test suite link.

### `src/client/` — `gpaste-client`

CLI tool for scripting and shell integration. Talks to the daemon via `GpasteClient`. Entry point for subcommands like `ui` (launches the GTK UI) and `daemon-reexec`.

### `src/ui/` — `gpaste-ui` (GTK4 + libadwaita)

The main graphical history browser. Launched via `gpaste-client ui`. Contains widgets for the history list, item actions, search, and settings panel. Uses libadwaita widgets extensively:

- `AdwApplicationWindow` (via `G_PASTE_GTK_INIT_APPLICATION` which uses `AdwApplication`)
- `AdwHeaderBar` with `AdwWindowTitle` (current history name shown as subtitle)
- `AdwShortcutsDialog` / `AdwShortcutsSection` / `AdwShortcutsItem` (keyboard shortcut help)
- `AdwToastOverlay` (wraps main content for future toast notifications)
- `AdwBanner` (shown on daemon connection failure)
- `AdwNavigationSplitView` (responsive split: history selector panel + history list)
- `AdwStatusPage` (shown when history is empty or search has no results)
- `AdwEntryRow` (in panel's "Switch to history" entry, inside a `boxed-list` GtkListBox)
- `AdwSidebar` / `AdwSidebarSection` / `AdwSidebarItem` (history selector list in the panel sidebar, libadwaita 1.9)
- `AdwAboutDialog` (about dialog)

**Subclassing notes:** GTK4 made many widget classes final (`G_DECLARE_FINAL_TYPE`), preventing subclassing. Libadwaita re-enables subclassing for its own derivable types. Within the ui, internal widgets subclass derivable types: `GtkBox` (for `GPasteUiPanel`, `GPasteUiHistory`), `AdwApplicationWindow` (for `GPasteUiWindow`), `AdwSidebarItem` (for `GPasteUiPanelHistory` — a GObject, not a widget), etc. `GtkStack` is final in GTK4 and cannot be subclassed — use `GtkBox` with manual visibility toggling instead.

**Image previews** in `GPasteUiItemSkeleton` rows are a small inline `GtkPicture` thumbnail (sized by `images-preview-size`, toggled by `images-preview`). Hovering it shows a larger detail preview via a custom `query-tooltip` (`gtk_tooltip_set_custom`), capped and aspect-preserved. The inline thumbnail is kept deliberately (glanceable, keyboard/touch-friendly); the tooltip only enlarges it on demand.

**Label widgets in list rows** use `GtkInscription` (not `GtkLabel`) for the main text display. `GtkInscription` is optimised for list-item cells and avoids overhead from markup/accessibility features not needed there. Set overflow explicitly with `gtk_inscription_set_text_overflow(GTK_INSCRIPTION_OVERFLOW_ELLIPSIZE_END)` — the default is `CLIP`. To display bold text, use `pango_parse_markup` to convert a markup string into `PangoAttrList` and pass it to `gtk_inscription_set_attributes`; call `gtk_inscription_set_attributes(NULL)` before `set_text` when switching back to plain text.

**AdwSidebar** (libadwaita 1.9) is used in `GPasteUiPanel` to list available histories. `GPasteUiPanelHistory` subclasses `AdwSidebarItem` (a `GObject`, not a widget) to represent each history entry. The right-click context menu (backup/empty/delete) is driven by a `GMenuModel` set on the sidebar, with `GSimpleAction`s installed on the panel widget under the `panel.` prefix.

**Lazy history loading** (`GPasteUiHistory`): the display count is computed at runtime to fill the window — there is no `max-displayed-history-size` setting. `priv->limit` is the count of items currently allowed on screen; `priv->size` (= number of allocated row widgets) is `MIN (available, limit)`. The batch size (`g_paste_ui_history_batch()`) is one viewport's worth of items — `page_size / measured-row-height + 1` — falling back to `G_PASTE_UI_HISTORY_DEFAULT_BATCH` until a row has been measured and the viewport allocated. The view grows `limit` by one batch on demand, driven by the scrolled window: the vertical adjustment's `changed` signal keeps loading batches while the content does not yet overflow the viewport (so the window always fills), and `GtkScrolledWindow::edge-reached` (`GTK_POS_BOTTOM`) loads the next batch each time the user scrolls to the bottom — lazily pulling in the whole history. A `priv->loading` flag guards against re-entrant growth while a refresh is in flight. When the history shrinks the now-unused row widgets are dropped and unref'd, and `limit` is clamped back down (`MIN (limit, MAX (batch, available))`) so lazy growth restarts from a single batch rather than eagerly reloading the old depth. There is no libadwaita lazy-scroll helper; `edge-reached` is the idiomatic GTK primitive for the bottom trigger (preferred over hand-computing the adjustment's value/page/upper).

**Merge selection mode** (`GPasteUiHistory` + `GPasteUiHeader` + `GPasteUiWindow`): the header's "merge" button puts the list into a multi-selection mode to merge several entries into one. `g_paste_ui_history_set_selection_mode()` flips the `GtkListBox` between `GTK_SELECTION_NONE` and `GTK_SELECTION_MULTIPLE` and toggles every row's `selectable`/`activatable` (so clicks select instead of pasting); rows loaded lazily while in the mode get the same treatment at creation. A capture-phase `GtkGestureClick` on the list box implements click-to-toggle (plain `GtkListBox` multi-select would need Ctrl/Shift), claiming the press and calling `select`/`unselect_row`. The native row highlight is the only selection cue. Selection *order* is tracked in a `GPtrArray` of uuids (appended/removed as rows toggle) so the merge keeps the order the user picked them, not the list order. The list emits `selection-changed(count)`; the window updates the header count (`g_paste_ui_header_set_selection_count`) and the "Merge" button's sensitivity (needs ≥2). The header swaps to a selection look (`.selection-mode` style class, the merge button replaced by a cancel button, the title showing "N selected"). The merge controls live in a bottom `GtkActionBar` (`adw_toolbar_view_add_bottom_bar`) revealed with the mode; "Merge" is a `GtkMenuButton` whose popover offers separator presets plus a custom-separator entry — choosing one collects the ordered uuids (`g_paste_ui_history_get_selected_uuids`) and calls `g_paste_client_merge()` (empty decoration), then leaves the mode. Escape and the cancel button also leave it.

### `src/preferences/` — GTK4 + Adwaita preferences application

Standalone preferences window. Uses GTK4 and libadwaita for a modern look.

### `src/gnome-shell/` — GNOME Shell extension (JavaScript)

Native shell integration. Provides the panel indicator and quick-access popover. Communicates with the daemon via D-Bus. Lives under the standard GNOME Shell extension layout with `extension.js` as entry point.

**History display** (`indicator.js`): the popover history is an `St.ScrollView` (its `max-height` capped at 60% of the monitor work area) wrapping a `PopupMenuSection`, filled lazily. The batch size is computed at runtime to fill the viewport — `_fillBatch()` returns `page_size / average-row-height + 1` (the average derived from the laid-out content, `upper / loaded`), falling back to `_DEFAULT_BATCH` before anything is laid out. `_loadMore()` appends a batch of `GPasteItem` rows, and the scroll view's `vadjustment` drives growth — `changed` keeps appending while the content does not yet overflow the viewport, `notify::value` appends the next batch when the user scrolls to the bottom — so the whole history is reachable by scrolling (no page switcher). St does not recycle actors, so only the scrolled-to depth is ever materialised; `_reloadGeneration` guards against overlapping async reloads, and rows are `destroy()`ed on reload. (We deliberately do *not* use `PopupSubMenu`: it is the same `St.ScrollView` primitive but as a click-to-expand "More…" submenu whose scroll is tied to the top menu's max-height, and it holds every item with no virtualisation — so it would force an expander UX and lose the lazy loading.) Because the section is nested in the scroll view (not the menu's item tree), each row's `activate` is wired to `menu.itemActivated()` by hand to close the menu.

### `data/`

Non-code resources: D-Bus service files (`dbus/`), `.desktop` entries, GSettings schemas (`gsettings/`), systemd user units, AppStream metadata.

### `po/`

Translations managed via Weblate. Add new strings to the relevant `.c` source with `_()` / `N_()` and update `po/POTFILES.in` if adding a new file.

## Key dependencies

- GLib/GObject/Gio ≥ 2.84
- GTK4 ≥ 4.18 + libadwaita ≥ 1.9 (UI and preferences; 1.9 required for `AdwSidebar`)
- GCR (`gcr-4`) ≥ 3.90 (password item storage; also the secure-memory allocator for encryption secrets)
- gjs ≥ 1.78 (GNOME Shell extension runtime)
- gtk4-x11 (the daemon forces the GDK x11 backend at startup)
- libsodium (optional; gated by the `encryption` meson feature, `auto` by default — history-encryption converter)
- libsecret (optional; gated by the `libsecret` meson feature, `auto` by default, and only used together with encryption — remembers the history passphrase in the keyring)

Image items use `GdkTexture` from GTK4 directly — there is no longer a GdkPixbuf dependency.

On Fedora: `dnf install meson ninja-build glib2-devel gtk4-devel gcr-devel libadwaita-devel gjs-devel`
