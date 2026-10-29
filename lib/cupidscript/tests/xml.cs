// XML parsing and stringify tests

// Test 1: Simple element with text
let xml1 = "<root>Hello</root>";
let d1 = xml_parse(xml1);
assert(d1.name == "root", "simple element name");
assert(d1.text == "Hello", "simple element text");

// Test 2: Element with single attribute
let xml2 = "<person name=\"John\"></person>";
let d2 = xml_parse(xml2);
assert(d2.name == "person", "element with attr name");
assert(d2.attrs.name == "John", "attribute value");

// Test 3: Element with multiple attributes
let xml3 = "<person name=\"Alice\" age=\"25\" city=\"NYC\"></person>";
let d3 = xml_parse(xml3);
assert(d3.name == "person", "multi-attr element name");
assert(d3.attrs.name == "Alice", "first attribute");
assert(d3.attrs.age == "25", "second attribute");
assert(d3.attrs.city == "NYC", "third attribute");

// Test 4: Self-closing tag
let xml4 = "<br/>";
let d4 = xml_parse(xml4);
assert(d4.name == "br", "self-closing tag name");

// Test 5: Self-closing tag with attributes
let xml5 = "<img src=\"pic.jpg\" alt=\"Picture\"/>";
let d5 = xml_parse(xml5);
assert(d5.name == "img", "self-closing with attrs name");
assert(d5.attrs.src == "pic.jpg", "self-closing attr 1");
assert(d5.attrs.alt == "Picture", "self-closing attr 2");

// Test 6: Nested elements
let xml6 = "<root><child>Content</child></root>";
let d6 = xml_parse(xml6);
assert(d6.name == "root", "nested element parent name");
assert(typeof(d6.children) == "list", "nested element has children list");
assert(len(d6.children) == 1, "nested element child count");
assert(d6.children[0].name == "child", "nested child name");
assert(d6.children[0].text == "Content", "nested child text");

// Test 7: Multiple children
let xml7 = "<list><item>A</item><item>B</item><item>C</item></list>";
let d7 = xml_parse(xml7);
assert(d7.name == "list", "multiple children parent");
assert(len(d7.children) == 3, "multiple children count");
assert(d7.children[0].text == "A", "first child");
assert(d7.children[1].text == "B", "second child");
assert(d7.children[2].text == "C", "third child");

// Test 8: Entity decoding - predefined
let xml8 = "<text>&lt;tag&gt; &amp; &quot;quotes&quot; &apos;apos&apos;</text>";
let d8 = xml_parse(xml8);
assert(contains(d8.text, "<"), "entity lt");
assert(contains(d8.text, ">"), "entity gt");
assert(contains(d8.text, "&"), "entity amp");
assert(contains(d8.text, "\""), "entity quot");

// Test 9: Numeric entities
let xml9 = "<text>&#65;&#66;&#67;</text>";
let d9 = xml_parse(xml9);
assert(contains(d9.text, "A"), "numeric entity decimal");

// Test 10: CDATA section
let xml10 = "<script><![CDATA[if (x < 10) alert(\"low\");]]></script>";
let d10 = xml_parse(xml10);
assert(d10.name == "script", "cdata element name");
assert(contains(d10.text, "if (x < 10)"), "cdata content");

// Test 11: Comments (are skipped)
let xml11 = "<!-- comment --><root>Text</root>";
let d11 = xml_parse(xml11);
assert(d11.name == "root", "element after comment");
assert(d11.text == "Text", "text after comment");

// Test 12: XML declaration
let xml12 = "<?xml version=\"1.0\"?><root>Content</root>";
let d12 = xml_parse(xml12);
assert(d12.name == "root", "element after xml decl");
assert(d12.text == "Content", "content after xml decl");

// Test 13: Empty element
let xml13 = "<div></div>";
let d13 = xml_parse(xml13);
assert(d13.name == "div", "empty element name");

// Test 14: Whitespace handling
let xml14 = "  <root>  Text  </root>  ";
let d14 = xml_parse(xml14);
assert(d14.name == "root", "element with whitespace");
assert(contains(d14.text, "Text"), "text preserved");

