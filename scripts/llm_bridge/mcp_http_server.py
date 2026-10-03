#!/usr/bin/env python3
"""The MCP server of mcp_server.py over HTTP on localhost, instead of stdio: for an MCP client that connects to a URL
rather than starting the server as its own child process.

**Why.** mcp_server.py is a stdio server: the client launches it and talks over its stdin/stdout. A client that can no
longer (or never could) launch a local command -- a desktop app whose local-extension rules changed, a sandboxed client --
can still reach a server that is already listening. This one runs in its own terminal, next to the game, and the client
connects to its URL. Same tools, same prompts, same Session code: only the transport differs.

**Usage.**

    python3 scripts/llm_bridge/mcp_http_server.py                  # http://127.0.0.1:5600/mcp
    python3 scripts/llm_bridge/mcp_http_server.py --port 5601 --token SECRET

then register the URL with the client, e.g. for Claude Code:

    claude mcp add --transport http daimonkeeper http://127.0.0.1:5600/mcp

(with --token, also: --header "Authorization: Bearer SECRET"). Anything that can POST JSON can use it too, e.g. curl.

**Protocol notes.** MCP's Streamable HTTP transport (2025-03-26 and later), the plain-JSON subset: every POST to /mcp
carries one JSON-RPC message (or a batch) and gets its answer as one application/json body -- 202 with no body when it
held only notifications. The server never pushes anything on its own, so there is no SSE stream: GET and DELETE answer
405, as the transport allows. No Mcp-Session-Id: the process holds one game connection (the game's own "one API client at
a time" rule), so every client of this server shares that one seat, and it outlives the client -- a client that restarts
and initializes again finds the game still connected and carries on. Tool calls are run one at a time, all on one thread
(a Session is not thread-safe, and its experience store is sqlite); a long wait_for_decision holds the next call back until it returns, but ping and the lists are answered
meanwhile. Keep wait_for_decision's timeout_seconds under the client's own request timeout (60 s in the reference MCP
SDKs; the default 50 fits).

**Security.** Binds 127.0.0.1 by default. The Host and Origin headers are checked against loopback names, which stops a
web page in a browser on this machine from reaching the server by DNS rebinding. Binding to a non-loopback address needs
--token (every request must then carry "Authorization: Bearer <token>"), since anyone who can reach the port can play the
seat and read the game's log. Standard library only, like the rest of scripts/llm_bridge/.
"""
import argparse
import hmac
import json
import os
import socket
import sys
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mcp_server  # noqa: E402

DEFAULT_HOST, DEFAULT_PORT, ENDPOINT = "127.0.0.1", 5600, "/mcp"
LOOPBACK_NAMES = ("127.0.0.1", "localhost", "::1")
MAX_BODY_BYTES = 4 * 1024 * 1024


def _hostname(value):
    """"localhost:5600" / "[::1]:5600" / "http://localhost:5600" -> "localhost" / "::1"."""
    if "//" not in value:
        value = "//" + value
    try:
        return (urlsplit(value).hostname or "").lower()
    except ValueError:
        return ""


class McpHttpServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, server=None, token=None, allowed_hosts=LOOPBACK_NAMES, quiet=False):
        self.mcp = server or mcp_server.Server()
        self.token = token
        self.allowed_hosts = tuple(allowed_hosts)
        self.quiet = quiet
        # Every tool call runs on this one thread, in arrival order: a Session is not thread-safe, and its experience
        # store is an sqlite connection, usable only from the thread that opened it.
        self.tool_thread = ThreadPoolExecutor(max_workers=1, thread_name_prefix="mcp-tools")
        if ":" in address[0]:
            self.address_family = socket.AF_INET6
        super().__init__(address, Handler)

    def handle_message(self, msg):
        """One JSON-RPC message -> its response dict, or None. Tool calls run on the tool thread, one at a time."""
        if isinstance(msg, dict) and msg.get("method") == "tools/call":
            return self.tool_thread.submit(self.mcp.handle, msg).result()
        if not isinstance(msg, dict):
            return {"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "invalid request"}}
        if "method" not in msg and ("result" in msg or "error" in msg):
            return None   # a client's response: this server sends no requests, so nothing is waiting for it
        return self.mcp.handle(msg)

    def shutdown_session(self):
        """Leave the game's seat cleanly when the server stops, so the game does not wait on a dead connection."""
        session = self.mcp.session
        if getattr(session, "connected", False):
            try:
                self.tool_thread.submit(session.disconnect).result(timeout=30)
            except Exception as e:  # noqa: BLE001 - best effort on the way out
                print("daimonkeeper-bridge: disconnect on shutdown failed: %s" % (e,), file=sys.stderr)

    def server_close(self):
        super().server_close()
        self.tool_thread.shutdown(wait=False)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "%s/%s" % (mcp_server.SERVER_NAME, mcp_server.SERVER_VERSION)

    def log_message(self, fmt, *args):
        if not self.server.quiet:
            sys.stderr.write("daimonkeeper-bridge: %s %s\n" % (self.address_string(), fmt % args))

    def _send(self, status, body=None, extra_headers=()):
        data = b"" if body is None else json.dumps(body).encode("utf-8")
        self.send_response(status)
        if body is not None:
            self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        for k, v in extra_headers:
            self.send_header(k, v)
        self.end_headers()
        if data:
            self.wfile.write(data)

    def _send_error(self, status, message, code=-32600, req_id=None, data=None):
        error = {"code": code, "message": message}
        if data is not None:
            error["data"] = data
        self._send(status, {"jsonrpc": "2.0", "id": req_id, "error": error})

    def _refused(self):
        """The Host/Origin/token checks: a reason to refuse this request, or None."""
        if self.server.allowed_hosts:
            if _hostname(self.headers.get("Host", "")) not in self.server.allowed_hosts:
                return 403, "Host not allowed"
            origin = self.headers.get("Origin")
            if origin and origin != "null" and _hostname(origin) not in self.server.allowed_hosts:
                return 403, "Origin not allowed"
        if self.server.token:
            auth = self.headers.get("Authorization", "")
            if not hmac.compare_digest(auth.encode("utf-8"), ("Bearer " + self.server.token).encode("utf-8")):
                return 401, "missing or wrong bearer token"
        return None

    def _check_path_and_access(self):
        if urlsplit(self.path).path.rstrip("/") != ENDPOINT:
            self._send_error(404, "not found; the MCP endpoint is %s" % ENDPOINT)
            return False
        refused = self._refused()
        if refused:
            self._send_error(*refused)
            return False
        return True

    def do_GET(self):
        if self._check_path_and_access():
            # No server-initiated messages, so no SSE stream to offer.
            self._send(405, None, [("Allow", "POST")])

    def do_DELETE(self):
        if self._check_path_and_access():
            self._send(405, None, [("Allow", "POST")])

    def _read_body(self):
        """The request body, or None (after answering) when it cannot be read. Read before any other answer: on a
        keep-alive connection an unread body would be taken for the next request's first line."""
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            length = None
        if length is None or length < 0 or length > MAX_BODY_BYTES:
            self.close_connection = True   # the body, if any, stays unread: never reuse this connection
            if length is None:
                self._send_error(411, "Content-Length required")
            else:
                self._send_error(413, "body too large")
            return None
        return self.rfile.read(length)

    def do_POST(self):
        raw = self._read_body()
        if raw is None or not self._check_path_and_access():
            return
        try:
            msg = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            self._send_error(400, "parse error", code=-32700)
            return
        version = self.headers.get("MCP-Protocol-Version")
        if version and version not in mcp_server.SUPPORTED_PROTOCOL_VERSIONS:
            # e.g. a client probing a newer revision first (Claude Code's server/discover at 2026-07-28); it then
            # falls back to initialize at a version listed here.
            self._send_error(400, "unsupported MCP-Protocol-Version %r" % (version,),
                             req_id=msg.get("id") if isinstance(msg, dict) else None,
                             data={"supported": list(mcp_server.SUPPORTED_PROTOCOL_VERSIONS)})
            return
        if isinstance(msg, list):
            if not msg:
                self._send_error(400, "empty batch")
                return
            responses = [r for r in (self.server.handle_message(m) for m in msg) if r is not None]
            body = responses or None
        else:
            body = self.server.handle_message(msg)
        if body is None:
            self._send(202)   # notifications/responses only: accepted, nothing to answer
        else:
            self._send(200, body)

def main(argv=None):
    ap = argparse.ArgumentParser(description="dAImon Keeper MCP server over HTTP (Streamable HTTP, JSON responses).")
    ap.add_argument("--host", default=DEFAULT_HOST, help="address to listen on (default %(default)s: this machine only)")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT, help="port to listen on (default %(default)s)")
    ap.add_argument("--token", default=os.environ.get("DAIMONKEEPER_MCP_TOKEN"),
                    help="require 'Authorization: Bearer TOKEN' on every request (env DAIMONKEEPER_MCP_TOKEN); "
                         "needed when --host is not a loopback address")
    ap.add_argument("--quiet", action="store_true", help="do not log each request to stderr")
    args = ap.parse_args(argv)

    loopback = args.host in LOOPBACK_NAMES
    if not loopback and not args.token:
        ap.error("--host %s is reachable from other machines; give --token too" % (args.host,))
    # On a non-loopback address the client names the host however it reaches it, so only the token guards it.
    allowed = LOOPBACK_NAMES if loopback else ()

    httpd = McpHttpServer((args.host, args.port), token=args.token, allowed_hosts=allowed, quiet=args.quiet)
    shown_host = "[%s]" % args.host if ":" in args.host else args.host
    print("daimonkeeper-bridge: MCP over HTTP at http://%s:%d%s%s  (Ctrl-C to stop)"
          % (shown_host, httpd.server_address[1], ENDPOINT, ", bearer token required" if args.token else ""),
          file=sys.stderr, flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.shutdown_session()
        httpd.server_close()


if __name__ == "__main__":
    main()
