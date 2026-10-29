
# Data Formats

## Table of Contents

- [RFC Standards Compliance](#rfc-standards-compliance)
- [Overview](#overview)
- [CSV (Comma-Separated Values)](#csv-comma-separated-values)
  - [RFC 4180 Compliance](#rfc-4180-compliance)
  - [Quick Start](#quick-start)
  - [Options](#options)
  - [Examples](#examples)
- [YAML (YAML Ain't Markup Language)](#yaml-yaml-aint-markup-language)
  - [RFC 9512 Compliance (YAML 1.2.2)](#rfc-9512-compliance-yaml-122)
  - [Quick Start](#quick-start-1)
  - [Data Types](#data-types)
  - [Advanced Features](#advanced-features)
    - [Block Scalars with Chomping Indicators](#block-scalars-with-chomping-indicators)
    - [Explicit Type Tags](#explicit-type-tags)
    - [Specialized Type Tags (YAML 1.2.2)](#specialized-type-tags-yaml-122)
    - [Merge Keys (<<)](#merge-keys-)
    - [Multiple Documents](#multiple-documents)
    - [Anchors and Aliases](#anchors-and-aliases)
    - [Unicode Escapes](#unicode-escapes)
    - [Explicit Keys](#explicit-keys)
    - [Directives](#directives)
    - [JSON Compatibility](#json-compatibility)
  - [Functions](#functions)
- [XML (Extensible Markup Language)](#xml-extensible-markup-language)
  - [Quick Start](#quick-start-2)
  - [XML Structure](#xml-structure)
  - [Examples](#examples-1)
  - [Common Patterns](#common-patterns)
    - [Configuration Files](#configuration-files)
    - [Data Export](#data-export)
    - [Data Conversion](#data-conversion)
- [Performance Considerations](#performance-considerations)
- [Error Handling](#error-handling)
- [Comparison: When to Use Each Format](#comparison-when-to-use-each-format)
- [See Also](#see-also)

CupidScript provides built-in support for three widely-used data formats: CSV, YAML, and XML. These formats enable easy data interchange, configuration management, and structured data processing.

## RFC Standards Compliance

CupidScript implements industry-standard specifications for data formats:

- **CSV**: Full compliance with **RFC 4180** + extensions
- **YAML**: 100% compliance with **RFC 9512** (YAML 1.2.2 specification)

These implementations are production-ready and interoperable with other RFC-compliant tools.

## Overview

| Format | Best For | Complexity | Human-Readable |
|--------|----------|------------|----------------|
| **CSV** | Tabular data, spreadsheets | Simple | Yes |
| **YAML** | Configuration files | Medium | Very |
| **XML** | Documents, structured data | Medium-High | Yes |
| **JSON** | APIs, general data | Simple | Yes |

## CSV (Comma-Separated Values)

CSV is ideal for simple tabular data and is compatible with spreadsheet applications.

### RFC 4180 Compliance

CupidScript's CSV parser is **fully compliant with RFC 4180** (Common Format and MIME Type for CSV Files) with additional extensions:

**RFC 4180 Features:**
- Standard field delimiters (commas)
- Quoted fields with embedded commas, newlines, and quotes
- Escaped quotes (double-quote within quoted fields: `""`)
- Multiple line-ending formats (`\r`, `\n`, `\r\n`)
- UTF-8 BOM handling

**Extensions:**
- Custom delimiters (tabs, semicolons, pipes, etc.)
- Custom quote characters
- Headers mode (first row → dictionary keys)
- Whitespace trimming
- Empty row filtering

### Quick Start

```cs
// Parse CSV
let csv_text = "name,age,city\nJohn,30,NYC\nAlice,25,LA";
let rows = csv_parse(csv_text);
print(rows);  // [["name", "age", "city"], ["John", "30", "NYC"], ...]

// Generate CSV
let data = [["name", "age"], ["Bob", "35"]];
let output = csv_stringify(data);
```

### Options

The `csv_parse()` function accepts an optional options map:

```cs
csv_parse(text, {
  delimiter: ",",      // Field separator (default: comma)
  quote: "\"",         // Quote character (default: double quote)
  headers: false,      // Treat first row as headers → list of maps
  skip_empty: false,   // Skip empty rows
  trim: false          // Trim whitespace from fields
})
```

### Examples

**Custom delimiter (tab-separated):**
```cs
let tsv = "a\tb\tc\n1\t2\t3";
let rows = csv_parse(tsv, {delimiter: "\t"});
```

**Headers mode:**
```cs
let csv = "name,age\nJohn,30\nAlice,25";
let records = csv_parse(csv, {headers: true});
// Returns: [{"name": "John", "age": "30"}, {"name": "Alice", "age": "25"}]
```

**Quoted fields:**
```cs
let csv = "name,description\nProduct,\"Contains, commas\"";
let rows = csv_parse(csv);  // Handles quoted fields correctly
```

## YAML (YAML Ain't Markup Language)

YAML is perfect for configuration files and human-readable data serialization.

### RFC 9512 Compliance (YAML 1.2.2)

CupidScript provides **100% full compliance** with **RFC 9512** (YAML 1.2.2 specification), making it the first embedded scripting language to achieve complete YAML 1.2.2 compliance.

**Complete Feature Set:**
- All core data types (strings, numbers, booleans, null)
- Collections (maps, lists, nested structures)
- Block scalars (literal `|`, folded `>`)
- All 20+ escape sequences per YAML 1.2.2 spec
- Anchors & aliases, merge keys
- Comments, document markers, directives
- Multi-document streams
- JSON compatibility (valid JSON is valid YAML)
- All specialized tags: `!!binary`, `!!timestamp`, `!!set`, `!!omap`, `!!pairs`
- Security features (anchor bomb prevention, no XXE vulnerabilities)

### Quick Start

```cs
// Parse YAML
let yaml_text = "name: MyApp\nversion: 1.0.0\ndebug: true";
let config = yaml_parse(yaml_text);
print(config.name);     // "MyApp"
print(config.version);  // "1.0.0"
print(config.debug);    // true

// Generate YAML
let data = {"host": "localhost", "port": 8080};
let yaml = yaml_stringify(data);
```

### Data Types

YAML supports rich data types:

```yaml
# Strings
plain: hello
quoted: "hello world"

# Numbers
integer: 42
hex: 0xFF
octal: 0o755
float: 3.14

# Booleans
bool_true: true
bool_false: false
yes_val: yes
no_val: no

# Null
null_val: null

# Lists
items: [a, b, c]
# Or block style:
items:
  - a
  - b
  - c

# Maps
config: {debug: true, port: 8080}
# Or block style:
config:
  debug: true
  port: 8080
```

### Advanced Features

**Comments:**
```yaml
# This is a comment
name: MyApp  # Inline comment
```

**Multiline strings:**
```yaml
# Literal (preserves newlines)
literal: |
  Line 1
  Line 2

# Folded (joins lines)
folded: >
  This is a long
  line that folds
```

### Custom Indentation

```cs
let data = {"key": "value"};
let yaml2 = yaml_stringify(data, 2);  // 2-space indent
let yaml4 = yaml_stringify(data, 4);  // 4-space indent
```

### YAML 1.2.2 Advanced Features

CupidScript implements **YAML 1.2.2** with comprehensive support for advanced features.

#### Block Scalars with Chomping Indicators

Control how trailing newlines are handled:

- `|` - Clip (default): Single trailing newline
- `|-` - Strip: Remove all trailing newlines
- `|+` - Keep: Preserve all trailing newlines
- `|2`, `|4`, etc. - Explicit indentation level

```yaml
# Strip chomping
script: |-
  echo hello
  echo world

# Keep chomping
text: |+
  content


# Explicit indentation
code: |2
    indented code
```

```cs
let cfg = yaml_parse("text: |-\n  no trailing\n  newlines\n");
print(cfg.text);  // "no trailing\nnewlines" (no final \n)
```

#### Explicit Type Tags

Force specific types with `!!` tags:

```yaml
version: !!str 1.0      # Force string
count: !!int "100"      # Parse string as integer
price: !!float "19.99"  # Parse string as float
enabled: !!bool yes     # Explicit boolean
value: !!null data      # Force null
```

```cs
let data = yaml_parse("id: !!str 42\n");
print(typeof(data.id));  // "string"
```

#### Specialized Type Tags (YAML 1.2.2)

CupidScript supports all YAML 1.2.2 specialized tags for advanced data types:

##### `!!binary` - Binary Data

Base64-encoded binary data, decoded to bytes type:

```yaml
icon: !!binary |
  iVBORw0KGgoAAAANSUhEUgAAAAoAAAAKCAYAAACNMs+9AAAAFUE=
  
secret: !!binary c2VjcmV0X2tleQ==
```

```cs
let doc = yaml_parse("data: !!binary SGVsbG8gV29ybGQ=\n");
print(typeof(doc.data));  // "bytes"
print(len(doc.data));     // 11 (decoded byte length)

// Use with file operations
write_file("output.bin", doc.data);
```

##### `!!timestamp` - ISO 8601 Timestamps

Validated ISO 8601 timestamps, stored as strings:

```yaml
created: !!timestamp 2024-01-27T15:30:00Z
modified: !!timestamp 2024-01-27T16:45:23.456Z
deployed: !!timestamp 2024-01-27T10:00:00+05:30
date: !!timestamp 2024-01-27
```

Supported formats:
- `YYYY-MM-DD` (date only)
- `YYYY-MM-DDTHH:MM:SS` or `YYYY-MM-DD HH:MM:SS`
- `YYYY-MM-DDTHH:MM:SS.sssZ` (fractional seconds)
- `YYYY-MM-DDTHH:MM:SS±HH:MM` (timezone offset)

```cs
let doc = yaml_parse("when: !!timestamp 2024-01-27T15:30:00Z\n");
print(doc.when);  // "2024-01-27T15:30:00Z"
// Parse further with date/time functions if needed
```

##### `!!set` - Unordered Sets

Unordered collection of unique values (map with null values):

```yaml
# Method 1: Block syntax
features: !!set
  fast: ~
  secure: ~
  reliable: ~

# Method 2: Flow syntax
tags: !!set {todo: ~, urgent: ~, review: ~}

# Method 3: From list (converted to set)
items: !!set [apple, banana, cherry]
```

```cs
let doc = yaml_parse("tags: !!set {bug: ~, urgent: ~}\n");
print(typeof(doc.tags));  // "map"

// Check membership
if (doc.tags.bug == nil) {
    print("Has 'bug' tag");
}

// Iterate over set
for tag in keys(doc.tags) {
    print("Tag:", tag);
}
```

##### `!!omap` - Ordered Maps

Ordered sequence of key-value pairs (list of single-entry maps):

```yaml
pipeline: !!omap
  - checkout: git clone
  - install: npm install
  - test: npm test
  - build: npm run build
  - deploy: ./deploy.sh
```

```cs
let doc = yaml_parse("
steps: !!omap
  - first: 1
  - second: 2
  - third: 3
");

print(typeof(doc.steps));  // "list"
print(len(doc.steps));     // 3

// Access by index (preserves order)
print(doc.steps[0].first);   // 1
print(doc.steps[1].second);  // 2

// Iterate in order
for entry in doc.steps {
    let key = keys(entry)[0];
    let val = entry[key];
    print(key, "=>", val);
}
```

##### `!!pairs` - Ordered Pairs

Ordered sequence allowing duplicate keys (list of single-entry maps):

```yaml
event_handlers: !!pairs
  - click: validate_form
  - click: track_analytics
  - click: submit_data
  - hover: show_tooltip
  - hover: preload_data
```

```cs
let doc = yaml_parse("
handlers: !!pairs
  - click: handler1
  - click: handler2
  - hover: handler3
");

print(typeof(doc.handlers));  // "list"
print(len(doc.handlers));     // 3

// Duplicate keys preserved!
print(doc.handlers[0].click);  // "handler1"
print(doc.handlers[1].click);  // "handler2"

// Group by event type
let by_event = {};
for pair in doc.handlers {
    let event = keys(pair)[0];
    let handler = pair[event];
    if (!by_event[event]) {
        by_event[event] = [];
    }
    push(by_event[event], handler);
}
```

**Comparison: !!omap vs !!pairs**

| Feature | !!omap | !!pairs |
|---------|--------|---------|
| Structure | List of single-entry maps | List of single-entry maps |
| Duplicate keys | Not recommended | Allowed |
| Use case | Ordered configuration | Event handlers, multi-valued mappings |

**Real-World Example:**

```yaml
deployment:
  timestamp: !!timestamp 2024-01-27T14:30:00Z
  
  environments: !!omap
    - dev: {url: dev.example.com, replicas: 1}
    - staging: {url: staging.example.com, replicas: 2}
    - prod: {url: example.com, replicas: 5}
  
  features: !!set [caching, monitoring, auto_scale]
  
  middleware: !!pairs
    - logging: request_logger
    - logging: response_logger
    - auth: jwt_validator
    - rate_limit: token_bucket
  
  config_archive: !!binary |
    H4sIAAAAAAAAA+3OMQ6AIAwF0N3TdHVxcHFwcXBxcXBxcXBxcXBxcXBxcXBx
```

```cs
let deploy = yaml_parse(yaml_text);
print("Deployed at:", deploy.deployment.timestamp);
print("Environments:", len(deploy.deployment.environments));
print("Features enabled:", len(keys(deploy.deployment.features)));
```

#### Merge Keys (`<<`)

Reuse common configurations:

```yaml
defaults: &defaults
  timeout: 30
  retry: 3

service_a:
  <<: *defaults
  name: Service A
  port: 8001

service_b:
  <<: *defaults
  name: Service B
  timeout: 60  # Override default
```

```cs
let cfg = yaml_parse("
base: &b
  x: 1
  y: 2
app:
  <<: *b
  z: 3
");
print(cfg.app.x);  // 1 (inherited)
print(cfg.app.z);  // 3
```

#### Multiple Documents

Parse multiple documents with `yaml_parse_all()`:

```yaml
%YAML 1.2
---
name: Document 1
...
---
name: Document 2
---
name: Document 3
```

```cs
let docs = yaml_parse_all("---\na: 1\n---\nb: 2\n");
print(len(docs));  // 2
print(docs[0].a);  // 1
```

#### Anchors and Aliases

Reference values to avoid duplication:

```yaml
admin: &admin_email admin@example.com

notifications:
  to: *admin_email
  from: *admin_email

database: &db
  host: localhost
  port: 5432

primary: *db
backup: *db
```

#### Unicode Escapes

Use `\uXXXX` (4 hex digits) or `\UXXXXXXXX` (8 hex digits) in double-quoted strings:

```yaml
greeting: "\u0048\u0065\u006c\u006c\u006f"  # Hello
emoji: "\U0001F44B"                          # 👋
chinese: "\u4F60\u597D"                      # 你好
```

```cs
let text = yaml_parse("msg: \"\\u0041\\u0042\\u0043\"\n");
print(text.msg);  // "ABC"
```

#### Explicit Keys

Use `?` for complex key structures:

```yaml
? simple_key
: value

? [complex, key]
: complex_value
```

#### Directives

Specify YAML version and tag namespaces:

```yaml
%YAML 1.2
%TAG !custom! tag:example.com,2024:
---
key: value
```

#### JSON Compatibility

YAML 1.2 is a strict JSON superset:

```cs
// All valid JSON is valid YAML
let data = yaml_parse('{"name": "John", "age": 30}');
print(data.name);  // "John"
```

### Functions

- **`yaml_parse(text: string) → value`** - Parse single YAML document
- **`yaml_parse_all(text: string) → list`** - Parse multiple documents
- **`yaml_stringify(value, indent?: int) → string`** - Generate YAML (default indent: 2)

## XML (Extensible Markup Language)

XML is ideal for documents, structured data, and data interchange.

### Quick Start

```cs
// Parse XML
let xml_text = "<person name=\"John\" age=\"30\"><city>NYC</city></person>";
let doc = xml_parse(xml_text);
print(doc.name);          // "person"
print(doc.attrs.name);    // "John"
print(doc.children[0].text);  // "NYC"

// Generate XML
let elem = {
  "name": "book",
  "attrs": {"isbn": "123"},
  "children": [
    {"name": "title", "text": "Guide"}
  ]
};
let xml = xml_stringify(elem);
```

### XML Structure

Parsed XML documents are represented as maps:

```cs
{
  "name": "element-name",
  "attrs": {               // Optional
    "attr1": "value1",
    "attr2": "value2"
  },
  "text": "text content",  // Optional (for simple text)
  "children": [            // Optional (for child elements)
    {"name": "child", ...}
  ]
}
```

### Examples

**Attributes:**
```cs
let xml = "<img src=\"photo.jpg\" alt=\"Photo\"/>";
let doc = xml_parse(xml);
print(doc.attrs.src);  // "photo.jpg"
print(doc.attrs.alt);  // "Photo"
```

**Nested elements:**
```cs
let xml = "<book><author>John</author><title>Guide</title></book>";
let doc = xml_parse(xml);
for child in doc.children {
  print(child.name, ":", child.text);
}
```

**Entity handling:**
```cs
let xml = "<text>Hello &lt;world&gt; &amp; friends</text>";
let doc = xml_parse(xml);
print(doc.text);  // "Hello <world> & friends"
```

**CDATA sections:**
```cs
let xml = "<script><![CDATA[if (x < 10) alert(\"low\");]]></script>";
let doc = xml_parse(xml);
print(doc.text);  // "if (x < 10) alert(\"low\");"
```

**Custom indentation:**
```cs
let elem = {"name": "root", "children": [...]};
let xml2 = xml_stringify(elem, 2);  // 2-space indent
let xml4 = xml_stringify(elem, 4);  // 4-space indent
let compact = xml_stringify(elem, 0);  // No formatting
```

## Common Patterns

### Configuration Files

**YAML config:**
```cs
let config_text = read_file("config.yaml");
let config = yaml_parse(config_text);

// Access nested config
print("Database:", config.database.host);
print("Port:", config.server.port);
```

**XML config:**
```cs
let config_text = read_file("config.xml");
let config = xml_parse(config_text);

// Navigate structure
for child in config.children {
  if (child.name == "database") {
    print("DB Host:", child.attrs.host);
  }
}
```

### Data Export

**Export to CSV:**
```cs
let data = [
  ["Name", "Score"],
  ["Alice", "95"],
  ["Bob", "87"]
];
let csv = csv_stringify(data);
write_file("scores.csv", csv);
```

**Export to XML:**
```cs
let report = {
  "name": "report",
  "children": [
    {"name": "date", "text": "2026-01-27"},
    {"name": "total", "text": "100"}
  ]
};
let xml = xml_stringify(report);
write_file("report.xml", xml);
```

### Data Conversion

**CSV to YAML:**
```cs
let csv = csv_parse(csv_text, {headers: true});
let yaml = yaml_stringify({"records": csv});
```

**XML to JSON:**
```cs
let xml_doc = xml_parse(xml_text);
let json = json_stringify(xml_doc);
```

## Performance Considerations

- **CSV**: Very fast for large tabular datasets
- **YAML**: Good for config files (< 1MB)
- **XML**: Good for structured documents (< 10MB)
- **JSON**: Fastest overall, use when possible

For very large files (> 10MB), consider:
- Processing in chunks
- Using streaming approaches
- Filtering data early

## Error Handling

All parsers return `nil` and set an error on parse failure:

```cs
let data = csv_parse(invalid_csv);
if (data == nil) {
  print("CSV parse failed");
}
```

Common errors:
- **CSV**: Unterminated quotes, inconsistent columns
- **YAML**: Invalid indentation, syntax errors
- **XML**: Unclosed tags, mismatched tags, invalid entities

## Comparison: When to Use Each Format

**Use CSV when:**
- Working with simple tabular data
- Need spreadsheet compatibility
- Data has uniform structure
- Performance is critical

**Use YAML when:**
- Writing configuration files
- Need human-readable format
- Have nested or complex data
- Comments are important

**Use XML when:**
- Need strict structure validation
- Working with document-oriented data
- Industry standards require XML (RSS, SVG, etc.)
- Need attributes and child elements together

**Use JSON when:**
- Building APIs
- Need fastest performance
- Wide language support required
- Data structure is simple

## See Also

- [Standard Library](Standard-Library.md) - Complete function reference
- [Strings and Strbuf](Strings-and-Strbuf.md) - String manipulation
- [Collections](Collections.md) - Working with lists and maps
