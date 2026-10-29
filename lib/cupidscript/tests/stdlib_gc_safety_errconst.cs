// Stdlib gc/safety APIs + ERR constant

// ERR constant
assert(is_map(ERR), "ERR is map");
assert(mhas(ERR, "DIV_ZERO"), "ERR.DIV_ZERO exists");
assert(mhas(ERR, "TYPE_ERROR"), "ERR.TYPE_ERROR exists");

// gc_stats shape
let st = gc_stats();
assert(is_map(st), "gc_stats map");
assert(mhas(st, "tracked"), "tracked");
assert(mhas(st, "collections"), "collections");
assert(mhas(st, "collected"), "collected");
assert(mhas(st, "allocations"), "allocations");

// gc_config get/set
let cfg0 = gc_config();
assert(is_map(cfg0), "gc_config get map");
assert(mhas(cfg0, "threshold"), "cfg threshold");
assert(mhas(cfg0, "alloc_trigger"), "cfg alloc_trigger");

assert(gc_config({"threshold": 0, "alloc_trigger": 0}), "gc_config(map) returns true");
let cfg1 = gc_config();
assert(cfg1.threshold == 0, "threshold set");
assert(cfg1.alloc_trigger == 0, "alloc_trigger set");

// Safety controls
let old_timeout = get_timeout();
let old_limit = get_instruction_limit();

assert(set_timeout(old_timeout), "set_timeout ok");
assert(set_instruction_limit(old_limit), "set_instruction_limit ok");

assert(get_timeout() == old_timeout, "get_timeout matches");
assert(get_instruction_limit() == old_limit, "get_instruction_limit matches");

let c1 = get_instruction_count();
let z = 0;
for (z = 0; z < 1000; z = z + 1) { }
let c2 = get_instruction_count();
assert(c2 >= c1, "instruction count monotonic");

// gc() returns int
let g = gc();
assert(is_int(g), "gc returns int");

print("stdlib_gc_safety_errconst ok");
