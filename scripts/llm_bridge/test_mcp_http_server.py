#!/usr/bin/env python3
"""Offline tests of the MCP-over-HTTP transport (no game; a loopback HTTP server in-process, and one real child process):
python3 scripts/llm_bridge/test_mcp_http_server.py"""
import contextlib
import http.client
import io
import json
import os
import re
import subprocess
import sys
import threading
import unittest

import tempfile as _tempfile

# Never let a test touch the real experience store (experience.default_path(), next to the bridge's own files).
_TEST_DATA_HOME = _tempfile.mkdtemp(prefix="kfx-bridge-test-data-")
os.environ["DAIMONKEEPER_EXPERIENCE"] = os.path.join(_TEST_DATA_HOME, "experience.sqlite")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mcp_http_server  # noqa: E402
import mcp_server  # noqa: E402
from test_mcp_server import FakeGame, StubSession  # noqa: E402


class HttpTransportBase(unittest.TestCase):
    token = None

    def setUp(self):
        self.session = StubSession()
        self.httpd = mcp_http_server.McpHttpServer(("127.0.0.1", 0), server=mcp_server.Server(session=self.session),
                                                   token=self.token, quiet=True)
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=5)

    def request(self, method, body=None, path="/mcp", headers=None, raw=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            hdrs = {"Accept": "application/json, text/event-stream"}
            data = raw
            if body is not None:
                data = json.dumps(body).encode("utf-8")
                hdrs["Content-Type"] = "application/json"
            hdrs.update(headers or {})
            conn.request(method, path, body=data, headers=hdrs)
            resp = conn.getresponse()
            payload = resp.read()
            return resp.status, resp.getheader("Content-Type"), (json.loads(payload) if payload else None)
        finally:
            conn.close()

    def rpc(self, method, params=None, req_id=1, headers=None):
        msg = {"jsonrpc": "2.0", "id": req_id, "method": method}
        if params is not None:
            msg["params"] = params
        return self.request("POST", msg, headers=headers)


class ProtocolOverHttpTests(HttpTransportBase):
    def test_initialize_echoes_a_supported_protocol_version(self):
        status, ctype, body = self.rpc("initialize", {"protocolVersion": "2025-06-18", "clientInfo": {"name": "tester"}})
        self.assertEqual(status, 200)
        self.assertEqual(ctype, "application/json")
        self.assertEqual(body["result"]["protocolVersion"], "2025-06-18")
        self.assertEqual(body["result"]["serverInfo"]["name"], "daimonkeeper-bridge")
        self.assertEqual(self.session.client_name, "tester")

    def test_initialize_falls_back_to_our_version_for_an_unknown_one(self):
        _, _, body = self.rpc("initialize", {"protocolVersion": "1999-01-01"})
        self.assertEqual(body["result"]["protocolVersion"], mcp_server.PROTOCOL_VERSION)

    def test_a_notification_is_accepted_with_no_body(self):
        status, _, body = self.request("POST", {"jsonrpc": "2.0", "method": "notifications/initialized"})
        self.assertEqual((status, body), (202, None))

    def test_a_client_response_is_accepted_with_no_body(self):
        status, _, body = self.request("POST", {"jsonrpc": "2.0", "id": 9, "result": {}})
        self.assertEqual((status, body), (202, None))

    def test_tools_list_and_call(self):
        _, _, body = self.rpc("tools/list", headers={"MCP-Protocol-Version": "2025-06-18"})
        self.assertIn("wait_for_decision", {t["name"] for t in body["result"]["tools"]})
        _, _, body = self.rpc("tools/call", {"name": "connect", "arguments": {"port": 1234}}, req_id=2)
        self.assertEqual(body["id"], 2)
        self.assertEqual(body["result"]["content"][0]["text"], "connected: ok")
        self.assertEqual(self.session.calls[-1], ("connect", {"port": 1234}))

    def test_a_tool_error_is_a_result_not_an_http_error(self):
        status, _, body = self.rpc("tools/call", {"name": "status", "arguments": {}})
        self.assertEqual(status, 200)
        self.assertTrue(body["result"]["isError"])

    def test_a_batch_gets_the_requests_answers_and_skips_notifications(self):
        status, _, body = self.request("POST", [
            {"jsonrpc": "2.0", "id": 1, "method": "ping"},
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "get_instructions"}},
        ])
        self.assertEqual(status, 200)
        self.assertEqual([r["id"] for r in body], [1, 2])
        self.assertEqual(body[1]["result"]["content"][0]["text"], "SYSTEM PROMPT TEXT")

    def test_a_batch_of_notifications_only_is_accepted_with_no_body(self):
        status, _, body = self.request("POST", [{"jsonrpc": "2.0", "method": "notifications/initialized"}])
        self.assertEqual((status, body), (202, None))

    def test_bad_json_is_a_parse_error(self):
        status, _, body = self.request("POST", raw=b"not even json", headers={"Content-Type": "application/json"})
        self.assertEqual(status, 400)
        self.assertEqual(body["error"]["code"], -32700)

    def test_a_non_object_message_is_an_invalid_request(self):
        status, _, body = self.request("POST", [42])
        self.assertEqual(status, 200)
        self.assertEqual(body[0]["error"]["code"], -32600)

    def test_an_unsupported_protocol_version_header_is_refused(self):
        status, _, _ = self.rpc("ping", headers={"MCP-Protocol-Version": "1999-01-01"})
        self.assertEqual(status, 400)

    def test_a_refused_request_does_not_poison_the_keep_alive_connection(self):
        # What Claude Code 2.1.283 does: a server/discover probe at a protocol revision this server does not know, then
        # initialize on the same connection. The refused probe's body must be read, not left to be taken for the next
        # request's first line.
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        try:
            probe = {"jsonrpc": "2.0", "id": "server-discover-probe-1", "method": "server/discover", "params": {}}
            conn.request("POST", "/mcp", body=json.dumps(probe), headers={"Content-Type": "application/json",
                                                                           "MCP-Protocol-Version": "2026-07-28"})
            resp = conn.getresponse()
            body = json.loads(resp.read())
            self.assertEqual((resp.status, body["id"]), (400, "server-discover-probe-1"))
            self.assertIn("2025-11-25", body["error"]["data"]["supported"])
            # Refused for its Host (DNS-rebinding check), also with a body.
            conn.request("POST", "/mcp", body=json.dumps(probe), headers={"Content-Type": "application/json",
                                                                           "Host": "evil.example"})
            resp = conn.getresponse()
            resp.read()
            self.assertEqual(resp.status, 403)
            conn.request("POST", "/mcp", body=json.dumps({"jsonrpc": "2.0", "id": 0, "method": "initialize",
                                                          "params": {"protocolVersion": "2025-11-25"}}),
                         headers={"Content-Type": "application/json"})
            resp = conn.getresponse()
            body = json.loads(resp.read())
            self.assertEqual((resp.status, body["result"]["protocolVersion"]), (200, "2025-11-25"))
        finally:
            conn.close()

    def test_get_and_delete_are_not_offered(self):
        for method in ("GET", "DELETE"):
            status, _, _ = self.request(method)
            self.assertEqual(status, 405, method)

    def test_other_paths_are_not_found(self):
        status, _, _ = self.request("POST", {"jsonrpc": "2.0", "id": 1, "method": "ping"}, path="/")
        self.assertEqual(status, 404)
        status, _, body = self.request("POST", {"jsonrpc": "2.0", "id": 1, "method": "ping"}, path="/mcp/")
        self.assertEqual((status, body["result"]), (200, {}))

    def test_a_foreign_host_or_origin_is_refused(self):
        # What a DNS-rebinding page in a local browser would send.
        status, _, _ = self.rpc("ping", headers={"Host": "evil.example:%d" % self.port})
        self.assertEqual(status, 403)
        status, _, _ = self.rpc("ping", headers={"Origin": "http://evil.example"})
        self.assertEqual(status, 403)
        for origin in ("http://localhost:3000", "http://127.0.0.1", "http://[::1]:8080", "null"):
            status, _, _ = self.rpc("ping", headers={"Origin": origin})
            self.assertEqual(status, 200, origin)
        status, _, _ = self.rpc("ping", headers={"Host": "localhost:%d" % self.port})
        self.assertEqual(status, 200)

    def test_tool_calls_run_one_at_a_time_but_ping_is_not_held_back(self):
        started, release = threading.Event(), threading.Event()
        in_call = []

        def slow_wait(**kw):
            in_call.append(1)
            self.assertEqual(len(in_call), 1, "two tool calls ran at once")
            started.set()
            release.wait(5)
            in_call.pop()
            return {"due": False}

        self.session.wait_for_decision = slow_wait
        results = []
        t1 = threading.Thread(target=lambda: results.append(self.rpc("tools/call", {"name": "wait_for_decision"})))
        t1.start()
        self.assertTrue(started.wait(5))
        t2 = threading.Thread(target=lambda: results.append(self.rpc("tools/call", {"name": "wait_for_decision"}, req_id=2)))
        t2.start()
        status, _, body = self.rpc("ping", req_id=3)   # answered while the tool call is blocked
        self.assertEqual((status, body["result"]), (200, {}))
        release.set()
        t1.join(5)
        t2.join(5)
        self.assertEqual(sorted(r[2]["id"] for r in results), [1, 2])

    def test_shutdown_disconnects_a_connected_session_only(self):
        disconnects = []
        self.session.disconnect = lambda: disconnects.append(1)
        self.httpd.shutdown_session()
        self.assertEqual(disconnects, [])
        self.session.connected = True
        self.httpd.shutdown_session()
        self.assertEqual(disconnects, [1])


