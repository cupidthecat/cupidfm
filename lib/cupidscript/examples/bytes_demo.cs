// Bytes demo: binary file roundtrip

let payload = bytes([0, 1, 2, 255]);
print("len:", len(payload));
print("first:", payload[0], "last:", payload[3]);

let path = "bytes_demo.bin";
write_file_bytes(path, payload);
let back = read_file_bytes(path);
print("roundtrip ok:", back == payload);

rm(path);
