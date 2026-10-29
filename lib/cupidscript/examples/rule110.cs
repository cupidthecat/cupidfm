// Rule 110 cellular automaton (1D) demonstration
// Shows Turing-complete behavior with a simple local rule.

let width = 41;
let steps = 25;
let mid = floor(width / 2);

let cells = list();
for i in range(width) {
  push(cells, i == mid ? 1 : 0);
}

fn rule110(l, c, r) {
  // Patterns that produce 1 in Rule 110:
  // 111->0, 110->1, 101->1, 100->0, 011->1, 010->1, 001->1, 000->0
  if (l == 1 && c == 1 && r == 0) { return 1; }
  if (l == 1 && c == 0 && r == 1) { return 1; }
  if (l == 0 && c == 1 && r == 1) { return 1; }
  if (l == 0 && c == 1 && r == 0) { return 1; }
  if (l == 0 && c == 0 && r == 1) { return 1; }
  return 0;
}

fn render(row) {
  let b = strbuf();
  for v in row {
    if (v == 1) { b.append("#"); }
    else { b.append("."); }
  }
  return b.str();
}

for step in range(steps) {
  print(render(cells));
  let next = list();
  for i in range(width) {
    let left = (i == 0) ? 0 : cells[i - 1];
    let center = cells[i];
    let right = (i == width - 1) ? 0 : cells[i + 1];
    push(next, rule110(left, center, right));
  }
  cells = next;
}
