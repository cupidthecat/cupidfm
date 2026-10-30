let test_url=getenv("CUPIDSCRIPT_HTTP_TEST_URL");
if (test_url != nil) {
// HTTP test - requires network access

let resp = await http_get(test_url);
assert(resp.status == 200, "HTTP status should be 200");
assert(len(resp.body) > 0, "body should not be empty");
print("http_get test passed");

} else { print("Loopback HTTP coverage runs in tests/test_http.py"); }
