// Set demo: unique collection

let tags = set(["red", "green", "green", "blue"]);
print("len:", len(tags));
print("has green:", set_has(tags, "green"));

set_add(tags, "yellow");
set_del(tags, "red");

for t in tags {
  print("tag:", t);
}

print("values:", set_values(tags));
