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

To disable the GTK UI components for a minimal daemon-only build:

```sh
meson .. -Dgnome-shell=false -Dintrospection=false -Dvapi=false
```

Run tests from the build directory:

```sh
ninja test
```

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
- Prefer `g_signal_connect_object (source, sig, cb, gobject, 0)` over plain `g_signal_connect` when the handler's data is a **GObject** shorter-lived than the signal source and you would otherwise not disconnect — it auto-disconnects when that object is finalized. Do **not** convert sites that pass non-GObject data (a `priv`/struct pointer), pass `NULL` data, or that deliberately disconnect early in `dispose` for ordering reasons (e.g. `GPasteInternalKeybindingProvider` disconnects its `GdkDisplay::xevent` handler before freeing the state that handler uses — `g_signal_connect_object` would only disconnect at finalize and regress that).

## Maintenance rules

- When adding or removing a public function from any library under `src/libgpaste/`, update the corresponding `.sym` file (`libgpaste.sym` or `libgpaste-gtk4.sym`).
- When updating source files in this repository, keep `CLAUDE.md` up to date to reflect any new patterns, rules, or architectural decisions introduced.

## Architecture

GPaste is a GNOME clipboard manager split across several binaries and a shared library.

### `src/libgpaste/` — shared library

The core library used by all other components. Two sub-modules:

- `gpaste/` — daemon-agnostic types: `GpasteClient` (D-Bus client), `GpasteSettings` (GSettings wrapper), `GPasteGnomeShellClient` (GNOME Shell keybinding D-Bus proxy), `GPasteGlobalShortcutClient` (XDG GlobalShortcuts portal proxy), enums, utilities.
- `gpaste-gtk4/` — GTK4 + Adwaita UI helpers (used by the preferences app).

The library exposes a versioned ABI (symbol version scripts in `src/libgpaste/`). GIR and Vala bindings are generated from it.

**`GPasteKeybindingProvider`** is a GObject interface (`G_DECLARE_INTERFACE`) that abstracts keybinding grabbing. It declares `grab_all(accels[])` / `ungrab_all()` vtable methods and a `keybinding-activated(const gchar *id)` signal. Implemented by `GPasteGnomeShellClient`, `GPasteGlobalShortcutClient`, and the daemon-internal `GPasteInternalKeybindingProvider`. The `GPasteKeybindingAccelerator` struct `{ const gchar *id; const gchar *accelerator; }` is the transfer type between keybinder and provider; arrays are null-terminated by `.id = NULL`.

**`GPasteGlobalShortcutClient`** wraps the XDG GlobalShortcuts portal (`org.freedesktop.portal.Desktop`). It implements `GPasteKeybindingProvider`; the portal session is created lazily on the first `grab_all` call. The public API is limited to constructors and the GObject type — all shortcut registration goes through the provider interface. Internally it stores registered shortcuts as a `GPtrArray` of private `_Shortcut` structs and handles the portal's Request/Response async pattern transparently.

**`GPasteGnomeShellClient`** implements `GPasteKeybindingProvider` using GNOME Shell's `GrabAccelerators` D-Bus API. It stores a `GHashTable` mapping shortcut id → GNOME Shell action id, retries on `G_DBUS_ERROR_UNKNOWN_METHOD` (up to 10 times), and watches the shell bus name to re-grab on shell restart.

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
- History persistence to disk

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

**Label widgets in list rows** use `GtkInscription` (not `GtkLabel`) for the main text display. `GtkInscription` is optimised for list-item cells and avoids overhead from markup/accessibility features not needed there. Set overflow explicitly with `gtk_inscription_set_text_overflow(GTK_INSCRIPTION_OVERFLOW_ELLIPSIZE_END)` — the default is `CLIP`. To display bold text, use `pango_parse_markup` to convert a markup string into `PangoAttrList` and pass it to `gtk_inscription_set_attributes`; call `gtk_inscription_set_attributes(NULL)` before `set_text` when switching back to plain text.

**AdwSidebar** (libadwaita 1.9) is used in `GPasteUiPanel` to list available histories. `GPasteUiPanelHistory` subclasses `AdwSidebarItem` (a `GObject`, not a widget) to represent each history entry. The right-click context menu (backup/empty/delete) is driven by a `GMenuModel` set on the sidebar, with `GSimpleAction`s installed on the panel widget under the `panel.` prefix.

### `src/preferences/` — GTK4 + Adwaita preferences application

Standalone preferences window. Uses GTK4 and libadwaita for a modern look.

### `src/gnome-shell/` — GNOME Shell extension (JavaScript)

Native shell integration. Provides the panel indicator and quick-access popover. Communicates with the daemon via D-Bus. Lives under the standard GNOME Shell extension layout with `extension.js` as entry point.

### `data/`

Non-code resources: D-Bus service files (`dbus/`), `.desktop` entries, GSettings schemas (`gsettings/`), systemd user units, AppStream metadata.

### `po/`

Translations managed via Weblate. Add new strings to the relevant `.c` source with `_()` / `N_()` and update `po/POTFILES.in` if adding a new file.

## Key dependencies

- GLib/GObject/Gio ≥ 2.84
- GTK4 ≥ 4.12 + libadwaita ≥ 1.9 (UI and preferences; 1.9 required for `AdwSidebar`)
- GCR (`gcr-4`) ≥ 3.90 (password item storage)
- gjs ≥ 1.78 (GNOME Shell extension runtime)
- Optional: libX11 + libXI (x-keybinder)

Image items use `GdkTexture` from GTK4 directly — there is no longer a GdkPixbuf dependency.

On Fedora: `dnf install meson ninja-build glib2-devel gtk4-devel gcr-devel libadwaita-devel gjs-devel`
