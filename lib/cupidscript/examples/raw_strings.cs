// Raw (backtick) strings are literal: no escapes, can be multiline.
let config = `{
  "path": "C:\\tools\\app",
  "enabled": true
}`;

print(config);

let pattern = `^\w+\.cs$`;
print("pattern:", pattern);
