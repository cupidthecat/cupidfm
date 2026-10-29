// Test TLS connection to a real HTTPS server

print("Testing tls_connect...");

// Test 1: Connect to a well-known HTTPS endpoint
let sock = await tls_connect("www.google.com", 443);
assert(socket_is_secure(sock), "should be secure");

// Send HTTP request
await socket_send(sock, "GET / HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n");

// Receive response
let response = await socket_recv(sock, 4096);
assert(len(response) > 0, "should receive response");
assert(str_contains(response, "HTTP/1.1"), "should be HTTP response");

// Get TLS info
let info = tls_info(sock);
print("TLS version:", info.version);
print("Cipher:", info.cipher);
assert(str_contains(info.version, "TLS"), "should report TLS version");

socket_close(sock);

print("tls_connect tests passed");
