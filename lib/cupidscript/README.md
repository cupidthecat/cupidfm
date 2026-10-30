# CupidScript

CupidScript is a C99 interpreter and embeddable scripting runtime. It includes classes, generators, async/await, pattern matching, comprehensions, and collection types.

## Build and test

Install a C compiler, Make, and the OpenSSL/zlib development libraries. Tests also use Python 3 and the OpenSSL command-line tool.

```sh
make -j
make test
./bin/cupidscript script.cs
```

The build produces `bin/cupidscript`, `bin/libcupidscript.a`, and the C API tests. Set `CS_NO_TLS=1` to build without TLS support. Use separate `OBJDIR` and `BINDIR` values for sanitizer builds; commands are in the [test guide](wiki/Tests.md).

## Embed in C

Include `cupidscript.h`, create a VM with `cs_vm_new`, register native functions, and execute code with `cs_vm_run_file` or `cs_vm_run_string`. Check return codes and read `cs_vm_last_error` when execution fails.

Constructors and retained getters return owned references. Setters retain stored values, so release the caller's references after storing them. `cs_to_cstr` returns a borrowed pointer. Release host-held values before destroying the VM with `cs_vm_free`.

The [embedding guide](wiki/Embedding-API.md) includes a complete, tested C example and describes tuple builders, cycle collection, and shutdown ownership.

## Documentation

- [Wiki home](https://github.com/cupidthecat/cupidscript/wiki): language topics and standard library.
- [Full reference](https://github.com/cupidthecat/cupidscript/wiki/Reference) or its [source copy](docs/reference.md): syntax, examples, and host use.
- [Embedding API](https://github.com/cupidthecat/cupidscript/wiki/Embedding-API): retained values and C examples.
- [Tuples](https://github.com/cupidthecat/cupidscript/wiki/TUPLES): positional/named fields, length, and destructuring.
- [Tests](wiki/Tests.md): normal and sanitizer commands.

The blocking HTTP client supports content-length, chunked, and close-delimited responses. TLS verifies peer names against the OpenSSL trust store. See the reference for network limits.

The runtime includes filesystem, JSON/CSV/YAML/XML, regex, process, and network APIs. Host applications can configure execution limits. See the reference for platform dependencies and supported behavior.

See [LICENSE](LICENSE) for licensing terms.
