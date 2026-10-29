// HTTPS test - requires network access and TLS support

let resp = await http_get("https://httpbin.org/get");
assert(resp.status == 200, "HTTPS status should be 200");
assert(len(resp.body) > 0, "body should not be empty");
print("https_get test passed");

// Test with custom headers
let resp2 = await http_request({
    url: "https://httpbin.org/headers",
    headers: {"X-Custom-Header": "test-value"}
});
assert(resp2.status == 200, "custom header request should succeed");
assert(str_contains(resp2.body, "X-Custom-Header"), "should echo our header");
print("https with custom headers test passed");
