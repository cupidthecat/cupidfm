// gc_config() variants

// set via (threshold, alloc_trigger)
assert(gc_config(0, 0), "gc_config(two ints) returns true");
let cfg = gc_config();
assert(cfg.threshold == 0, "threshold 0");
assert(cfg.alloc_trigger == 0, "alloc_trigger 0");

// invalid args returns nil (no error)
assert(is_nil(gc_config("nope")), "gc_config(bad) -> nil");
assert(gc_config(1, "nope"), "gc_config(mixed bad) -> true");
let cfg2 = gc_config();
assert(cfg2.threshold == 1, "mixed bad applies threshold");

print("stdlib_gc_config_variants ok");
