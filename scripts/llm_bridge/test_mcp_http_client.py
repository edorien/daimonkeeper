#!/usr/bin/env python3
"""Offline tests of the command-line MCP-over-HTTP client (no game: an in-process mcp_http_server.py, in front of either a
stub session or test_mcp_server's fake game): python3 scripts/llm_bridge/test_mcp_http_client.py"""
import contextlib
import io
import json
import os
import sys
import tempfile
import threading
import unittest

import tempfile as _tempfile

# Never let a test touch the real experience store (experience.default_path(), next to the bridge's own files).
_TEST_DATA_HOME = _tempfile.mkdtemp(prefix="kfx-bridge-test-data-")
os.environ["DAIMONKEEPER_EXPERIENCE"] = os.path.join(_TEST_DATA_HOME, "experience.sqlite")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mcp_http_client  # noqa: E402
import mcp_http_server  # noqa: E402
import mcp_server  # noqa: E402
from test_mcp_server import FakeGame, StubSession  # noqa: E402


class ServerFixture(unittest.TestCase):
    token = None

    def start(self, server=None):
        self.httpd = mcp_http_server.McpHttpServer(("127.0.0.1", 0), server=server, token=self.token, quiet=True)
        self.url = "http://127.0.0.1:%d/mcp" % self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.httpd.shutdown_session()
        self.httpd.shutdown()
        self.httpd.server_close()
        self.thread.join(timeout=5)

    def run_client(self, *argv, stdin=""):
        out = io.StringIO()
        with contextlib.redirect_stderr(io.StringIO()):   # the expected failures' messages
            code = mcp_http_client.main(["--url", self.url] + list(argv), stdin=io.StringIO(stdin), stdout=out)
        return code, out.getvalue()


class StubServerTests(ServerFixture):
    def setUp(self):
        self.session = StubSession()
        self.start(mcp_server.Server(session=self.session))

    def test_list_names_every_tool(self):
        code, out = self.run_client("--list")
        self.assertEqual(code, 0)
        names = {line.split(":")[0] for line in out.splitlines()}
        self.assertEqual(names, {t["name"] for t in mcp_server.TOOLS})

    def test_inline_arguments(self):
        code, out = self.run_client("connect", '{"port": 1234, "agent": "Claude"}')
        self.assertEqual((code, out), (0, "connected: ok\n"))
        self.assertEqual(self.session.calls[-1], ("connect", {"port": 1234, "agent": "Claude"}))

    def test_arguments_from_stdin_survive_shell_hostile_text(self):
        orders = {"reasoning": "Don't wait; it's \"urgent\"", "orders": [{"verb": "mark_dig", "slab_rect": [1, 1, 2, 2]}]}
        code, _ = self.run_client("submit_orders", "-", stdin=json.dumps(orders))
        self.assertEqual(code, 0)
        self.assertEqual(self.session.calls[-1][2], orders["reasoning"])

    def test_no_arguments_means_an_empty_object(self):
        code, out = self.run_client("get_instructions")
        self.assertEqual((code, out), (0, "SYSTEM PROMPT TEXT\n"))

    def test_the_client_name_is_sent_as_the_mcp_client_info(self):
        self.run_client("--client-name", "Claude", "get_instructions")
        self.assertEqual(self.session.client_name, "Claude")

    def test_a_tool_error_exits_1_with_its_text(self):
        code, out = self.run_client("status")
        self.assertEqual(code, 1)
        self.assertIn("TOOL ERROR: not connected", out)

    def test_bad_arguments_exit_2_without_calling(self):
        for raw in ("not json", "[1, 2]"):
            code, _ = self.run_client("connect", raw)
            self.assertEqual(code, 2, raw)
        self.assertEqual(self.session.calls, [])

    def test_the_http_timeout_covers_a_long_wait(self):
        seen = []
        client = mcp_http_client.Client(self.url)
        client._post = lambda msg, timeout: seen.append(timeout) or {"content": [{"type": "text", "text": "{}"}]}
        client.call("wait_for_decision", {"timeout_seconds": 200})
        client.call("status")
        self.assertEqual(seen, [200 + mcp_http_client.TIMEOUT_MARGIN_SECONDS, mcp_http_client.DEFAULT_TIMEOUT_SECONDS])

    def test_brief_leaves_other_text_alone(self):
        self.assertEqual(mcp_http_client.brief("connected: ok"), "connected: ok")
        self.assertEqual(mcp_http_client.brief('{"due": false, "turn": 5}'), '{"due": false, "turn": 5}')


class TokenTests(ServerFixture):
    token = "s3cret"

    def setUp(self):
        self.start(mcp_server.Server(session=StubSession()))

    def test_the_token_is_sent(self):
        self.assertEqual(self.run_client("get_instructions")[0], 2)   # refused: 401
        self.assertEqual(self.run_client("--token", "s3cret", "get_instructions")[0], 0)


class UnreachableTests(unittest.TestCase):
    def test_no_server_exits_2(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = mcp_http_client.main(["--url", "http://127.0.0.1:1/mcp", "status"], stdout=io.StringIO())
        self.assertEqual(code, 2)
        self.assertIn("is mcp_http_server.py running?", err.getvalue())


class FakeGameTests(ServerFixture):
    """A real Session and real rendered decisions, so --brief is checked against the text the game actually produces."""

    def setUp(self):
        self.game = FakeGame()
        self.start()

    def tearDown(self):
        super().tearDown()
        self.game.close()

    def test_play_a_decision_briefly_and_keep_the_full_text(self):
        code, out = self.run_client("connect", json.dumps({"port": self.game.port, "max_age_turns": 3000}))
        self.assertEqual(code, 0, out)
        self.assertEqual(self.httpd.mcp.session.max_age_turns, 3000)

        tmp = tempfile.TemporaryDirectory(prefix="kfx-client-test-")
        self.addCleanup(tmp.cleanup)
        saved = os.path.join(tmp.name, "decision.json")
        code, out = self.run_client("--brief", "--save", saved, "wait_for_decision", '{"timeout_seconds": 0.2}')
        self.assertEqual(code, 0, out)
        head, _, state = out.partition("\n")
        self.assertEqual(json.loads(head)["reasons"], ["start"])
        self.assertTrue(state.startswith("DECISION DUE"), state[:200])
        self.assertIn("TURN ", state)
        for gone in ("YOUR PLAN", "YOUR NOTES", "EXPERIENCE"):
            self.assertNotIn(gone, state)
        self.assertIn("full map omitted", state)

        with open(saved, encoding="utf-8") as f:
            full = json.load(f)["state"]   # --save keeps what --brief left out
        self.assertIn("YOUR PLAN", full)
        self.assertIn("\nMAP ", full)
        self.assertLess(len(state), len(full))

        code, out = self.run_client("--brief", "wait_for_decision", '{"timeout_seconds": 0.2, "treat_timeout_as_decision": true}')
        self.assertEqual(code, 0, out)
        self.assertNotIn("full map omitted", out)   # only a level's first decision carries the map

    def test_brief_keeps_a_note_before_the_memory(self):
        state = "NOTE: the game was reloaded\n\nYOUR PLAN (...):\nplan\nYOUR NOTES (...):\nnotes\n\nDECISION DUE because: x\nTURN 5 | gold 1"
        self.assertEqual(mcp_http_client.brief_state(state), "NOTE: the game was reloaded\n\nDECISION DUE because: x\nTURN 5 | gold 1")


if __name__ == "__main__":
    unittest.main()
