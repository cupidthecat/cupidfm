#!/usr/bin/env python3
"""Exercise HTTP/TLS against local servers, with no external service dependency."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import unittest

arguments = argparse.ArgumentParser()
arguments.add_argument("--bin", type=Path, default=Path(__file__).resolve().parents[1] / "bin/cupidscript")
arguments.add_argument("--no-tls", action="store_true")
options, remaining = arguments.parse_known_args()
BINARY = options.bin.resolve()

class Http(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="cupidscript-http-")
        cls.root = Path(cls.temporary.name)
        cls.certificate = cls.root / "certificate.pem"
        if not options.no_tls:
            import ssl
            key = cls.root / "key.pem"
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                            "-keyout", str(key), "-out", str(cls.certificate)], check=True, capture_output=True)
            cls.tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            cls.tls.load_cert_chain(cls.certificate, key)
    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()
    def execute(self, source, success=True, env=None):
        path = self.root / "request.cs"; path.write_text(source)
        environment = dict(os.environ)
        if not options.no_tls: environment["SSL_CERT_FILE"] = str(self.certificate)
        if env: environment.update(env)
        result = subprocess.run([str(BINARY), str(path)], env=environment, capture_output=True, text=True, timeout=15)
        self.assertGreaterEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("AddressSanitizer", result.stderr)
        self.assertNotIn("LeakSanitizer", result.stderr)
        self.assertNotIn("runtime error:", result.stderr)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result
    @contextmanager
    def server(self, response=b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", tls=False, fragment=False, reset=False, raw=False):
        listener = socket.socket(); listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", 0)); listener.listen(1); listener.settimeout(5)
        result = {}; errors = []
        def serve():
            try:
                connection, _ = listener.accept(); connection.settimeout(5)
                if reset:
                    connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0)); connection.close(); return
                if tls: connection = self.tls.wrap_socket(connection, server_side=True)
                with connection:
                    request = bytearray()
                    if raw:
                        while len(request) < 3:
                            part = connection.recv(3-len(request))
                            if not part: break
                            request.extend(part)
                        result["raw"] = bytes(request); return
                    while b"\r\n\r\n" not in request:
                        part = connection.recv(4096)
                        if not part: return
                        request.extend(part)
                        if len(request) > 1 << 20: raise AssertionError("test request exceeded limit")
                    head, body = bytes(request).split(b"\r\n\r\n", 1)
                    headers = {}
                    for line in head.split(b"\r\n")[1:]:
                        name, value = line.split(b":", 1); headers[name.lower()] = value.strip()
                    length = int(headers.get(b"content-length", b"0"))
                    while len(body) < length:
                        part = connection.recv(min(65536, length-len(body)))
                        if not part: break
                        body += part
                    result.update(head=head, body=body, headers=headers)
                    if fragment:
                        for byte in response: connection.sendall(bytes([byte]))
                    else: connection.sendall(response)
                    if tls:
                        try: connection.unwrap().close()
                        except OSError: pass
            except (OSError, ValueError) as error:
                # A negative TLS test or an intentionally rejected response can close early.
                result["transport_error"] = str(error)
            except BaseException as error: errors.append(error)
            finally: listener.close()
        thread = threading.Thread(target=serve, daemon=True); thread.start()
        try: yield listener.getsockname()[1], result
        finally:
            thread.join(7)
            self.assertFalse(thread.is_alive(), "loopback server did not finish")
            self.assertEqual(errors, [])
    def test_content_length_and_close_delimited(self):
        for response in (b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello",
                         b"HTTP/1.0 200 OK\r\n\r\nhello"):
            with self.server(response) as (port, request):
                self.execute(f'let r=await http_get("http://127.0.0.1:{port}/"); assert(r.status==200 && r.body=="hello", "response");')
            self.assertEqual(request["headers"][b"host"], f"127.0.0.1:{port}".encode())
    def test_chunked_split_trailers_and_informational(self):
        response = (b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                    b"2;name=value\r\nok\r\n0\r\nX-Trailer: value\r\n\r\n")
        with self.server(response, fragment=True) as (port, _):
            self.execute(f'let r=http_get("http://127.0.0.1:{port}/"); assert(r.body=="ok" && r.headers["X-Trailer"]=="value", "chunked response");')
    def test_large_request_target_and_headers(self):
        with self.server() as (port, request):
            self.execute(f'let r=http_request({{url:"http://127.0.0.1:{port}/"+str_repeat("p",6000), headers:{{"X-Long":str_repeat("v",12000)}}}}); assert(r.status==200,"large request");')
        self.assertEqual(len(request["head"].split(b"\r\n")[0]), len(b"GET / HTTP/1.1")+6000)
        self.assertEqual(len(request["headers"][b"x-long"]), 12000)
    def test_binary_request_and_response(self):
        response = b"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\na\0b"
        with self.server(response) as (port, request):
            self.execute(f'let r=http_post("http://127.0.0.1:{port}/", "a\\0b"); assert(len(r.body)==3 && r.body=="a\\0b", "binary body");')
        self.assertEqual(request["body"], b"a\0b")
    def test_head_and_no_content(self):
        for method, status in (("HEAD", 200), ("GET", 204), ("GET", 304)):
            response = f"HTTP/1.1 {status} Status\r\nContent-Length: 999\r\n\r\n".encode()
            with self.server(response) as (port, _):
                self.execute(f'let r=http_request({{url:"http://127.0.0.1:{port}/",method:"{method}"}}); assert(r.status=={status} && len(r.body)==0,"bodyless response");')
    def test_malformed_and_truncated_responses(self):
        for response in (b"HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nshort",
                         b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nG\r\n",
                         b"HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nx"):
            with self.server(response) as (port, _): self.execute(f'http_get("http://127.0.0.1:{port}/");', False)
    def test_validation_errors_release_owned_values(self):
        for options_text in ('{url:"ftp://127.0.0.1/"}', '{url:"http://127.0.0.1:70000/"}',
                             '{url:"http://127.0.0.1:bad/"}', '{url:"http://127.0.0.1:1/",headers:{"X-Test":"bad\\r\\nInjected: yes"}}',
                             '{url:"http://127.0.0.1:1/",headers:{"Content-Length":"1"},body:"x"}'):
            self.execute('http_request('+options_text+');', False)
    def test_peer_reset_does_not_signal_process(self):
        with self.server(reset=True) as (port, _):
            self.execute(f'http_post("http://127.0.0.1:{port}/",str_repeat("x",16000000));', False)
    def test_socket_send_preserves_embedded_nul(self):
        with self.server(raw=True) as (port, received):
            self.execute(f'let s=await tcp_connect("127.0.0.1",{port}); let n=await socket_send(s,"a\\0b"); assert(n==3,"binary socket write"); socket_close(s);')
        self.assertEqual(received["raw"], b"a\0b")
    def test_tls_or_explicitly_disabled(self):
        if options.no_tls:
            result = self.execute('http_get("https://127.0.0.1:1/");', False)
            self.assertIn("TLS disabled", result.stderr)
            return
        with self.server(tls=True) as (port, _):
            self.execute(f'let r=http_get("https://localhost:{port}/"); assert(r.status==200 && r.body=="ok","verified TLS");')
        with self.server(tls=True) as (port, _):
            self.execute(f'http_get("https://127.0.0.1:{port}/");', False)
        for upgrade in (False, True):
            with self.server(tls=True) as (port, _):
                connect = f'let s=await tcp_connect("localhost",{port}); await tls_upgrade(s);' if upgrade else f'let s=await tls_connect("localhost",{port});'
                self.execute(connect + 'assert(socket_is_secure(s),"secure socket"); assert(str_contains(tls_info(s).version,"TLS"),"TLS info"); await socket_send(s,"GET / HTTP/1.1\\r\\nHost: localhost\\r\\nConnection: close\\r\\n\\r\\n"); let response=await socket_recv(s,4096); assert(len(response)>0,"TLS response"); socket_close(s);')

if __name__ == "__main__": unittest.main(argv=[__file__, *remaining])
