// Map keys accept any value

let m = {"a": 1};
m[0] = 2;
assert(m[0] == 2, "map int key assignment");
print("negative_index_assign_types ok");
