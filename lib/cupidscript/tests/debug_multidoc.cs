// Debug multi-doc parsing step by step
print("Test 1: Single doc without ---");
let d1 = yaml_parse("a: 1\n");
print("d1.a = " + d1.a);
assert(d1.a == 1, "single doc");
print("PASS");

print("\nTest 2: Single doc with ---");
let d2 = yaml_parse("---\na: 2\n");
print("d2 type = " + typeof(d2));
if (typeof(d2) == "map") {
    print("d2.a = " + d2.a);
} else {
    print("d2 = " + d2);
}

print("\nTest 3: yaml_parse_all with single doc");
let docs3 = yaml_parse_all("a: 1\n");
print("len(docs3) = " + len(docs3));
if (len(docs3) > 0) {
    print("docs3[0] type = " + typeof(docs3[0]));
    if (typeof(docs3[0]) == "map") {
        print("docs3[0].a = " + docs3[0].a);
    }
}

print("\nTest 4: yaml_parse_all with ---");
let docs4 = yaml_parse_all("---\na: 1\n");
print("len(docs4) = " + len(docs4));
if (len(docs4) > 0) {
    print("docs4[0] type = " + typeof(docs4[0]));
    if (typeof(docs4[0]) == "map") {
        print("docs4[0].a = " + docs4[0].a);
    } else {
        print("docs4[0] = " + docs4[0]);
    }
}

print("\nTest 5: yaml_parse_all with two docs");
let docs5 = yaml_parse_all("---\na: 1\n---\nb: 2\n");
print("len(docs5) = " + len(docs5));
for i in range(len(docs5)) {
    print("docs5[" + i + "] type = " + typeof(docs5[i]));
    if (typeof(docs5[i]) == "map") {
        let ks = keys(docs5[i]);
        print("  keys: " + ks);
    } else {
        print("  value: " + docs5[i]);
    }
}

print("\nDone");
