# AGENTS.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

GPaste uses Meson + Ninja:

```sh
meson setup build
ninja -C build
```

The build directory is `build`, not `builddir`.

Build options (`meson setup build -Doption=value`, or `meson configure build`).
The `feature` ones default to `auto`: enabled when the dependency is found,
skipped otherwise, so a build silently loses a feature if its dependency is
missing — pass `enabled` to make that fatal.

| Option | Type | Default | Description |
|---|---|---|---|
| `encryption` | feature | auto | libsodium-based history encryption (`G_PASTE_ENABLE_ENCRYPTION`) |
| `sqlite` | feature | auto | SQLite storage backends, needs SQLite ≥ 3.35 (`G_PASTE_ENABLE_SQLITE`) |
| `libsecret` | feature | auto | passphrase in the keyring, only with `encryption` (`G_PASTE_ENABLE_LIBSECRET`) |
| `pwquality` | feature | auto | password strength rating: the passphrase of an encrypted history, and a new password item (`G_PASTE_ENABLE_PWQUALITY`) |
| `gnome-shell` | bool | true | GNOME Shell extension + mutter clipboard backend (`G_PASTE_ENABLE_GNOME_SHELL`) |
| `introspection` | bool | true | Generate GIR data |
| `vapi` | bool | true | Generate Vala bindings (errors out without `introspection`) |
| `systemd` | bool | true | systemd user unit |
| `bash-completion`, `zsh-completion`, `fish-completion` | bool | true | Shell completions |

Four string options — `dbus-services-dir`, `dbus-interfaces-dir`,
`control-center-keybindings-dir` and `systemd-user-unit-dir` — override install
paths that are otherwise queried from `dbus-1` (the first two),
`gnome-keybindings` and `systemd` via pkg-config. Setting them is how a packager
avoids pulling those in as build dependencies; they are still required at
runtime.

For a lighter build that skips the GNOME Shell extension, GIR introspection data
and Vala bindings (the daemon, UI and preferences apps are always built):

```sh
meson setup build -Dgnome-shell=false -Dintrospection=false -Dvapi=false
```

Run the tests:

```sh
ninja -C build test          # or: meson test -C build
```

The test infrastructure and what each suite covers are documented in [`tests/AGENTS.md`](tests/AGENTS.md).

## Code style

- C standard: GNU17
- Formatting: ClangFormat (see `.clang-format`). Key rules: Allman braces, 4-space indent, no column limit, space before parens, no tabs.
- clang-format is not yet enforced; do not run it automatically.
- **Braces**: Remove braces from `if`/`else if`/`else` branches whose body is a single statement on a single line. Keep braces when the body has multiple statements OR spans multiple lines. A **single call wrapped over two lines keeps its braces** — the `preferred_trigger` add in `build_shortcuts_variant()` is that case; a nested if-else chain is the other. Multi-statement macros that need to appear as a single statement must use the `do { ... } while (0)` idiom — `SWITCH_STATE` in `gpaste-file-backend.c` does this and can safely appear without surrounding braces.
- **Alignment**: a parameter list that wraps has every continuation line starting at the column of the opening parenthesis, and the parameter names line up in one column, `PointerAlignment: Right` putting the stars against the name. That column is the largest `type` + `*`s + 1 over the parameters, taken per parameter — `GError **error` needs `6 + 2 + 1`, `GPasteClient *self` needs `12 + 1 + 1`, so the wider of the two sets it. A run of struct members aligns the same way, blank lines and comments included in the run (the `parent_instance` line stands on its own). A header may share one column across a whole block of declarations, so match the block rather than tightening one signature out of it.
- A rename that shortens or lengthens a type silently leaves these behind, so **re-align the whole signature or run, not just the line you touched** — and re-read the comments you touched against the rule below. Both are part of finishing an edit, not a separate pass.
- **Comments say what the code does**, not what it once did nor why it was changed: no "used to", no "no longer", no "instead of the old", no commit hashes. The domain has a past of its own — an old file format, a stale passphrase, an item the history no longer holds — and that is fair game; the code's does not. Rationale is welcome, and most of this tree's comments are rationale, but it has to be a rationale for what is there now.
- **A comment earns its lines.** Rationale, an invariant, an ownership rule, a trap the next reader would fall into: those pay. Restating the call below it, or the ownership its `g_autoptr` already declares, does not. Say the same thing once — a second site that needs it points at the first ("As in `on_unlock_reply()`: …") rather than repeating it, so the two cannot drift apart.
- **Re-read every comment a change adds before its commit lands.** `git diff --cached -U0 | grep -E '^\+\s*(/\*|\*|//)'` lists them; each has to pass the two rules above on its own. Writing them is when the change is freshest in mind, which is exactly when narrating it reads as natural prose — "rather than the old", "which is what retires", "the branch X left behind", "what just changed". Only a re-read from cold catches those, so it is a step of its own, before the commit rather than a follow-up to it. Amend when one slips through.

