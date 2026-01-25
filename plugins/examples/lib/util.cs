// util.cs - helper functions for example plugins

fn list_from_csv(s) {
  let out = list();
  if (s == nil) {
    return out;
  }
  // Keep it simple: comma-separated with no escaping.
  let parts = str_split(s, ",");
  let n = len(parts);
  let i = 0;
  while (i < n) {
    let p = parts[i];
    // Light "trim": remove spaces (paths with spaces won't work with this helper).
    p = str_replace(p, " ", "");
    if (p != "") {
      push(out, p);
    }
    i = i + 1;
  }
  return out;
}

fn is_empty(s) {
  return (s == nil) || (s == "");
}