class TokenTests(HttpTransportBase):
    token = "s3cret"

    def test_a_request_without_the_token_is_refused(self):
        status, _, _ = self.rpc("ping")
        self.assertEqual(status, 401)
        status, _, _ = self.rpc("ping", headers={"Authorization": "Bearer wrong"})
        self.assertEqual(status, 401)

    def test_a_request_with_the_token_is_answered(self):
        status, _, body = self.rpc("ping", headers={"Authorization": "Bearer s3cret"})
        self.assertEqual((status, body["result"]), (200, {}))


class GameOverHttpTests(unittest.TestCase):
    """A real Session against test_mcp_server's fake game, every call through HTTP."""

    def setUp(self):
        self.game = FakeGame()
        self.httpd = mcp_http_server.McpHttpServer(("127.0.0.1", 0), quiet=True)
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.httpd.shutdown_session()
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=5)
        self.game.close()

    def call(self, name, arguments=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.httpd.server_address[1], timeout=10)
        try:
            conn.request("POST", "/mcp", headers={"Content-Type": "application/json"},
                         body=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                                          "params": {"name": name, "arguments": arguments or {}}}))
            return json.loads(conn.getresponse().read())["result"]
        finally:
            conn.close()

    def test_connect_decide_submit_and_the_seat_outlives_the_client(self):
        r = self.call("connect", {"host": "127.0.0.1", "port": self.game.port})
        self.assertFalse(r.get("isError"), r)
        first = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])
        self.assertEqual(first["reasons"], ["start"])

        threading.Timer(0.05, self.game.push_event, args=(["quarter_1"],)).start()
        data = json.loads(self.call("wait_for_decision", {"timeout_seconds": 3.0})["content"][0]["text"])
        self.assertEqual((data["due"], data["reasons"]), (True, ["quarter_1"]))

        # Each call above was its own HTTP connection; the game connection stayed up throughout.
        result = json.loads(self.call("submit_orders", {"orders": [{"verb": "mark_dig", "slab_rect": [1, 1, 2, 2]}],
                                                        "reasoning": "dig it"})["content"][0]["text"])
        self.assertEqual(len(result["sent"]), 1)
        self.assertEqual(self.game.submitted[-1]["verb"], "mark_dig")

        # Stopping the server leaves the seat cleanly.
        self.httpd.shutdown_session()
        self.assertFalse(self.httpd.mcp.session.connected)


