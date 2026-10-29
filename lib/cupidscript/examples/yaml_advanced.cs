// Advanced YAML Features Demonstration
// Showcasing YAML 1.2.2 capabilities

print("=== YAML 1.2.2 Advanced Features Demo ===\n");

// ============================================================================
// 1. Block Scalars with Chomping Indicators
// ============================================================================

print("1. Block Scalars with Chomping Indicators");
print("-" * 50);

// Strip chomping (|-) - removes all trailing newlines
let config_strip = yaml_parse("
script: |-
  echo 'Hello'
  echo 'World'
");

print("Strip chomping (|-):");
print(config_strip.script);
print("");

// Keep chomping (|+) - preserves all trailing newlines
let config_keep = yaml_parse("
script: |+
  echo 'First'
  echo 'Second'


");

print("Keep chomping (|+):");
print("[" + config_keep.script + "]");
print("");

// Clip chomping (|) - single newline at end (default)
let config_clip = yaml_parse("
description: |
  This is a multi-line
  description that ends
  with a single newline.
");

print("Clip chomping (| default):");
print(config_clip.description);
print("");

// Explicit indentation (|2)
let explicit_indent = yaml_parse("
code: |2
    def hello():
        print('indented')
");

print("Explicit indentation (|2):");
print(explicit_indent.code);
print("");

// Folded scalars (>)
let folded = yaml_parse("
text: >
  This is a long piece of text
  that will be folded into
  a single line with spaces.
");

print("Folded scalar (>):");
print(folded.text);
print("\n");

// ============================================================================
// 2. Type Tags
// ============================================================================

print("2. Explicit Type Tags");
print("-" * 50);

let tagged_yaml = "
string_num: !!str 12345
int_string: !!int \"789\"
float_string: !!float \"3.14159\"
bool_string: !!bool yes
force_null: !!null value
";

let tagged = yaml_parse(tagged_yaml);
print("!!str 12345 =>", tagged.string_num, "(type:", typeof(tagged.string_num) + ")");
print("!!int \"789\" =>", tagged.int_string, "(type:", typeof(tagged.int_string) + ")");
print("!!float \"3.14159\" =>", tagged.float_string, "(type:", typeof(tagged.float_string) + ")");
print("!!bool yes =>", tagged.bool_string, "(type:", typeof(tagged.bool_string) + ")");
print("!!null value =>", tagged.force_null, "(type:", typeof(tagged.force_null) + ")");
print("");

// ============================================================================
// 3. Merge Keys (<<)
// ============================================================================

print("3. Merge Keys (DRY Configuration)");
print("-" * 50);

let merge_yaml = "
# Define defaults with anchor
defaults: &defaults
  timeout: 30
  retry: 3
  log_level: info

# Service configurations inherit defaults
service_a:
  <<: *defaults
  name: Authentication Service
  port: 8001

service_b:
  <<: *defaults
  name: Database Service
  port: 8002
  timeout: 60  # Override default

# Multiple inheritance
cache_config: &cache
  ttl: 3600
  max_size: 1000

api_config: &api
  rate_limit: 100
  version: v1

combined_service:
  <<: [*defaults, *cache, *api]
  name: Combined Service
  port: 9000
";

let merged = yaml_parse(merge_yaml);
print("Service A:");
print("  Name:", merged.service_a.name);
print("  Port:", merged.service_a.port);
print("  Timeout:", merged.service_a.timeout);
print("  Retry:", merged.service_a.retry);
print("");

print("Service B (with timeout override):");
print("  Name:", merged.service_b.name);
print("  Port:", merged.service_b.port);
print("  Timeout:", merged.service_b.timeout);
print("  Retry:", merged.service_b.retry);
print("");

print("Combined Service (multiple merges):");
print("  Name:", merged.combined_service.name);
print("  Timeout:", merged.combined_service.timeout);
print("  TTL:", merged.combined_service.ttl);
print("  Rate Limit:", merged.combined_service.rate_limit);
print("\n");

// ============================================================================
// 4. Multiple Documents
// ============================================================================

print("4. Multiple Documents in One Stream");
print("-" * 50);

let multi_doc_yaml = "
%YAML 1.2
---
# Document 1: Configuration
app: MyApp
version: 1.0.0
...
---
# Document 2: Deployment
environment: production
servers:
  - web1.example.com
  - web2.example.com
---
# Document 3: Monitoring
metrics:
  enabled: true
  interval: 60
";

let documents = yaml_parse_all(multi_doc_yaml);
print("Parsed", len(documents), "documents:");
print("");

print("Document 1 (Config):");
print("  App:", documents[0].app);
print("  Version:", documents[0].version);
print("");

print("Document 2 (Deployment):");
print("  Environment:", documents[1].environment);
print("  Servers:", documents[1].servers);
print("");

print("Document 3 (Monitoring):");
print("  Metrics enabled:", documents[2].metrics.enabled);
print("  Interval:", documents[2].metrics.interval);
print("\n");

// ============================================================================
// 5. Explicit Keys
// ============================================================================

print("5. Explicit Keys (? indicator)");
print("-" * 50);

let explicit_keys_yaml = "
# Simple explicit key
? simple_key
: simple_value

# Explicit key allows complex key structures
? [complex, key, list]
: value_for_complex_key

# Useful for disambiguation
? key_with_colon:in:it
: value
";

let explicit = yaml_parse(explicit_keys_yaml);
print("Simple explicit key:", explicit.simple_key);
print("Complex key exists:", explicit["[complex, key, list]"] != nil || explicit["complex"] != nil);
print("\n");

// ============================================================================
// 6. Unicode Escapes
// ============================================================================

print("6. Unicode Escape Sequences");
print("-" * 50);

let unicode_yaml = "
# \\uXXXX for BMP characters
greeting: \"\\u0048\\u0065\\u006c\\u006c\\u006f\"

# \\UXXXXXXXX for full Unicode range
emoji: \"\\U0001F44B\"  # Waving hand

# Chinese characters
chinese: \"\\u4F60\\u597D\"  # Nǐ hǎo (Hello)

# Mix of ASCII and Unicode
message: \"Hello \\u4E16\\u754C!\"  # Hello 世界!
";

let unicode = yaml_parse(unicode_yaml);
print("Greeting:", unicode.greeting);
print("Emoji:", unicode.emoji);
print("Chinese:", unicode.chinese);
print("Mixed:", unicode.message);
print("\n");

// ============================================================================
// 7. Anchors and Aliases
// ============================================================================

print("7. Anchors and Aliases (References)");
print("-" * 50);

let anchor_yaml = "
# Define reusable blocks
database_defaults: &db_defaults
  host: localhost
  port: 5432
  pool_size: 10

# Reference them multiple times
primary_db:
  <<: *db_defaults
  database: primary

secondary_db:
  <<: *db_defaults
  database: secondary
  port: 5433  # Override

# Anchor simple values too
admin_email: &admin admin@example.com
notifications:
  to: *admin
  from: *admin
";

let anchored = yaml_parse(anchor_yaml);
print("Primary DB:", anchored.primary_db.database, "on", anchored.primary_db.host + ":" + anchored.primary_db.port);
print("Secondary DB:", anchored.secondary_db.database, "on", anchored.secondary_db.host + ":" + anchored.secondary_db.port);
print("Notifications to:", anchored.notifications.to);
print("\n");

// ============================================================================
// 8. JSON Compatibility
// ============================================================================

print("8. JSON Compatibility (YAML 1.2 is JSON superset)");
print("-" * 50);

// Pure JSON is valid YAML
let json_data = yaml_parse('{
  "name": "John Doe",
  "age": 30,
  "email": "john@example.com",
  "hobbies": ["reading", "coding", "gaming"],
  "address": {
    "street": "123 Main St",
    "city": "Anytown",
    "zip": "12345"
  }
}');

print("Name:", json_data.name);
print("Age:", json_data.age);
print("Hobbies:", json_data.hobbies);
print("City:", json_data.address.city);
print("\n");

// ============================================================================
// 9. Practical Example: Complete Application Config
// ============================================================================

print("9. Real-World Application Configuration");
print("-" * 50);

let app_config_yaml = "
%YAML 1.2
---
# Application Configuration

# Common settings with anchor
common: &common_settings
  log_level: info
  timeout: 30
  retry_attempts: 3

# Development environment
development:
  <<: *common_settings
  database:
    host: localhost
    port: 5432
    name: myapp_dev
  debug: true
  cache:
    enabled: false

# Production environment
production:
  <<: *common_settings
  database:
    host: db.prod.example.com
    port: 5432
    name: myapp_prod
    pool_size: 20
    ssl: true
  debug: false
  cache:
    enabled: true
    ttl: 3600
    backend: redis
  monitoring:
    enabled: true
    endpoints:
      - https://monitor1.example.com
      - https://monitor2.example.com

# Feature flags
features: &features
  new_ui: !!bool yes
  beta_api: !!bool no
  analytics: !!bool yes

# Apply features
app:
  name: MyApplication
  version: \"2.1.0\"
  features: *features
  description: |
    This is a multi-line description
    of the application that supports
    YAML 1.2.2 features.
";

let app_config = yaml_parse(app_config_yaml);
print("Application:", app_config.app.name, "v" + app_config.app.version);
print("");
print("Development:");
print("  Database:", app_config.development.database.host + ":" + app_config.development.database.port);
print("  Debug:", app_config.development.debug);
print("  Timeout:", app_config.development.timeout);
print("");
print("Production:");
print("  Database:", app_config.production.database.host + ":" + app_config.production.database.port);
print("  Pool size:", app_config.production.database.pool_size);
print("  Cache enabled:", app_config.production.cache.enabled);
print("  Monitoring:", app_config.production.monitoring.enabled);
print("");
print("Features:");
print("  New UI:", app_config.app.features.new_ui);
print("  Beta API:", app_config.app.features.beta_api);
print("  Analytics:", app_config.app.features.analytics);

print("\n=== Demo Complete ===");
