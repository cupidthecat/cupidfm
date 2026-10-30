assert(url_build({}) == "", "empty URL map");
let long_path = "/" + str_repeat("x", 12000);
let built = url_build({scheme: "http", host: "localhost", path: long_path, query: "value=1"});
assert(len(built) == len("http://localhost") + len(long_path) + len("?value=1"), "large URL construction");
let parts = url_parse("HTTPS://user:pass@[::1]:8443/path?q=1#frag");
assert(parts.scheme == "https" && parts.host == "::1" && parts.port == 8443, "bracketed host and port");
assert(url_build(parts) == "https://user:pass@[::1]:8443/path?q=1#frag", "IPv6 URL round trip");
