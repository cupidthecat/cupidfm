// XML parsing and generation demonstration

print("=== XML Parsing Demo ===\n");

// Parse a simple XML document
let simple_xml = "<person name=\"John\" age=\"30\"><city>NYC</city></person>";

print("Parsing XML:");
print(simple_xml);
print("");

let person = xml_parse(simple_xml);
print("Parsed person:");
print("  Name:", person.name);
print("  Attributes:", person.attrs);
if (person.attrs) {
  print("    name:", person.attrs.name);
  print("    age:", person.attrs.age);
}
print("  Children:", person.children);
if (person.children) {
  print("    City:", person.children[0].text);
}

print("\n=== XML Generation Demo ===\n");

// Create an XML document structure
let book = {
  "name": "book",
  "attrs": {
    "isbn": "978-0-123456-78-9",
    "category": "fiction"
  },
  "children": [
    {
      "name": "title",
      "text": "The Great Adventure"
    },
    {
      "name": "author",
      "text": "Jane Smith"
    },
    {
      "name": "year",
      "text": "2024"
    },
    {
      "name": "price",
      "text": "29.99"
    }
  ]
};

print("Generating XML from object:");
let book_xml = xml_stringify(book);
print(book_xml);

print("\n=== Working with Nested Structures ===\n");

// Parse XML with nested structure
let nested_xml = "<library><section name=\"Fiction\"><book>Book1</book><book>Book2</book></section></library>";

print("Parsing nested XML:");
print(nested_xml);
print("");

let library = xml_parse(nested_xml);
print("Library structure:");
print("  Root:", library.name);
print("  Section:", library.children[0].name);
print("  Section name:", library.children[0].attrs.name);
print("  Books:");
for child in library.children[0].children {
  print("    -", child.text);
}

print("\n=== Entity Handling ===\n");

// Parse XML with entities
let entity_xml = "<message>Hello &lt;world&gt; &amp; friends!</message>";

print("XML with entities:");
print(entity_xml);

let message = xml_parse(entity_xml);
print("Decoded text:", message.text);

print("\n=== Roundtrip Test ===\n");

// Create XML, parse it, modify it, stringify again
let original = {
  "name": "config",
  "children": [
    {"name": "host", "text": "localhost"},
    {"name": "port", "text": "8080"},
    {"name": "debug", "text": "true"}
  ]
};

print("Original config:");
let xml1 = xml_stringify(original);
print(xml1);

print("\nParsing back:");
let parsed = xml_parse(xml1);
print("Parsed:", parsed);

print("\nRe-generating:");
let xml2 = xml_stringify(parsed);
print(xml2);

print("\n=== Self-Closing Tags ===\n");

let self_closing = {
  "name": "img",
  "attrs": {
    "src": "photo.jpg",
    "alt": "A photo"
  }
};

print("Self-closing tag:");
let img_xml = xml_stringify(self_closing);
print(img_xml);

print("\n=== CDATA Example ===\n");

let cdata_xml = "<script><![CDATA[if (x < 10) { alert(\"low\"); }]]></script>";

print("XML with CDATA:");
print(cdata_xml);

let script = xml_parse(cdata_xml);
print("Script content:", script.text);

print("\n=== Custom Indentation ===\n");

let sample = {
  "name": "root",
  "children": [
    {"name": "child1", "text": "A"},
    {"name": "child2", "text": "B"}
  ]
};

print("Indent 2 (default):");
print(xml_stringify(sample, 2));

print("\nIndent 4:");
print(xml_stringify(sample, 4));

print("\nCompact (indent 0):");
print(xml_stringify(sample, 0));

print("\nXML demo complete!");
