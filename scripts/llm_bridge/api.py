"""Newline-delimited JSON client for keeperfx's in-game TCP API (API_ENABLED=TRUE, API_PORT). Standard library only."""
import collections
import json
import select
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
        self.events = collections.deque()   # pushed events ({"event": ..., "data": ...}) that arrived while waiting for a reply

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
        while True:
            line = self._read_line(None)
            resp = json.loads(line)
            if "event" in resp and "ack" not in resp:
                self.events.append(resp)      # the game pushed something: keep it for wait_event(), keep waiting for our reply
                continue
            if resp.get("ack") != self.ack:
                raise ConnectionError("response does not match the request: %r" % resp)
            return resp

    def _read_line(self, timeout):
        """One newline-terminated line from the socket; None if `timeout` seconds pass first (timeout=None waits)."""
        deadline = None if timeout is None else time.time() + timeout
        while b"\n" not in self.buf:
            wait = None if deadline is None else max(0.0, deadline - time.time())
            if wait is not None and not select.select([self.sock], [], [], wait)[0]:
                return None
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("the game closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        self.bytes_received += len(line) + 1
        return line

    def wait_event(self, name, timeout):
        """The next pushed event called `name` (its whole dict), or None after `timeout` seconds. Other events are discarded."""
        deadline = time.time() + timeout
        while True:
            while self.events:
                ev = self.events.popleft()
                if ev.get("event") == name:
                    return ev
            line = self._read_line(max(0.0, deadline - time.time()))
            if line is None:
                return None
            resp = json.loads(line)
            if "event" in resp and "ack" not in resp:
                self.events.append(resp)

    def drain_events(self, name):
        """Pushed events called `name` that are already here (without waiting), oldest first."""
        out = []
        while True:
            line = self._read_line(0.0) if (b"\n" in self.buf or select.select([self.sock], [], [], 0)[0]) else None
            if line is None:
                break
            resp = json.loads(line)
            if "event" in resp and "ack" not in resp:
                self.events.append(resp)
        keep = collections.deque()
        for ev in self.events:
            (out if ev.get("event") == name else keep).append(ev)
        self.events = keep
        return out

    def data(self, **req):
        """Like call(), but return the `data` payload and raise ApiError on failure."""
        r = self.call(**req)
        if not r.get("success"):
            raise ApiError(r.get("error", "UNKNOWN"), req)
        return r.get("data")
