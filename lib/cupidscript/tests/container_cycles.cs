gc_config(0, 0);
let items = [];
let record = (items,);
push(items, record);
assert(gc() == 0, "a rooted tuple/list cycle remains alive");
assert(record[0] == items, "rooted tuple survives collection");
items = nil;
record = nil;
assert(gc() == 2, "collect tuple/list cycle");

let keys = {};
keys[keys] = keys;
keys = nil;
assert(gc() == 1, "collect map with itself as key and value");

let members = set();
set_add(members, members);
members = nil;
assert(gc() == 1, "collect set with itself as key");

let tail = [];
let owner = {};
owner[tail] = owner;
push(tail, owner);
tail = nil;
owner = nil;
assert(gc() == 2, "collect mutually referring map keys and lists");

// Teardown must collect cycles even when automatic collection is disabled.
let teardown = [];
push(teardown, teardown);
