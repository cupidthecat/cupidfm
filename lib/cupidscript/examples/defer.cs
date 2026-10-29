// defer statement example

fn with_cleanup() {
  defer { print("cleanup"); }
  print("work");
}

with_cleanup();
