# HTTP and TLS

`http_request` and the HTTP convenience functions use a blocking HTTP/1.x client. `await` accepts the returned value, but does not make this client asynchronous. Requests use IPv4 connections, close after each response, and do not follow redirects or decompress content. There is currently no configurable network timeout or response-size limit; use the socket APIs when the host needs to manage those policies.

The parser accepts content-length, chunked, and connection-close bodies, interim responses, and bodyless HEAD/204/304 responses. It rejects truncated or conflicting framing. Response header keys retain their received spelling. String bodies preserve embedded NUL bytes. Request framing headers are managed by the client; method, URL, and custom headers reject invalid control bytes.

TLS uses the OpenSSL trust store and verifies the requested DNS name or IP address. `CS_NO_TLS=1` removes the TLS socket functions and makes HTTPS fail before connecting. Plain HTTP remains available. TLS and plain sends protect against transport-generated SIGPIPE on Linux; TLS also uses SO_NOSIGPIPE on systems that provide it.

`make test` runs the language/C API cases, parser fragmentation tests, and local HTTP/TLS servers. Python 3 and the OpenSSL command-line tool are test dependencies for TLS builds. These tests do not contact public servers.
