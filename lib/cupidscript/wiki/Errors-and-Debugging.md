# Errors & Debugging

## Table of Contents

- [Parse Errors](#parse-errors)
- [Runtime Errors](#runtime-errors)
- [Exception Handling](#exception-handling)
- [Error Objects](#error-objects)
- [Stack Traces](#stack-traces)
- [Tips](#tips)

CupidScript has two major error classes:

* Parse errors (lexer/parser)
* Runtime errors (VM)

## Parse Errors

When parsing fails, the parser produces an allocated error string like:

```
Parse error at <input>:line:col: message
```

Examples:

* "expected expression"
* "expected ')'"
* "expected '{'"
* "expected function name"
* "invalid assignment target"

## Runtime Errors

Runtime errors include source location:

```
Runtime error at source:line:col: message
```

Common runtime errors:

* undefined variable
* type error: arithmetic expects int
* division by zero
* mod by zero
* comparisons require both ints or both strings
* attempted to call non-function
* wrong argument count
* indexing expects list[int] or map[key]
* field access expects map
* uncaught exception (from `throw`)

## Exception Handling

CupidScript supports exception handling via `throw`, `try/catch`, and optional `finally`:

```c
try {
  if (x < 0) {
    throw error("Invalid input: negative number", "INVALID_ARG");
  }
  process(x);
} catch (e) {
  if (is_error(e)) {
    print("Error caught:", e.msg);
  } else {
    print("Error caught:", e);
  }
} finally {
  // cleanup that should always run
  print("cleanup");
}
```

Thrown values can be any type - commonly strings or maps with error details:

```c
throw {"msg": "file not found", "code": 404, "path": p};
```

## Error Objects

For standardized error handling, use the `error()` helper to create error objects:

* `error(msg)` - Creates a map with `{msg, code, stack}` fields (`code` defaults to `"ERROR"`)
* `error(msg, code)` - Creates a map with `{msg, code, stack}` fields
* `is_error(value)` - Checks if value is an error object (has `msg` and `code` fields)
* `format_error(err)` - Formats an error object into a readable string (`[CODE] message`)

There is also a global `ERR` map containing common string error codes you can use with `error(msg, code)`.

The `stack` field contains a list of stack frame strings captured at the time of error creation.

Example:

```c
fn divide(a, b) {
  if (b == 0) {
    throw error("Division by zero", "DIV_ZERO");
  }
  return a / b;
}

try {
  let result = divide(10, 0);
} catch (e) {
  if (is_error(e)) {
    print("Error:", e.msg);
    print("Code:", e.code);
    print("Formatted:", format_error(e));
    print("Stack trace:");
    for frame in e.stack {
      print("  ", frame);
    }
  } else {
    print("Caught:", e);
  }
} finally {
  // release resources or reset state
  print("cleanup done");
}
```

Benefits of error objects:

* Consistent structure across all errors
* Automatic stack trace capture
* Error codes for categorization (`code` is always present)
* Easy to check with `is_error()`

`finally` can be a block or a single expression statement:

```c
try { do_work(); } catch (e) { print(e); } finally cleanup();
```

## Stack Traces

Your VM maintains a call frame stack for:

* native function calls
* user-defined function calls
* dotted "method/global fallback" calls

When an error occurs, the VM appends:

```
Stack trace:
  at func (file:line:col)
  at ...
```

This makes it practical to debug scripts without a debugger attached.

## Tips

* When building larger scripts, wrap assumptions with `assert(...)`.
* Use `typeof(x)` when debugging dynamic type issues.
* Use `fmt("%v", x)` or `print(x)` to inspect values.
