// HTTP test - requires network access

let resp = await http_get("http://httpbin.org/get");
assert(resp.status == 200, "HTTP status should be 200");
assert(len(resp.body) > 0, "body should not be empty");
print("http_get test passed");
