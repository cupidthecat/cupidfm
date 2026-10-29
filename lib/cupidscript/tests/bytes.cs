// Bytes / binary data tests

let b = bytes(4);
assert(typeof(b) == "bytes", "typeof bytes");
assert(is_bytes(b), "is_bytes");
assert(len(b) == 4, "bytes length");
assert(b[0] == 0 && b[1] == 0, "zero filled");

b[1] = 255;
assert(b[1] == 255, "byte set/get");

b[5] = 7;
assert(len(b) == 6, "bytes extend on set");
assert(b[5] == 7, "byte set/get extended");

let from_str = bytes("A");
assert(len(from_str) == 1, "bytes from string length");
assert(from_str[0] == 65, "bytes from string data");

let from_list = bytes([1, 2, 3]);
assert(len(from_list) == 3, "bytes from list length");
assert(from_list[2] == 3, "bytes from list data");

let file = "tmp_bytes_test.bin";
assert(write_file_bytes(file, from_list), "write_file_bytes");
let r = read_file_bytes(file);
assert(r != nil, "read_file_bytes");
assert(r == from_list, "bytes roundtrip");
assert(rm(file), "cleanup");

print("bytes ok");
