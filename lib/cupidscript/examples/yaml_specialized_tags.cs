// YAML 1.2.2 Specialized Tags - Real-World Examples
// Demonstrates practical usage of !!binary, !!timestamp, !!set, !!omap, and !!pairs

print("=== YAML 1.2.2 Specialized Tags Examples ===\n");

// ============================================================================
// Example 1: Configuration File with Timestamps
// ============================================================================
print("--- Example 1: Configuration with Timestamps ---");

let config_yaml = "
application:
  name: MyApp
  version: 2.1.0
  deployed: !!timestamp 2024-01-27T10:30:00Z
  last_updated: !!timestamp 2024-01-27T15:45:23.456Z
  
maintenance_windows:
  - start: !!timestamp 2024-02-01T02:00:00Z
    end: !!timestamp 2024-02-01T04:00:00Z
  - start: !!timestamp 2024-02-15T02:00:00Z
    end: !!timestamp 2024-02-15T04:00:00Z
";

let config = yaml_parse(config_yaml);
print("Application: " + config.application.name + " v" + config.application.version);
print("Deployed: " + config.application.deployed);
print("Last Updated: " + config.application.last_updated);
print("Maintenance windows: " + len(config.maintenance_windows));

// ============================================================================
// Example 2: Feature Flags using !!set
// ============================================================================
print("\n--- Example 2: Feature Flags with Sets ---");

let features_yaml = "
enabled_features: !!set
  dark_mode: ~
  notifications: ~
  analytics: ~
  experimental_ui: ~
  
disabled_features: !!set {legacy_api: ~, old_dashboard: ~}

user_permissions: !!set [read, write, delete, admin]
";

let features = yaml_parse(features_yaml);

fn check_feature(feature_name, feature_set) {
    if (feature_set[feature_name] == nil) {
        return "✓ " + feature_name + " is enabled";
    }
    return "✗ " + feature_name + " is disabled";
}

print(check_feature("dark_mode", features.enabled_features));
print(check_feature("notifications", features.enabled_features));
print(check_feature("legacy_api", features.disabled_features));

print("\nUser has permissions: " + join(keys(features.user_permissions), ", "));

// ============================================================================
// Example 3: Ordered Configuration with !!omap
// ============================================================================
print("\n--- Example 3: Build Pipeline Stages (Order Matters) ---");

let pipeline_yaml = "
pipeline: !!omap
  - checkout:
      command: git clone
      timeout: 300
  - install:
      command: npm install
      timeout: 600
  - test:
      command: npm test
      timeout: 1200
  - build:
      command: npm run build
      timeout: 900
  - deploy:
      command: ./deploy.sh
      timeout: 1800
";

let pipeline = yaml_parse(pipeline_yaml);

print("Build Pipeline Stages:");
let stage_num = 1;
for stage in pipeline.pipeline {
    let stage_name = keys(stage)[0];
    let stage_config = stage[stage_name];
    print("  " + stage_num + ". " + stage_name + " - " + stage_config.command + 
          " (timeout: " + stage_config.timeout + "s)");
    stage_num = stage_num + 1;
}

// ============================================================================
// Example 4: Event Handlers with !!pairs (Duplicate Keys Allowed)
// ============================================================================
print("\n--- Example 4: Event Handlers with Multiple Listeners ---");

let events_yaml = "
event_handlers: !!pairs
  - click: validate_form
  - click: track_analytics
  - click: submit_data
  - hover: show_tooltip
  - hover: preload_data
  - blur: save_draft
  - blur: hide_tooltip
";

let events = yaml_parse(events_yaml);

print("Event Handlers Configuration:");
let handlers_by_event = {};
for handler in events.event_handlers {
    let event_name = keys(handler)[0];
    let handler_func = handler[event_name];
    
    if (!handlers_by_event[event_name]) {
        handlers_by_event[event_name] = [];
    }
    push(handlers_by_event[event_name], handler_func);
}

for event_name in keys(handlers_by_event) {
    print("  " + event_name + ": " + join(handlers_by_event[event_name], ", "));
}

// ============================================================================
// Example 5: Binary Data - Small Images/Icons
// ============================================================================
print("\n--- Example 5: Embedded Binary Data ---");

let assets_yaml = "
icons:
  checkmark: !!binary |
    iVBORw0KGgoAAAANSUhEUgAAAAoAAAAKCAYAAACNMs+9AAAAFUlEQVR42mNk
    YPhfz0AEYBxVSF+FAP5FDvcfRYWgAAAAAElFTkSuQmCC
  
  warning: !!binary |
    R0lGODlhEAAQAMQAAORHHOVSKudfOulrSOp3WOyDZu6QdvCchPGolfO0o/XB
    s/fNwfjZ0frl3/zy7////wAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=

files:
  config: !!binary Y29uZmlnPXRydWUK
  secret: !!binary c2VjcmV0X2tleV9oZXJl
";

let assets = yaml_parse(assets_yaml);

print("Loaded binary assets:");
print("  Checkmark icon: " + len(assets.icons.checkmark) + " bytes");
print("  Warning icon: " + len(assets.icons.warning) + " bytes");
print("  Config file: " + len(assets.files.config) + " bytes");
print("  Secret key: " + len(assets.files.secret) + " bytes");

