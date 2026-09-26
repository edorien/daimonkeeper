"""Newline-delimited JSON client for keeperfx's in-game TCP API (API_ENABLED=TRUE, API_PORT). Standard library only."""
import json
import socket
import time


class ApiError(Exception):
    """The game answered success=false. `code` is the stable error code (NOT_AVAILABLE, STALE_VIEW, ...)."""

    def __init__(self, code, request):
        super().__init__("%s (request: %s)" % (code, request))
        self.code = code
        self.request = request


class Api:
    def __init__(self, host="127.0.0.1", port=5599):
        self.host, self.port = host, port
        self.sock = None
        self.buf = b""
        self.ack = 0
        self.bytes_received = 0

    def connect(self, timeout=60.0):
        deadline = time.time() + timeout
        while True:
            try:
                self.sock = socket.create_connection((self.host, self.port), timeout=10)
                self.buf = b""
                return
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.25)

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def call(self, **req):
        """Send one command; return the whole response dict (check ["success"] yourself)."""
        self.ack += 1
        req["ack"] = self.ack
        self.sock.sendall((json.dumps(req) + "\n").encode())
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("the game closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        self.bytes_received += len(line) + 1
        resp = json.loads(line)
        if resp.get("ack") != self.ack:
            raise ConnectionError("response does not match the request: %r" % resp)
        return resp

    def data(self, **req):
        """Like call(), but return the `data` payload and raise ApiError on failure."""
        r = self.call(**req)
        if not r.get("success"):
            raise ApiError(r.get("error", "UNKNOWN"), req)
        return r.get("data")
