# Modules (load/require) & Paths

## Table of Contents

- [Current Directory Stack](#current-directory-stack)
- [`load(path)`](#loadpath)
- [`require(path)`](#requirepath)
- [`require_optional(path)`](#require_optionalpath)
- [Path Helpers](#path-helpers)

CupidScript implements basic file execution helpers in the stdlib.

## Current Directory Stack

The VM tracks a directory stack. When you call `load("rel.cs")` or `require("rel.cs")`:

* If the path is absolute, it's used directly.
* If the path is relative, it's joined with the VM's current directory.

This is used so scripts can load other scripts relative to themselves.

You can inspect or change the current directory from script code:

* `cwd()` returns the VM's current directory (string)
* `chdir(path)` updates the current directory used for resolving relative paths

## `load(path)`

Runs a file immediately (every time you call it).

```c
load("util.cs");
load("util.cs"); // runs again
```

## `require(path)`

Runs a file only once per resolved path.

```c
let util = require("util.cs");
let util2 = require("util.cs"); // cached, returns same exports
```

`require()` returns the module's `exports` map.

Inside a required file, the VM provides:

* `exports` (map) - values to export
* `__file__` (string) - resolved module path
* `__dir__` (string) - directory containing the module

You can populate exports either directly or with the `export` statement:

```c
export foo = 123;
export greet = fn(name) { return "hello " + name; };
export { foo as foo2, greet as hi };

// equivalent:
exports["foo"] = 123;
exports["greet"] = fn(name) { return "hello " + name; };
exports["foo2"] = exports["foo"];
exports["hi"] = exports["greet"];
```

You can also import with statement syntax (sugar over `require()`):

```c
import "./util.cs";                 // side effects only
import util from "./util.cs";        // exports map
import {join, ext as path_ext} from "./util.cs";
```

If a script is executed directly (not via `require()`), `export` will create a global `exports` map automatically.

## `require_optional(path)`

Like `require()`, but returns `nil` if the file doesn't exist instead of throwing an error.

```c
// Try to load optional configuration
let config = require_optional("config.cs");
if (config == nil) {
  print("No config file found, using defaults");
  config = default_config();
}
```

Useful for:

* Optional plugins or extensions
* Configuration files that may not exist
* Conditional feature loading based on file presence

Returns:

* module exports if file exists
* `nil` if file doesn't exist

## Path Helpers

Use:

* `path_join(a,b)`
* `path_dirname(p)`
* `path_basename(p)`
* `path_ext(p)`

Example:

```c
let p = path_join("src", "main.cs");
print(path_dirname(p));
print(path_basename(p));
print(path_ext(p));
```
