let value = ["user", 42];

let label = match (value) {
  case [name, id] if id > 0: "user:" + name;
  case _: "unknown";
};

print(label);
