# Implementation Notes (Lexer / Parser / VM)

## Table of Contents

- [Lexer](#lexer)
- [Parser](#parser)
- [VM / Runtime](#vm--runtime)
- [Safety Controls](#safety-controls)
- [Garbage Collection](#garbage-collection)

This page is for contributors hacking on the language runtime.

## Lexer

### Whitespace & Comments

The lexer skips:

* spaces, tabs, CR, LF
* `//` line comments
* `/* ... */` block comments

### Tokens

Recognizes:

* integers (decimal with underscores, or hex `0xFF` with underscores)
* floats (decimal point required: `3.14`, scientific notation: `1.5e-3`, `2e10`)
* identifiers / keywords: `let`, `fn`, `if`, `else`, `while`, `for`, `in`, `return`, `break`, `continue`, `throw`, `try`, `catch`, `finally`, `export`, `import`, `from`, `as`, `true`, `false`, `nil`
* strings (double-quoted, allows escapes)
* operators/punctuation:

  * `()[]{} , ; . : ?`
  * `+ - * / %`
  * `! !=`
  * `= == += -= *= /=`
  * `< <=`
  * `> >=`
  * `&&`
  * `||`

### Source Locations

Tokens carry `line` and `col` which update on newline.

## Parser

### AST Node Types

Statements:

* `N_BLOCK`
* `N_LET`, `N_ASSIGN`, `N_SETINDEX`
* `N_IF`, `N_WHILE`, `N_RETURN`
* `N_FORIN` (for-in loops)
* `N_BREAK`, `N_CONTINUE`
* `N_THROW`, `N_TRY` (exception handling, with optional `finally` block)
* `N_EXPORT`, `N_EXPORT_LIST` (module exports)
* `N_IMPORT` (module imports)
* `N_EXPR_STMT`
* `N_FNDEF`

Expressions:

* `N_BINOP`, `N_UNOP`
* `N_TERNARY` (ternary operator `? :`)
* `N_CALL`
* `N_INDEX`
* `N_GETFIELD`
* `N_FUNCLIT`
* `N_LISTLIT` (list literals `[...]`)
* `N_MAPLIT` (map literals `{...}`)
* `N_IDENT`
* `N_LIT_INT`, `N_LIT_FLOAT`, `N_LIT_STR`, `N_LIT_BOOL`, `N_LIT_NIL`
* `N_PATTERN_TYPE` (type pattern `Type(x)` in match/switch)

### Assignment Grammar

Assignments are statements only:

* `name = expr;`
* `name += expr;` (also `-=`, `*=`, `/=`)
* `target[index] = expr;`

Parser uses a lookahead approach:

* if a statement starts with `IDENT`, it tries parsing an lvalue and checks for assignment operators.
* It rewinds lexer state and then parses properly.
* Compound assignments (`+=` etc.) are desugared to `name = name + expr` during parsing.

### Semicolons

Semicolons are optional in many places; `maybe_semi()` consumes an optional `;`.

## VM / Runtime

### Values

`cs_value` carries a `cs_type` tag and either:

* immediate (`bool`, `int`, `float`)
* pointer to refcounted heap objects (`string`, `list`, `map`, `set`, `strbuf`, `function`, `native`)

Note: `float` is stored as a 64-bit `double` in the value union.

### Environments

`cs_env` is a chained scope (linked list via `parent`), storing parallel arrays of:

* `keys[]` (malloc'd C strings)
* `vals[]` (cs_value)

Lookup walks outward; assignment updates nearest existing scope, else creates in current scope.

## Safety Controls

The VM includes built-in protection against runaway scripts:

**Instruction Counting:**
- `vm->instruction_count` increments on every expression evaluation in `eval_expr()`
- Checked against `vm->instruction_limit` (0 = unlimited)
- Prevents infinite loops from consuming CPU indefinitely

**Timeout Tracking:**
- `vm->exec_start_ms` records wall-clock time at script start
- `vm->exec_timeout_ms` sets maximum execution duration (0 = unlimited)
- Checked every 1000 instructions to minimize overhead
- Prevents long-running operations from blocking the host

**Interrupt Mechanism:**
- `vm->interrupt_requested` flag can be set from any thread
- Checked on every expression evaluation
- Allows host to cancel script execution (e.g., from UI cancel button)

**Implementation:**
- `vm_check_safety()` called at start of `eval_expr()`
- All counters reset in `run_ast_in_env()` at script start
- Errors reported with location context when limits exceeded

### Functions

A function value stores:

* parameter list
* body AST pointer
* closure env (refcounted)

### Async Scheduler

Async functions return promises and are scheduled as tasks in a cooperative queue.

* `sleep(ms)` schedules a timer that resolves its promise at `now + ms`.
* `await` runs the scheduler until the promise resolves (or rejects).
* Rejections propagate as runtime throws from `await`.

### Strings

Parser stores raw token text (including quotes) for string literals.
VM unescapes at evaluation time.

### Field Access Behavior

* `map.field` → `map_get(map, "field")`
* If `a.b.c` is used and `a` is *undefined*, VM can fall back to a dotted global lookup of `"a.b.c"` for compatibility with "namespaced globals".

### strbuf Methods

Handled as special cases in `CALL` when callee is `GETFIELD`:

* `append`, `str`, `clear`, `len`

## Garbage Collection

### Reference Counting

All heap objects use reference counting:

* Strings (`cs_string`)
* Lists (`cs_list_obj`)
* Maps (`cs_map_obj`)
* String builders (`cs_strbuf_obj`)
* Functions (`cs_func`)
* Native functions (`cs_native`)

When refcount reaches 0, objects are freed immediately.

### Cycle Detection

Lists and maps can form reference cycles (e.g., `list[0] = list`). The VM tracks all live lists/maps in a linked list (`vm->tracked`).

**Algorithm:**

1. Build candidate list from tracked objects
2. Initialize `gc_refs` to actual refcount for each
3. Subtract internal references (within tracked objects)
4. Mark objects with `gc_refs > 0` as reachable (externally referenced)
5. Recursively mark objects reachable from marked set
6. Collect unmarked objects (cycles with no external refs)

**Trigger Points:**

* Manual: `gc()` function in scripts
* Automatic: configurable via `gc_config()`

### Auto-GC Policy

The VM supports automatic garbage collection based on two policies:

**Threshold-Based:**
- `vm->gc_threshold` - collect when `tracked_count >= threshold`
- Useful for limiting memory footprint
- Set via `cs_vm_set_gc_threshold(vm, N)` or `gc_config(N, ...)`

**Allocation-Based:**
- `vm->gc_alloc_trigger` - collect every N list/map allocations
- `vm->gc_allocations` tracks allocations since last GC
- Useful for regular cleanup during heavy allocation
- Set via `cs_vm_set_gc_alloc_trigger(vm, N)` or `gc_config(..., N)`

**Trigger Points:**

* During `list_new()` and `map_new()` after tracking
* After `cs_vm_run_file()` completes
* After `cs_vm_run_string()` completes

**Statistics:**

* `vm->gc_collections` - total collections performed
* `vm->gc_objects_collected` - total objects freed
* Accessible via `gc_stats()` function

**Default Behavior:**

* Both policies disabled by default (0)
* GC only runs when explicitly called via `gc()`
* Host can enable policies for automatic memory management

**Example Configurations:**

```c
// Collect when 1000 objects tracked
cs_vm_set_gc_threshold(vm, 1000);

// Collect every 500 allocations
cs_vm_set_gc_alloc_trigger(vm, 500);

// Both policies
gc_config(1000, 500);

// Manual only (default)
gc_config(0, 0);
```

