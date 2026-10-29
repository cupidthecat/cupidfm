// HTTP/HTTPS client example

print("Fetching via HTTPS...");
let resp = await http_get("https://httpbin.org/get");

print("Status:", resp.status, resp.status_text);
print("Headers:");
for k in keys(resp.headers) {
    print("  ", k, ":", mget(resp.headers, k));
}
print("Body:", substr(resp.body, 0, 200), "...");

// TLS socket example
print("\nDirect TLS connection:");
let sock = await tls_connect("www.google.com", 443);
print("Secure:", socket_is_secure(sock));
let info = tls_info(sock);
print("TLS Version:", info.version);
print("Cipher:", info.cipher);
socket_close(sock);
