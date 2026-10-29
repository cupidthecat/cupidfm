// Conway's Game of Life (2D) demonstration
// Rules:
// - Any live cell with fewer than two live neighbours dies.
// - Any live cell with two or three live neighbours lives.
// - Any live cell with more than three live neighbours dies.
// - Any dead cell with exactly three live neighbours becomes a live cell.

// Disable execution timeout so the demo can run continuously.
set_timeout(0);

let width = 30;
let height = 15;
// Set to 0 to run forever (Ctrl+C to stop)
let steps = 0;
let delay_ms = 100;

// Initialize a pattern that evolves for many generations (R-pentomino)
let grid = list();
for y in range(height) {
  let row = list();
  for x in range(width) {
    push(row, 0);
  }
  push(grid, row);
}

// R-pentomino near the center
let cx = floor(width / 2);
let cy = floor(height / 2);
grid[cy][cx + 1] = 1;
grid[cy][cx + 2] = 1;
grid[cy + 1][cx] = 1;
grid[cy + 1][cx + 1] = 1;
grid[cy + 2][cx + 1] = 1;

fn in_bounds(x, y) {
  return x >= 0 && x < width && y >= 0 && y < height;
}

fn neighbors(x, y) {
  let count = 0;
  for dy in range(-1, 2) {
    for dx in range(-1, 2) {
      if (dx == 0 && dy == 0) { continue; }
      let nx = x + dx;
      let ny = y + dy;
      if (in_bounds(nx, ny) && grid[ny][nx] == 1) {
        count = count + 1;
      }
    }
  }
  return count;
}

fn render() {
  let b = strbuf();
  for y in range(height) {
    for x in range(width) {
      if (grid[y][x] == 1) { b.append("#"); }
      else { b.append("."); }
    }
    b.append("\n");
  }
  return b.str();
}

fn clear_screen() {
  // Without ANSI escapes available, approximate a clear by printing blank lines.
  let b = strbuf();
  for i in range(height + 5) {
    b.append("\n");
  }
  print(b.str());
}

let step = 0;
while (steps == 0 || step < steps) {
  clear_screen();
  print(render());
  let next = list();
  for y in range(height) {
    let row = list();
    for x in range(width) {
      let n = neighbors(x, y);
      let alive = grid[y][x] == 1;
      if (alive && (n < 2 || n > 3)) {
        push(row, 0);
      } else if (!alive && n == 3) {
        push(row, 1);
      } else if (alive) {
        push(row, 1);
      } else {
        push(row, 0);
      }
    }
    push(next, row);
  }
  grid = next;
  step = step + 1;
  if (delay_ms > 0) { await sleep(delay_ms); }
}
