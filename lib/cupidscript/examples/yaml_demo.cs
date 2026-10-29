// YAML parsing and generation demonstration

print("=== YAMLParsing Demo ===\n");

// Parse a simple YAML config
let config_yaml = "app: MyApplication\nversion: 1.0.0\ndebug: true\nport: 8080\n";

print("Parsing YAML config:");
print(config_yaml);

let config = yaml_parse(config_yaml);
print("Parsed config:");
print("  App:", config.app);
print("  Version:", config.version);
print("  Debug:", config.debug);
print("  Port:", config.port);

print("\n=== YAML Generation Demo ===\n");

// Generate YAML from an object
let server_config = {
  "host": "localhost",
  "port": 3000,
  "debug": true
};

print("Generating YAML from object:");
let yaml_output = yaml_stringify(server_config);
print(yaml_output);

print("\n=== Working with Lists ===\n");

// Parse YAML with a list
let team_yaml = "team: DevOps\nmembers: [alice, bob, charlie]\nactive: true\n";

let team = yaml_parse(team_yaml);
print("Team:", team.team);
print("Members:", team.members);
print("Active:", team.active);

print("\n=== Data Types Demo ===\n");

// Different data types
let types_yaml = "string_val: hello\ninteger_val: 42\nfloat_val: 3.14\nbool_val: true\nnull_val: null\n";

let types = yaml_parse(types_yaml);
print("String:", types.string_val);
print("Integer:", types.integer_val);
print("Float:", types.float_val);
print("Boolean:", types.bool_val);
print("Null:", types.null_val);

print("\n=== Roundtrip Test ===\n");

// Create data, convert to YAML, parse back
let original = {
  "name": "Product X",
  "price": 29.99,
  "in_stock": true,
  "quantity": 100
};

print("Original data:");
print(original);

let yaml_str = yaml_stringify(original);
print("\nAs YAML:");
print(yaml_str);

let restored = yaml_parse(yaml_str);
print("\nRestored from YAML:");
print("  Name:", restored.name);
print("  Price:", restored.price);
print("  In stock:", restored.in_stock);
print("  Quantity:", restored.quantity);

print("\nYAML demo complete!");
