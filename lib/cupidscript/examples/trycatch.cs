// Demonstrates: throw + try/catch, thrown values as data

fn boom() {
  throw { "msg": "boom", "code": 123 };
}

fn wrapper() {
  boom();
  print("unreachable");
}

try {
  wrapper();
  print("unreachable");
} catch (e) {
  print("caught:", e["msg"], e["code"]);
}