class CommandLineTests(unittest.TestCase):
    def test_a_non_loopback_host_needs_a_token(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            mcp_http_server.main(["--host", "0.0.0.0", "--port", "0"])

    def test_the_real_process_serves_mcp_over_http(self):
        server_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mcp_http_server.py")
        env = dict(os.environ, DAIMONKEEPER_MCP_TOKEN="")
        proc = subprocess.Popen([sys.executable, server_path, "--port", "0", "--quiet"], stderr=subprocess.PIPE,
                                stdout=subprocess.DEVNULL, text=True, env=env)
        try:
            banner = proc.stderr.readline()
            m = re.search(r"http://127\.0\.0\.1:(\d+)/mcp", banner)
            self.assertTrue(m, banner)
            conn = http.client.HTTPConnection("127.0.0.1", int(m.group(1)), timeout=10)
            conn.request("POST", "/mcp", body=json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                                                          "params": {"protocolVersion": "2025-03-26"}}),
                         headers={"Content-Type": "application/json", "Accept": "application/json, text/event-stream"})
            resp = conn.getresponse()
            body = json.loads(resp.read())
            self.assertEqual(body["result"]["protocolVersion"], "2025-03-26")
            conn.request("POST", "/mcp", body=json.dumps({"jsonrpc": "2.0", "id": 2, "method": "tools/call",
                                                          "params": {"name": "status", "arguments": {}}}),
                         headers={"Content-Type": "application/json"})
            body = json.loads(conn.getresponse().read())   # same keep-alive connection
            self.assertTrue(body["result"]["isError"])
            self.assertIn("not connected", body["result"]["content"][0]["text"])
            conn.close()
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
            proc.stderr.close()


if __name__ == "__main__":
    unittest.main()
