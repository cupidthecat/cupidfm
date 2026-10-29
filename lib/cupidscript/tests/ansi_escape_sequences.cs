let esc = "\x1b";
assert(len(esc) == 1, "hex escape produces one byte");

let clear = "\x1b[2J";
assert(len(clear) == 4, "ansi sequence length");

print("ansi_escape_sequences ok");
