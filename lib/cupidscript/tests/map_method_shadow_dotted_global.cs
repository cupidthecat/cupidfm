// If a base identifier exists, method call should use the map, not dotted-global fallback.

let fm = map();
fm["status"] = fn(x) { return x; };

assert(fm.status("ok") == "ok", "map method should shadow fm.status native");

print("map_method_shadow_dotted_global ok");

