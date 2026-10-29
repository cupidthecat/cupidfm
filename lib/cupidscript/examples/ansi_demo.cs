// ANSI escape sequence demo (colors + cursor control)
// Note: support depends on your terminal.

let esc = "\x1b";

// Clear screen and move cursor to top-left
print(esc + "[2J" + esc + "[H");

print(esc + "[31m" + "red" + esc + "[0m");
print(esc + "[32m" + "green" + esc + "[0m");
print(esc + "[34m" + "blue" + esc + "[0m");
