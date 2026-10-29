let d1 = yaml_parse("list: [1, 2, 3]\n");
print("List:", d1.list);

let d2 = yaml_parse("map: {a: 1}\n");
print("Map:", d2.map);
