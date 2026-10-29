// Simpler debug test
print("Testing yaml_parse on simple map...");
let d1 = yaml_parse("a: 1\n");
print("d1.a = " + d1.a);

print("\nTesting yaml_parse on a: 1 then ---...");
let d2 = yaml_parse("a: 1\n---\nb: 2\n");
print("d2 type = " + typeof(d2));
if (typeof(d2) == "map") {
    let ks = keys(d2);
    print("d2 keys = " + ks);
    if (len(ks) > 0) {
        print("d2.a = " + d2.a);
    }
}

print("\nDone");
