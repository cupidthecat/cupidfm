let u = url_parse("http://user:pass@example.com:8080/path?q=1#frag");
assert(u.scheme == "http", "scheme");
assert(u.user == "user", "user");
assert(u.pass == "pass", "pass");
assert(u.host == "example.com", "host");
assert(u.port == 8080, "port");
assert(u.path == "/path", "path");
assert(u.query == "q=1", "query");
assert(u.fragment == "frag", "fragment");

let simple = url_parse("http://example.com/");
assert(simple.host == "example.com", "simple host");
assert(simple.port == 80, "default port");

let rebuilt = url_build({scheme: "http", host: "test.com", path: "/api", query: "x=1"});
assert(rebuilt == "http://test.com/api?x=1", "url_build");

print("url_parse tests passed");
