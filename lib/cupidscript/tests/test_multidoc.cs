// Debug multi-doc parsing
let docs = yaml_parse_all("---\na: 1\n---\nb: 2\n");
print("Number of documents: " + len(docs));
print("Type of docs: " + typeof(docs));

if (len(docs) >= 1) {
    print("docs[0] type: " + typeof(docs[0]));
    if (typeof(docs[0]) == "map") {
        print("docs[0].a = " + docs[0].a);
    } else {
        print("docs[0] value: " + docs[0]);
    }
}

if (len(docs) >= 2) {
    print("docs[1] type: " + typeof(docs[1]));
    if (typeof(docs[1]) == "map") {
        print("docs[1].b = " + docs[1].b);
    } else {
        print("docs[1] value: " + docs[1]);
    }
}