### GObject conventions

- **No `const` on instance pointers.** Methods take `GPasteFoo *self`, never `const GPasteFoo *`, the same way GLib and GTK do — a getter is free to populate a cache, take a lock or notify. The `G_PASTE_CONST_*` shims that used to launder a `gconstpointer` through the type macros are gone; use the stock `G_PASTE_IS_FOO`, `G_PASTE_FOO`, `G_PASTE_FOO_GET_CLASS` and `g_paste_foo_get_instance_private`. `const` still belongs on plain structs and enums (`GPasteDaemonMethods`, `GPasteClipboardContent`, `GPasteStorage`, the `*Private` and `*Class` structs) where it is ordinary C const-correctness. A `GCompareFunc` or similar that hands you a `gconstpointer` casts it at the call site, as GLib's own comparison functions do.
- **State is a property, an occurrence is a signal.** Something the object *is* (`GPasteScreensaverClient:active`, `GPasteUiHistory:selection-mode`) is a property with `notify::`; something that *happens* (`name-lost`, `reexecute-self`, `keybinding-activated`, `GPasteHistory::update`) stays a signal. Install properties with the `g_object_class_install_properties` array form and `G_PARAM_EXPLICIT_NOTIFY`, so the setter decides whether anything actually moved. Do not add a property that merely restates a signal already carrying the same value in-band.
- **Never emit under `G_PASTE_LOCK_HISTORY`.** Handlers call back into `GPasteHistory`, and the lock is not recursive — this is why the `selected` emission is deferred until the lock is released. The same applies to `g_object_notify*`.

### Documenting errors

GIR cannot express *which* domain a function throws: the format only has a boolean `throws="1"` per callable, plus `glib:error-domain` on the enumeration (which `GPasteError` carries). A `@error:` parameter line does not survive into the GIR at all — introspection drops the throws parameter — so anything a binding consumer needs to know has to be in the function's *description*, not on its `@error:` line.

