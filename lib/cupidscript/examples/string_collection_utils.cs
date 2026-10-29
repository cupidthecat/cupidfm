// String + collection utility examples

print(str_contains("hello", "ell"));
print(str_count("banana", "na"));
print(str_pad_start("7", 4, "0"));
print(str_pad_end("7", 4, "0"));
print(str_reverse("Cupid"));

let xs = [1, 2, 2, 3, 1, 4];
print(list_unique(xs));
print(list_flatten([1, [2, 3], [4], 5]));
print(list_chunk([1, 2, 3, 4, 5], 2));
print(list_compact([1, nil, 2, nil, 3]));
print(list_sum([1, 2, 3]));
