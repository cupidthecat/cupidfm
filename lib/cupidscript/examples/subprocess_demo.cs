// Subprocess demo: run a command and print captured output

let r = subprocess("echo", ["Hello from subprocess"]);
print("exit:", r.code);
print("out:", r.out);
