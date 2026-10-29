// Demonstrates try/catch/finally for cleanup

fn open_resource(path) {
  // placeholder for a resource handle
  return {"path": path};
}

fn close_resource(res) {
  print("closing", res.path);
}

let res = nil;
try {
  res = open_resource("/tmp/example.txt");
  // simulate work
  print("working on", res.path);
  // simulate error
  throw error("boom", "E_IO");
} catch (e) {
  print("caught", format_error(e));
} finally {
  if (res != nil) {
    close_resource(res);
  }
  print("cleanup complete");
}
