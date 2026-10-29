let single = yaml_parse("a: 1\nb: 2");
print("single = " + single);
print("single.a = " + single.a);

let docs = yaml_parse_all("---\na: 1\n---\nb: 2\n");
print("len = " + len(docs));
print("doc0 = " + docs[0]);
print("doc0.a = " + docs[0].a);
print("doc1 = " + docs[1]);
print("doc1.b = " + docs[1].b);
