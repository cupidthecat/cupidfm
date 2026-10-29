// Filesystem stdlib example

let base = "demo_fs";
if (!exists(base)) {
  mkdir(base);
}

write_file(path_join(base, "hello.txt"), "Hello from CupidScript\n");
let content = read_file(path_join(base, "hello.txt"));
print("read:", content);

let entries = list_dir(base);
print("entries:", entries);

print("cwd:", cwd());
chdir(base);
print("cwd after chdir:", cwd());
chdir("..");

rename(path_join(base, "hello.txt"), path_join(base, "greeting.txt"));
print("renamed file exists:", exists(path_join(base, "greeting.txt")));

// cleanup
rm(path_join(base, "greeting.txt"));
rm(base);
