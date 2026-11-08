# SPDX-License-Identifier: MS-PL
"""Local TLS/WebSocket fixture for CnaServiceTransportLifetimeTests (no devices).

The wire oracle is HTTP/WebSocket framing (RFC 6455 section 4.2.2 and section 5),
plus the minimal CNA v1 hello/authentication/events protocol. No service process,
account store, external network, renderer, or audio device is used.
"""
import argparse
import base64
import hashlib
import http.server
import json
import os
import pathlib
import ssl
import subprocess
import tempfile
import threading
import time


def exact(stream, count):
    result = stream.read(count)
    if len(result) != count:
        raise RuntimeError("truncated WebSocket frame")
    return result


class Endpoint(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def do_POST(self):
        request = json.loads(exact(self.rfile, int(self.headers["Content-Length"])))
        now = int(time.time())
        if request["op"] == "hello":
            result = dict(version=1, maxMessageBytes=65536,
                          capabilities=["identity", "authentication", "achievements", "events"])
        elif request["op"] == "auth.login":
            result = dict(token="a" * 64, expires=now + 3600, serverTime=now,
                          identity=dict(userId="u1", gamertag="Alice", displayName="Alice",
                                        motto="", region="US", picture="", gamerScore=0,
                                        totalAchievements=0, allowOnlineSessions=True))
        else:
            raise RuntimeError("unexpected control operation: " + request["op"])
        reply = json.dumps(dict(v=1, id=request["id"], error="OK", result=result)).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)

    def do_GET(self):
        assert self.path == "/cna/v1/events", self.path
        with self.server.guard:
            self.server.attempts += 1
            attempt = self.server.attempts
        mode = self.server.mode
        if mode == "rejected" or (mode == "retry" and attempt == 1):
            self.send_response(503)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            if mode == "rejected":
                self.server.reached.set()
            return
        if mode == "pending":
            # Hold the HTTP upgrade pending while CNA returns from main. The
            # event thread must finish its timed transfer and clean up first.
            self.server.reached.set()
        else:
            key = self.headers["Sec-WebSocket-Key"]
            digest = hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", base64.b64encode(digest).decode())
            self.end_headers()
            opcode, size = exact(self.rfile, 2)
            assert opcode == 0x81 and size & 0x80, "masked client text frame"
            size &= 0x7f
            if size == 126:
                size = int.from_bytes(exact(self.rfile, 2), "big")
            assert size <= 4096
            mask = exact(self.rfile, 4)
            message = exact(self.rfile, size)
            hello = json.loads(bytes(value ^ mask[index % 4] for index, value in enumerate(message)))
            assert hello == dict(v=1, id="events", game="net-lsan", token="a" * 64), hello
            welcome = b'{"v":1,"error":"OK"}'
            self.wfile.write(bytes([0x81, len(welcome)]) + welcome)
            self.wfile.flush()
            self.server.reached.set()
        # Observe the connection close from the real event easy-handle cleanup.
        while self.rfile.read(1):
            pass
        self.server.closed.set()
        self.close_connection = True


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, *_):
        import traceback
        self.errors.append(traceback.format_exc())


def run(binary, root, cert, key, mode):
    server = Server(("127.0.0.1", 0), Endpoint)
    server.mode, server.attempts = mode, 0
    server.guard = threading.Lock()
    server.reached, server.closed = threading.Event(), threading.Event()
    server.errors = []
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    env = os.environ.copy()
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_ASSERT="abort",
               CNA_GAMER_SERVICES_ENDPOINT=f"https://localhost:{server.server_port}/cna/v1",
               CNA_GAME_ID="net-lsan", CNA_GAMER_SERVICES_CA_BUNDLE=str(cert),
               CNA_GAMER_SERVICES_CREDENTIALS_DIR="0", CNA_GAMER_SERVICES_EVENTS="1",
               CNA_GAMER_SERVICES_INSECURE_LOOPBACK="0")
    env["ASAN_OPTIONS"] = env.get("ASAN_OPTIONS", "") + ":detect_leaks=1"
    process = subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, env=env, cwd=root)
    try:
        assert server.reached.wait(25), f"events endpoint not reached ({mode}): {server.errors}"
        output, error = process.communicate("stop\n", timeout=15)
        print(output, end="")
        print(error, end="")
        assert process.returncode == 0, f"{mode}: exit {process.returncode}"
        assert not server.errors, server.errors
        if mode != "rejected":
            assert server.closed.wait(5), f"event connection not closed ({mode})"
        assert "backend and worker stopped before OpenSSL cleanup" in error
        print(f"{mode}: event attempts={server.attempts}; clean process exit")
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()
        server.shutdown()
        server.server_close()
        thread.join()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=pathlib.Path)
    parser.add_argument("--scratch", required=True, type=pathlib.Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="events-tls-", dir=args.scratch) as temp:
        root = pathlib.Path(temp)
        cert, key = root / "cert.pem", root / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                        "-keyout", str(key), "-out", str(cert)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for mode in ("connected", "rejected", "pending", "retry"):
            run(args.binary.resolve(), root, cert, key, mode)


if __name__ == "__main__":
    main()
