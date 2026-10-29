// Filesystem stdlib tests

fn list_has(xs, name) {
  for x in xs {
    if (x == name) { return true; }
  }
  return false;
}

let base = "tmp_fs_test";

// Best-effort cleanup from previous runs
if (exists(base)) {
  if (is_dir(base)) {
    let entries = list_dir(base);
    if (entries != nil) {
      for e in entries {
        let p = path_join(base, e);
        if (is_dir(p)) {
          rm(p); // only empty dirs
        } else {
          rm(p);
        }
      }
    }
    rm(base);
  } else {
    rm(base);
  }
}

assert(mkdir(base), "mkdir base");
assert(is_dir(base), "is_dir base");

let file = path_join(base, "a.txt");
assert(write_file(file, "hello"), "write_file");
assert(exists(file), "exists file");
assert(is_file(file), "is_file");
let data = read_file(file);
assert(data == "hello", "read_file");

let entries = list_dir(base);
assert(entries != nil, "list_dir");
assert(list_has(entries, "a.txt"), "list_dir includes file");

let new_file = path_join(base, "b.txt");
assert(rename(file, new_file), "rename");
assert(!exists(file), "old file removed");
assert(exists(new_file), "new file exists");

let sub = path_join(base, "sub");
assert(mkdir(sub), "mkdir sub");
assert(is_dir(sub), "is_dir sub");
entries = list_dir(base);
assert(list_has(entries, "sub"), "list_dir includes subdir");

let old = cwd();
assert(chdir(base), "chdir base");
let expected = path_join(old, base);
assert(cwd() == expected, "cwd changed");
assert(write_file("c.txt", "x"), "write in cwd");
assert(read_file("c.txt") == "x", "read in cwd");
assert(chdir(".."), "chdir back");

assert(rm(path_join(base, "c.txt")), "rm file in base");
assert(rm(new_file), "rm renamed");
assert(rm(sub), "rm subdir");
assert(rm(base), "rm base");

print("filesystem ok");
