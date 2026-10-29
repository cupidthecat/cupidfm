// JSON example

let obj = {"name": "frank", "age": 30, "tags": ["a", "b"]};
let s = json_stringify(obj);
print("json:", s);

let back = json_parse(s);
print("name:", back.name);
print("tags:", back.tags);