// Test 15: Stringify simple element
let elem15 = {"name": "root", "text": "Hello"};
let xml15 = xml_stringify(elem15);
assert(typeof(xml15) == "string", "stringify returns string");
assert(contains(xml15, "<root>"), "stringify opening tag");
assert(contains(xml15, "Hello"), "stringify text");
assert(contains(xml15, "</root>"), "stringify closing tag");

// Test 16: Stringify with attributes
let elem16 = {"name": "person", "attrs": {"name": "John", "age": "30"}};
let xml16 = xml_stringify(elem16);
assert(contains(xml16, "<person"), "stringify element with attrs");
assert(contains(xml16, "name="), "stringify attribute name");
assert(contains(xml16, "John"), "stringify attribute value");

// Test 17: Stringify self-closing
let elem17 = {"name": "br"};
let xml17 = xml_stringify(elem17);
assert(contains(xml17, "<br/>"), "stringify self-closing");

// Test 18: Stringify with children
let elem18 = {
  "name": "root",
  "children": [
    {"name": "child1", "text": "A"},
    {"name": "child2", "text": "B"}
  ]
};
let xml18 = xml_stringify(elem18);
assert(contains(xml18, "<child1>"), "stringify child 1");
assert(contains(xml18, "<child2>"), "stringify child 2");

// Test 19: Entity escaping in text
let elem19 = {"name": "text", "text": "a < b & c > d"};
let xml19 = xml_stringify(elem19);
assert(contains(xml19, "&lt;"), "entity escape lt");
assert(contains(xml19, "&gt;"), "entity escape gt");
assert(contains(xml19, "&amp;"), "entity escape amp");

// Test 20: Entity escaping in attributes
let elem20 = {"name": "tag", "attrs": {"value": "a<b&c>d"}};
let xml20 = xml_stringify(elem20);
assert(contains(xml20, "&lt;"), "attr entity lt");
assert(contains(xml20, "&gt;"), "attr entity gt");
assert(contains(xml20, "&amp;"), "attr entity amp");

// Test 21: Roundtrip simple
let orig21 = "<root>Test</root>";
let parsed21 = xml_parse(orig21);
let back21 = xml_stringify(parsed21);
let reparsed21 = xml_parse(back21);
assert(reparsed21.name == "root", "roundtrip name");
assert(reparsed21.text == "Test", "roundtrip text");

// Test 22: Roundtrip with attributes
let orig22 = "<person name=\"Alice\" age=\"25\"></person>";
let parsed22 = xml_parse(orig22);
let back22 = xml_stringify(parsed22);
let reparsed22 = xml_parse(back22);
assert(reparsed22.name == "person", "roundtrip attrs name");
assert(reparsed22.attrs.name == "Alice", "roundtrip attrs value");

// Test 23: Stringify custom indent
let elem23 = {"name": "root", "text": "Test"};
let xml23a = xml_stringify(elem23, 2);
let xml23b = xml_stringify(elem23, 4);
assert(typeof(xml23a) == "string", "custom indent 2");
assert(typeof(xml23b) == "string", "custom indent 4");

// Test 24: Namespaced elements
let xml24 = "<ns:element xmlns:ns=\"http://example.com\">Content</ns:element>";
let d24 = xml_parse(xml24);
assert(d24.name == "ns:element", "namespaced element");
assert(d24.text == "Content", "namespaced content");

// Test 25: Deeply nested
let xml25 = "<a><b><c><d>Deep</d></c></b></a>";
let d25 = xml_parse(xml25);
assert(d25.name == "a", "deep nest level 1");
assert(d25.children[0].name == "b", "deep nest level 2");
assert(d25.children[0].children[0].name == "c", "deep nest level 3");
assert(d25.children[0].children[0].children[0].name == "d", "deep nest level 4");
assert(d25.children[0].children[0].children[0].text == "Deep", "deep nest text");

print("All XML tests passed!");