// You can use these bytes for actual image processing, file writing, etc.
// Example: write_file("checkmark.png", assets.icons.checkmark);

// ============================================================================
// Example 6: Complex Real-World Scenario - API Documentation
// ============================================================================
print("\n--- Example 6: API Endpoint Documentation ---");

let api_yaml = "
api_version: !!timestamp 2024-01-27
endpoints: !!omap
  - /users:
      methods: !!set [GET, POST]
      auth: required
      rate_limit: 100
  - /users/{id}:
      methods: !!set [GET, PUT, DELETE]
      auth: required
      rate_limit: 50
  - /public/status:
      methods: !!set [GET]
      auth: none
      rate_limit: 1000

middleware: !!pairs
  - logging: request_logger
  - logging: response_logger
  - auth: jwt_validator
  - auth: permission_checker
  - rate_limit: token_bucket
  - cors: cors_handler

release_notes:
  - version: 2.1.0
    date: !!timestamp 2024-01-27T12:00:00Z
    changes: !!set {performance: ~, security: ~, bugfix: ~}
";

let api_doc = yaml_parse(api_yaml);

print("\nAPI Documentation (v" + api_doc.api_version + ")");
print("\nEndpoints:");
for endpoint in api_doc.endpoints {
    let path = keys(endpoint)[0];
    let config = endpoint[path];
    print("  " + path);
    print("    Methods: " + join(keys(config.methods), ", "));
    print("    Auth: " + config.auth);
    print("    Rate Limit: " + config.rate_limit + " req/min");
}

print("\nMiddleware Pipeline:");
let middleware_order = [];
for mw in api_doc.middleware {
    let mw_type = keys(mw)[0];
    let mw_handler = mw[mw_type];
    push(middleware_order, mw_type + ":" + mw_handler);
}
for i in range(len(middleware_order)) {
    print("  " + (i + 1) + ". " + middleware_order[i]);
}

// ============================================================================
// Example 7: Database Migration with Ordered Steps
// ============================================================================
print("\n--- Example 7: Database Migration ---");

let migration_yaml = "
migration:
  id: 20240127_001
  timestamp: !!timestamp 2024-01-27T14:30:00Z
  
  up: !!omap
    - create_table:
        sql: CREATE TABLE users (id INT, name VARCHAR(255))
    - add_index:
        sql: CREATE INDEX idx_users_name ON users(name)
    - insert_data:
        sql: INSERT INTO users VALUES (1, 'Admin')
    - add_constraint:
        sql: ALTER TABLE users ADD CONSTRAINT pk_users PRIMARY KEY (id)
  
  down: !!omap
    - drop_constraint:
        sql: ALTER TABLE users DROP CONSTRAINT pk_users
    - drop_index:
        sql: DROP INDEX idx_users_name
    - truncate:
        sql: TRUNCATE TABLE users
    - drop_table:
        sql: DROP TABLE users
  
  tags: !!set {schema: ~, users: ~, initial: ~}
";

let migration = yaml_parse(migration_yaml);

print("\nMigration " + migration.migration.id + " (" + migration.migration.timestamp + ")");
print("Tags: " + join(keys(migration.migration.tags), ", "));

print("\nUp Migration Steps:");
for i in range(len(migration.migration.up)) {
    let step = migration.migration.up[i];
    let step_name = keys(step)[0];
    print("  " + (i + 1) + ". " + step_name);
}

print("\nDown Migration Steps:");
for i in range(len(migration.migration.down)) {
    let step = migration.migration.down[i];
    let step_name = keys(step)[0];
    print("  " + (i + 1) + ". " + step_name);
}

// ============================================================================
// Example 8: Game Configuration with Mixed Tags
// ============================================================================
print("\n--- Example 8: Game Level Configuration ---");

let game_yaml = "
level:
  name: 'Desert Temple'
  difficulty: !!omap
    - easy: {enemies: 5, health: 100}
    - medium: {enemies: 10, health: 75}
    - hard: {enemies: 20, health: 50}
  
  powerups: !!set
    speed_boost: ~
    shield: ~
    double_damage: ~
    invincibility: ~
  
  spawn_points: !!pairs
    - enemy: {x: 100, y: 200}
    - enemy: {x: 150, y: 250}
    - enemy: {x: 200, y: 300}
    - powerup: {x: 50, y: 50}
    - powerup: {x: 300, y: 100}
  
  preview_image: !!binary |
    /9j/4AAQSkZJRgABAQEAYABgAAD/2wBD==
  
  created: !!timestamp 2024-01-20T10:00:00Z
  last_played: !!timestamp 2024-01-27T08:30:00Z
";

let game = yaml_parse(game_yaml);

print("\nGame Level: " + game.level.name);
print("Created: " + game.level.created);
print("Available Powerups: " + len(keys(game.level.powerups)));
print("Spawn Points: " + len(game.level.spawn_points));

print("\nDifficulty Settings:");
for difficulty in game.level.difficulty {
    let level = keys(difficulty)[0];
    let settings = difficulty[level];
    print("  " + level + ": " + settings.enemies + " enemies, " + settings.health + " HP");
}

// ============================================================================
print("\n" + "=".repeat(60));
print("✅ All examples completed successfully!");
print("YAML 1.2.2 Specialized Tags are production-ready!");
