let test_url=getenv("CUPIDSCRIPT_HTTPS_TEST_URL");
if (test_url != nil) {
// HTTPS test - requires network access and TLS support

let resp = await http_get(test_url);
assert(resp.status == 200, "HTTPS status should be 200");
assert(len(resp.body) > 0, "body should not be empty");
print("https_get test passed");

// Test with custom headers
let resp2 = await http_request({
    url: test_url,
    headers: {"X-Custom-Header": "test-value"}
});
assert(resp2.status == 200, "custom header request should succeed");
assert(str_contains(resp2.body, "X-Custom-Header"), "should echo our header");
print("https with custom headers test passed");

} else { print("Loopback HTTP coverage runs in tests/test_http.py"); }