- The `@error:` line is always exactly `@error: return location for a #GError, or %NULL`. It carries no annotations: `(nullable)` on a throws parameter is discarded along with the parameter.
- State the domain in the description of any function that throws, naming the concrete domains (`%G_PASTE_ERROR`, `%G_DBUS_ERROR`, `%G_IO_ERROR`, …). Where a whole class shares one error story, the class doc carries it instead of repeating it on every method — `GPasteClient` does this for its D-Bus methods, and only its constructors, which never reach the daemon, restate theirs.
- Say so explicitly when a failure is *not* an error: `g_paste_prompt_passphrase_finish()` and `g_paste_prompt_migration_finish()` report a dismissal through the return value with `@error` left unset.
- The name in a doc block's first line must match the symbol exactly. A typo silently detaches the whole block — the function then reaches the GIR with no documentation at all, with nothing to warn you.

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
- Replace `g_free(old); ptr = g_strdup(new)` with `g_set_str(&ptr, new)` — but **only when `new` is borrowed**. `g_set_str` does an internal `g_strdup`, so if `new` is a freshly-allocated string you already own (e.g. from `g_strdup_printf`, `g_strconcat`, `g_settings_get_string`), that dup is wasted.
- When the new value is already owned, use `g_set_str_take(&ptr, new)` (GLib 2.90), which frees the old value and consumes the new one — never `g_free(ptr); ptr = new`, which silently leaks if the assignment is ever reached without the free. Same split for string arrays: `g_set_strv` when borrowed, `g_set_strv_take` when owned.
- A setter whose callers always pass a freshly-built string should take ownership of it (`gchar *` arg, annotated `(transfer full)`) and store it with `g_set_str_take` rather than dup it — this is how `g_paste_item_set_display_string` and `g_paste_clipboard_content_set_text_take` work, and callers hand off with `g_steal_pointer(&local)`. Where both shapes have real callers, provide the copying setter and a `_take` variant beside it.
- Use `g_steal_pointer(&ptr)` when transferring ownership out of an auto-managed variable — including when passing a `g_autofree`/`g_autoptr` local into a container that takes ownership (e.g. `g_ptr_array_add (arr, g_steal_pointer (&local))`).
- In `dispose`: use `g_clear_object`, `g_clear_pointer`, `g_clear_list`, `g_clear_slist` — they null the pointer, making double-dispose safe. Release all reference-counted objects here (GObject, GBytes, GPtrArray, GHashTable, …), not in `finalize`.
- In `finalize`: use `g_free` for plain heap allocations (`gchar *`, plain structs). Do **not** call `g_object_unref` or other ref-counting unrefs here — those belong in `dispose`.
- When a type needs to release GObject refs but currently only has `finalize`, add a `dispose` function and register it with `object_class->dispose`.
- A value owned for the length of one loop iteration is declared **inside** the loop, where its cleanup macro frees it at the end of that iteration. Declared around the loop and freed by hand at the bottom of the body (or with `g_clear_pointer`, which only nulls what the next iteration overwrites anyway), the cleanup it declared never fires and the free is back to being one an early `break` can forget.
- A plain allocation belongs in `dispose` too, when a disposed object has to stop *answering* for it: `GPasteClipboardMetaSource` drops its mimetype list there — nothing in that list needs an unref, but it is what the tables released beside it serve reads from, and a disposed source still naming its formats would be offering reads it can only refuse. The bullets above say where each kind of thing goes by default, not where it must go regardless; say which, and why, in a comment.
- The mirror of that: an owned list belongs in `finalize` when something can hold a ref across `dispose` and still read through it. `GPasteClipboardsManager` frees its `_Clipboard` records there because a clipboard update in flight refs the manager precisely so the record its reply reads is still standing.
- A plain struct with a free function of its own gets `G_DEFINE_AUTOPTR_CLEANUP_FUNC (Type, type_free)` beside that function, which is what makes `g_autoptr(Type)` and `g_autoslist(Type)` work for something that is not a GObject (`DeferredAction` in `gpaste-ui-window.c`). A list of such structs freed by hand is the same omission as a list of GObjects freed by hand.
- Do **not** use `g_autolist`/`g_autoslist` on lists that do not own their elements. `gdk_file_list_get_files` is **`(transfer container)`** — it returns a fresh `g_slist_copy()` whose `GFile`s stay owned by the `GdkFileList`, so its result must be freed with `g_autoptr(GSList)` (never leaked, never `g_autoslist`'d). `g_hash_table_get_values` is the same shape — use `g_autoptr(GList)`.
- Do **not** use auto-cleanup on a variable whose ownership is intentionally transferred — use `g_steal_pointer` to make the handoff explicit.
- Do **not** use `if (ptr) g_slist_free_full (g_steal_pointer (&ptr), fn)` in dispose — use `g_clear_slist (&ptr, fn)` instead (handles NULL safely).
- When storing a `g_settings_get_string` value (owned, `(transfer full)`) into a `gchar *` field, free the old value and take ownership directly: `g_free(field); field = g_settings_get_string(...)`. Do **not** assign without freeing first (leak on re-assignment), and do **not** route it through `g_set_str` (that would re-`g_strdup` an already-owned string — a wasted allocation).
- For a stored `GSource`/timeout/idle/signal handler id, prefer `g_clear_handle_id (&id, remove_func)` over the hand-rolled `if (id) { remove_func (id); id = 0; }` — it is NULL-safe and nulls the field in one call (e.g. `g_clear_handle_id (&priv->retry_source, g_source_remove)`). Use this in new code and when touching such cleanup.
- A `GCancellable` field standing for an operation that may be superseded is given up on with `g_paste_clear_cancellable (&field)` (`gpaste-macros.h`), not a hand-rolled `g_cancellable_cancel` + `g_clear_object` pair: GLib has `g_set_object` and friends but nothing cancellable-aware, and a plain set would drop the reference without cancelling, leaving the operation it named running — which is the whole of what the field is for. It only clears; the successor, where there is one, is allocated by the caller at the point the new operation goes out, so a caller with none to issue (a `dispose ()`, a request answered without a round trip) is not left holding a cancellable that stands for a reply which is never coming. Both halves are NULL-safe, so a slot that may or may not have something out needs no guard.
- A stack `GVariantBuilder` is declared and initialised in one statement: `g_auto (GVariantBuilder) builder = G_VARIANT_BUILDER_INIT (<type>)`. The type is not copied there, so it has to be one that outlives the builder — every builder in this tree names a literal (`G_VARIANT_TYPE_VARDICT`, `G_VARIANT_TYPE ("aa{sv}")`, `G_PASTE_ITEMS_VARIANT_TYPE`), which is. A `g_auto` declaration left bare until a later `g_variant_builder_init` is a builder whose cleanup would run over whatever the stack held, should anything return in between; for the case where the type really is not known until later, that is what `G_VARIANT_BUILDER_INIT_UNSET ()` and `g_variant_builder_init_static ()` are for.
- A `GSource` whose callback needs a GObject the source does not otherwise hold takes a `g_paste_weak_ref_new (object)` as its data, with `g_paste_weak_ref_free` as its destroy notify (`g_timeout_add_full`, not `g_timeout_add`), and the callback opens it with `g_autoptr (Type) self = g_weak_ref_get (user_data)`. A reference of its own would put the one thing that cancels the source — that object's `dispose()` — out of reach, since `dispose()` only runs once the last reference is dropped; a raw pointer borrows its safety from that same teardown path instead of stating it.
- The same holds for **anything an object tracks so that its own `dispose()` can end it** — an async request in a list, a pending operation in a table — not only for a `GSource`: it holds the object with an embedded `GWeakRef` (`g_weak_ref_init` in the constructor, `g_weak_ref_clear` in the free function, `g_weak_ref_get` at the top of every callback), and anything it carries that would take a reference of its own goes the same way — a `GTask` created for it passes **no source object**, since `g_task_new()` references the one it is given. A strong reference anywhere in that loop is one the object holds on itself, and the teardown it is waiting for can then never run.
- Build NULL-terminated string vectors with `GStrvBuilder` (`g_strv_builder_add` for borrowed strings, `g_strv_builder_take` for owned ones, `g_strv_builder_end` to get the `GStrv`) rather than hand-managing a `GPtrArray` + manual `NULL` terminator + `(GStrv) ->pdata` cast. It also avoids the deep `g_strdupv` copy that returning a `GPtrArray`'s contents requires.
- Prefer `g_signal_connect_object (source, sig, cb, gobject, 0)` over plain `g_signal_connect` when the handler's data is a **GObject** shorter-lived than the signal source and you would otherwise not disconnect — it auto-disconnects when that object is finalized. Do **not** convert sites that pass non-GObject data (a `priv`/struct pointer), pass `NULL` data, or that deliberately disconnect early in `dispose` for ordering reasons (where `g_signal_connect_object` would only disconnect at finalize and regress that ordering).

## Single sources of truth

Four lists in this tree used to exist in several copies and drift apart. Each is
now written down once; add to the list, not to its consumers.

| List | Lives in | Drives |
|---|---|---|
| The keyboard shortcuts | `G_PASTE_FOR_EACH_KEYBINDING` in `gpaste-3/gpaste-keybindings.h` | the shortcuts dialog, the preferences' shortcuts page, `data/control-center/42-gpaste.xml` (generated at build time by `tools/gen-keybindings-xml.py`), and the daemon's own bindings. Each carries two names: the sentence the shortcut UIs show and the few words the desktop portal lists it under |
| The command line's verbs | `commands[]` in `src/client/gpaste-client.c` | dispatch, and `show_help()`, printed straight from it |
| A `GPasteItem`'s kind | `GPasteItemKind` in `gpaste-3/gpaste-item-enums.h` | the item classes and both storage backends, through `g_paste_item_kind_to_string()` / `_from_string()`. The wire carries the enum *value*, not its nick, so the nicks are the on-disk format's business alone |
| The prompts' wording | `GPastePromptText` in `gpaste-daemon/gpaste-prompt.h` | both passphrase/migration dialogs -- the Adwaita one out of process and the St one in the shell |
| The D-Bus methods | `data/dbus/org.gnome.GPaste3.xml` | both halves of the wire, via gdbus-codegen |

**The window's own shortcuts are not in that table.** `G_PASTE_FOR_EACH_KEYBINDING` is the single source for the *global* keybindings because three consumers have to agree on them. The graphical tool's own — search, new item, paste-by-index, preferences, close — are configurable by nobody, work only while the window has the focus, and have no business in the desktop portal's list, so they live in a small table in `src/ui/gpaste-ui-shortcuts-window.c`, beside the window that answers them. Do not merge the two.

**The prompts are built twice, but say one thing.** St and Adwaita share no
widget vocabulary, so `src/gnome-shell/prompt.js` and `src/daemon/gpaste-prompt-adw.c`
each build their own dialog -- but every *decision* (`can_import`,
`backend_changes`, `passphrase_is_complete`, `remember_state`,
`passphrase_strength`) and now every *word* (`g_paste_prompt_text()`,
`g_paste_prompt_storage_label()`) comes from the C side. Add a string there, not
in a dialog.

**A history is identified by its name.** Every `GPasteStorageBackendClass` vfunc
takes one; a backend derives its own path with
`g_paste_storage_backend_get_history_file_path()` rather than being handed one,
so nothing has to reverse the mapping to get the name back.

The shell completions and the man page are the exception: each verb completes
its arguments differently and the man page says more than a table could carry,
so they stay hand-written and `tests/completions` checks them against
`commands[]` instead — every verb and alias has to be offered by all three
shells and every canonical verb documented in the man page.

An enumeration exposed to a binding needs a GType: `GPasteItemKind`,
`GPasteStorage` and `GPasteUpdateAction` each register one, which is what lets a
setting like `storage-backend` be a `g_param_spec_enum` rather than a widened
integer.

## Maintenance rules

- A public library function is exported by marking its definition `G_PASTE_VISIBLE` (the libraries build with `gnu_symbol_visibility: 'hidden'`, so anything unmarked stays internal). There are no `.sym` version scripts to maintain; the `G_PASTE_*_TYPE` macros already mark their generated `get_type`. Conditionally-compiled (feature-gated) symbols just need the marker — they export only in configurations where they are compiled.
- **`G_PASTE_VISIBLE` belongs to the libraries only.** `src/ui/`, `src/client/`, `src/daemon/` and `src/preferences/` build executables, which export nothing; their headers are reached through their own directory and carry no umbrella guard either.
- When updating source files in this repository, keep the `AGENTS.md` covering them up to date — this one, or the one beside the code (see Architecture) — to reflect any new patterns, rules, or architectural decisions introduced. An `AGENTS.md` says what exists, where it lives and the invariants spanning several files; the reason behind one function belongs in the comment beside it, and what a D-Bus method does in `data/dbus/org.gnome.GPaste3.xml`. Point at those rather than repeating them, so the two cannot drift apart.

**Changelog entries are short and only say what a user sees.** This covers `NEWS`, the `<release>` notes and the release blog post alike. One line per change, a dozen words or so, in the imperative ("Add …", "Fix …"), features first and fixes last, several related changes grouped into one line that says what they add up to. Describe the effect, never the mechanism: no functions, signals, locks, reads or races, and no reasoning about why the old behaviour happened — that belongs in the commit message. Internal work (refactors, tests, library API and ABI, soversions, code cleanups) gets no line; a D-Bus interface change gets one only for the third-party clients it breaks. `NEWS` alone keeps a single `Build:` line for new dependency requirements; the `<release>` notes, which software centres show, leave it out.

## Architecture

GPaste is a GNOME clipboard manager split across several binaries and a shared library.

Each part documents itself in an `AGENTS.md` beside its code. Read the one for the directory you are working in before starting, and keep it up to date in the same change:

| Directory | What | Documented in |
|---|---|---|
| `src/libgpaste/` | libgpaste, libgpaste-gtk4, and the D-Bus interface both halves of the wire follow | [`src/libgpaste/AGENTS.md`](src/libgpaste/AGENTS.md) |
| `src/libgpaste/gpaste-daemon/`, `src/daemon/` | libgpaste-daemon and the `gpaste-daemon` executable | [`src/libgpaste/gpaste-daemon/AGENTS.md`](src/libgpaste/gpaste-daemon/AGENTS.md) |
| `src/client/` | `gpaste-client` | below |
| `src/ui/` | `gpaste-ui` | [`src/ui/AGENTS.md`](src/ui/AGENTS.md) |
| `src/preferences/` | `gpaste-preferences` | [`src/preferences/AGENTS.md`](src/preferences/AGENTS.md) |
| `src/gnome-shell/` | the GNOME Shell extension, and its JavaScript conventions | [`src/gnome-shell/AGENTS.md`](src/gnome-shell/AGENTS.md) |
| `data/` | D-Bus, desktop, GSettings, systemd, AppStream, shell completions | [`data/AGENTS.md`](data/AGENTS.md) |
| `po/` | translations | [`po/AGENTS.md`](po/AGENTS.md) |
| `tests/` | the test environment and suites | [`tests/AGENTS.md`](tests/AGENTS.md) |

### `src/client/` — `gpaste-client`

CLI tool for scripting and shell integration. Talks to the daemon via `GpasteClient`. Entry point for subcommands like `ui` (launches the GTK UI), `daemon-reexec` and `migrate` (resets the backend revision and re-execs the daemon to run an on-demand storage migration).

## Key dependencies

- GLib/GObject/Gio ≥ 2.90 (declared as `2.89.0` in `meson.build` so it configures against the development releases)
- GTK4 ≥ 4.24 + libadwaita ≥ 1.10 (UI and preferences; GTK4 declared as `4.23.4` for the same reason)
- GCR (`gcr-4`) ≥ 4.0 (password item storage; also the secure-memory allocator for encryption secrets)
- gjs ≥ 1.78 (GNOME Shell extension runtime)
- gtk4-x11 (the daemon forces the GDK x11 backend at startup)
- libsodium (optional; gated by the `encryption` meson feature, `auto` by default — history-encryption converter)
- libsecret (optional; gated by the `libsecret` meson feature, `auto` by default, and only used together with encryption — remembers the history passphrase in the keyring)
- libpwquality (optional; gated by the `pwquality` meson feature, `auto` by default — rates a password on `libgpaste`'s 0-4 scale, for the new-encrypted-history prompt's passphrase and the graphical tool's new-password composer alike)
- sqlite3 ≥ 3.35 (optional; gated by the `sqlite` meson feature, `auto` by default — the SQLite history storage backend; 3.35 for `RETURNING` and `UPDATE … FROM`)

`meson.build` derives `GLIB_VERSION_MIN_REQUIRED`/`MAX_ALLOWED`, `GDK_VERSION_*` and `ADW_VERSION_*` from those requirements, pinning min and max to the same value so both newer-than-required and newly-deprecated API warn. GLib and GTK only define macros for their stable (even) minors, so an odd minor is rounded up; libadwaita releases every minor as stable and defines a macro for each, so its derivation must **not** be rounded.

Image items use `GdkTexture` from GTK4 directly — there is no longer a GdkPixbuf dependency.

On Fedora: `dnf install meson ninja-build glib2-devel gtk4-devel gcr-devel libadwaita-devel gjs-devel`
