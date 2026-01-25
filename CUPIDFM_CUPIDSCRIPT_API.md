# CupidFM Cupidscript API

CupidFM can load Cupidscript plugins (`.cs`) at startup and expose a small `fm.*` API so scripts can interact with the file manager.

This document describes the currently-implemented API surface in this repository.

## Plugin Locations

CupidFM loads `*.cs` plugins from these folders (in this order):

1. `~/.cupidfm/plugins`
2. `~/.cupidfm/plugin` (legacy)
3. `./cupidfm/plugins`
4. `./cupidfm/plugin` (legacy)
5. `./plugins`

## Plugin Hooks

Plugins are regular Cupidscript files. CupidFM calls:

- `fn on_load()` (optional) once after the plugin file is executed.
- `fn on_key(key)` (optional) on every keypress.
  - Return `true` to consume the keypress (CupidFM will not handle it).
  - Return `false` to let CupidFM handle it normally.

You can also bind specific keys to a function using `fm.bind(...)` (see below).

## Key Format

CupidFM passes keys to plugins as **strings**, such as:

- `"^T"` for Ctrl+T
- `"F5"` for function keys
- `"KEY_UP"`, `"KEY_DOWN"`, `"KEY_LEFT"`, `"KEY_RIGHT"`
- `"Tab"`
- `"a"` for printable characters

If you need a numeric keycode, use `fm.key_code(name)`.

## fm.* API

### UI

- `fm.notify(msg)`
- `fm.status(msg)`
  - Show a message in the bottom notification bar.
  - `fm.status` is an alias of `fm.notify`.

- `fm.popup(title, msg)`
  - Show a blocking popup window with a title and message.

### Context

- `fm.cwd() -> string`
  - Current directory path (the directory panel).

- `fm.selected_name() -> string`
  - Name of the currently selected entry (file/dir) in the directory panel, or `""` if none.

- `fm.selected_path() -> string`
  - Full path to the currently selected entry (cwd + selected_name), or `""` if none.

### Key Binding / Events

- `fm.bind(key, func_name) -> bool`
  - Register a function to be called when `key` is pressed.
  - `key` can be:
    - a string like `"^T"`, `"F5"`, `"KEY_UP"`, `"Tab"`, `"a"`, etc.
    - or an integer numeric keycode
  - `func_name` must be the name of a function defined in your plugin.
  - The function will be called as `fn your_func(key) -> bool`, where `key` is the key string.
  - Return `true` to consume the keypress.

- `fm.key_name(code:int) -> string`
  - Convert a numeric keycode into the string name CupidFM uses.

- `fm.key_code(name:string) -> int`
  - Convert a key name (like `"^T"` or `"F5"`) into a numeric keycode.
  - Returns `-1` if unknown.

### Control

- `fm.reload()`
  - Ask CupidFM to reload the directory listing (like refreshing the directory panel).

- `fm.exit()`
  - Ask CupidFM to exit.

## Minimal Example Plugin

Save this as `~/.cupidfm/plugins/example.cs`:

```cs
fn on_load() {
  fm.notify("example plugin loaded");
  fm.bind("^T", "on_ctrl_t");
}

fn on_ctrl_t(key) {
  fm.popup("Example", "Selected: " + fm.selected_path());
  return true;
}

fn on_key(key) {
  return false;
}
```

## Notes / Limitations

- Plugins run in-process inside CupidFM via the Cupidscript VM.
- The API is intentionally small right now; it can be extended with more `fm.*` functions over time.
- If a plugin function errors, CupidFM will show the VM error in the notification bar (and then clear it).

