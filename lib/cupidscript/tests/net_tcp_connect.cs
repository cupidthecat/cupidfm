// Test tcp_connect - test error handling (connection tests can timeout)

// Test: Invalid host should error quickly
let bad_host_error = false;
try {
    let sock = await tcp_connect("this-host-does-not-exist-12345.invalid", 80);
    socket_close(sock);
} catch (e) {
    bad_host_error = true;
    print("Invalid host error (expected):", e);
}
assert(bad_host_error, "should error on invalid host");

print("tcp_connect tests passed");
