let many_maps = "items:\n";
for (let i = 0; i < 5; i = i + 1) {
    many_maps = many_maps + "  - key: value\n";
}
print("YAML:");
print(many_maps);
let d = yaml_parse(many_maps);
print("Type:", typeof(d));
print("Keys:", keys(d));
if (typeof(d.items) != "nil") {
    print("Items type:", typeof(d.items));
    print("Items len:", len(d.items));
}
