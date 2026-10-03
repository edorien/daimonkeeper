#!/usr/bin/env python3
"""Offline tests of the MCP server (no game, no network beyond a loopback fake and a real child process for the transport
smoke test): python3 scripts/llm_bridge/test_mcp_server.py"""
import json
import os
import shutil
import tempfile
import subprocess
import sys
import threading
import unittest

import tempfile as _tempfile

# Never let a test touch the real experience store (experience.default_path(), next to the bridge's own files).
_TEST_DATA_HOME = _tempfile.mkdtemp(prefix="kfx-bridge-test-data-")
os.environ["DAIMONKEEPER_EXPERIENCE"] = os.path.join(_TEST_DATA_HOME, "experience.sqlite")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import mcp_server  # noqa: E402
from mcp_session import SessionError  # noqa: E402
from test_bridge import make_view  # noqa: E402


class StubSession:
    """Enough of Session's shape to test the protocol/dispatch layer without a network connection."""

    def __init__(self):
        self.calls = []

    def connect(self, **kw):
        self.calls.append(("connect", kw))
        return "connected: ok"

    def wait_for_decision(self, **kw):
        self.calls.append(("wait_for_decision", kw))
        return {"due": False, "turn": 5}

    def status(self):
        raise SessionError("not connected; call connect first")

    def look(self, slab_rect):
        return "window for %r" % (slab_rect,)

    def submit(self, orders, reasoning="", plan=None, notes=None):
        self.calls.append(("submit", orders, reasoning, plan, notes))
        return {"sent": [], "refused": []}

    def check_orders(self, orders):
        self.calls.append(("check_orders", orders))
        return {"would_succeed": [], "would_fail": []}

    def set_speed(self, turns_per_second):
        self.calls.append(("set_speed", turns_per_second))
        return "turns_per_second is now %d" % turns_per_second

    def log_tail(self, lines=100):
        self.calls.append(("log_tail", lines))
        return "line1\nline2"

    def instructions(self):
        return "SYSTEM PROMPT TEXT"

    def disconnect(self):
        return "disconnected"


class BoomSession(StubSession):
    def status(self):
        raise RuntimeError("boom")


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.server = mcp_server.Server(session=StubSession())

    def test_initialize_advertises_tools_and_prompts(self):
        resp = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        self.assertEqual(resp["result"]["serverInfo"]["name"], "daimonkeeper-bridge")
        self.assertIn("tools", resp["result"]["capabilities"])
        self.assertIn("prompts", resp["result"]["capabilities"])

    def test_notification_gets_no_response(self):
        self.assertIsNone(self.server.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}))

    def test_unknown_method_on_a_request_is_a_json_rpc_error(self):
        resp = self.server.handle({"jsonrpc": "2.0", "id": 2, "method": "nope"})
        self.assertEqual(resp["error"]["code"], -32601)

    def test_unknown_method_on_a_notification_is_silently_ignored(self):
        self.assertIsNone(self.server.handle({"jsonrpc": "2.0", "method": "nope"}))

    def test_ping(self):
        resp = self.server.handle({"jsonrpc": "2.0", "id": 3, "method": "ping"})
        self.assertEqual(resp["result"], {})

    def test_tools_list_matches_the_dispatch_table(self):
        resp = self.server.handle({"jsonrpc": "2.0", "id": 4, "method": "tools/list"})
        names = {t["name"] for t in resp["result"]["tools"]}
        self.assertEqual(names, {"connect", "get_instructions", "wait_for_decision", "status", "look", "submit_orders",
                                  "check_orders", "set_game_speed", "get_log_tail", "get_recipes", "get_experience",
                                  "record_debrief", "disconnect"})
        for t in resp["result"]["tools"]:
            self.assertIn("inputSchema", t)

    def test_prompts_list_and_get(self):
        resp = self.server.handle({"jsonrpc": "2.0", "id": 5, "method": "prompts/list"})
        self.assertEqual([p["name"] for p in resp["result"]["prompts"]], ["play_daimonkeeper"])
        resp = self.server.handle({"jsonrpc": "2.0", "id": 6, "method": "prompts/get", "params": {"name": "play_daimonkeeper"}})
        self.assertIn("Dungeon Keeper", resp["result"]["messages"][0]["content"]["text"])
        resp = self.server.handle({"jsonrpc": "2.0", "id": 7, "method": "prompts/get", "params": {"name": "nope"}})
        self.assertEqual(resp["error"]["code"], -32602)


class ToolDispatchTests(unittest.TestCase):
    def setUp(self):
        self.stub = StubSession()
        self.server = mcp_server.Server(session=self.stub)

    def call(self, name, arguments=None, req_id=1):
        return self.server.handle({"jsonrpc": "2.0", "id": req_id, "method": "tools/call",
                                    "params": {"name": name, "arguments": arguments}})["result"]

    def test_connect_forwards_arguments(self):
        r = self.call("connect", {"host": "1.2.3.4", "port": 6000, "claim": 2})
        self.assertEqual(r["content"][0]["text"], "connected: ok")
        self.assertEqual(self.stub.calls, [("connect", {"host": "1.2.3.4", "port": 6000, "claim": 2})])

    def test_get_instructions(self):
        r = self.call("get_instructions", {})
        self.assertEqual(r["content"][0]["text"], "SYSTEM PROMPT TEXT")

    def test_wait_for_decision_returns_json_text(self):
        r = self.call("wait_for_decision", {"timeout_seconds": 1.5})
        self.assertEqual(json.loads(r["content"][0]["text"]), {"due": False, "turn": 5})

    def test_look_passes_the_rectangle(self):
        r = self.call("look", {"slab_rect": [1, 2, 3, 4]})
        self.assertEqual(r["content"][0]["text"], "window for [1, 2, 3, 4]")

    def test_submit_orders_defaults(self):
        r = self.call("submit_orders", {"orders": [{"verb": "cancel"}]})
        self.assertEqual(json.loads(r["content"][0]["text"]), {"sent": [], "refused": []})
        self.assertEqual(self.stub.calls, [("submit", [{"verb": "cancel"}], "", None, None)])

    def test_check_orders_forwards_and_returns_json(self):
        r = self.call("check_orders", {"orders": [{"verb": "build_room", "kind": "TREASURE", "slab_rect": [1, 1, 2, 2]}]})
        self.assertEqual(json.loads(r["content"][0]["text"]), {"would_succeed": [], "would_fail": []})
        self.assertEqual(self.stub.calls, [("check_orders", [{"verb": "build_room", "kind": "TREASURE", "slab_rect": [1, 1, 2, 2]}])])

    def test_set_game_speed_forwards_the_value(self):
        r = self.call("set_game_speed", {"turns_per_second": 5})
        self.assertEqual(r["content"][0]["text"], "turns_per_second is now 5")
        self.assertEqual(self.stub.calls, [("set_speed", 5)])

    def test_get_log_tail_forwards_lines_and_defaults(self):
        r = self.call("get_log_tail", {"lines": 20})
        self.assertEqual(r["content"][0]["text"], "line1\nline2")
        self.assertEqual(self.stub.calls, [("log_tail", 20)])
        self.stub.calls.clear()
        self.call("get_log_tail", {})
        self.assertEqual(self.stub.calls, [("log_tail", 100)])

    def test_disconnect(self):
        self.assertEqual(self.call("disconnect", {})["content"][0]["text"], "disconnected")

    def test_a_session_error_is_reported_as_a_tool_error_not_a_protocol_error(self):
        r = self.call("status", {})
        self.assertTrue(r.get("isError"))
        # exactly the message, with no "SessionError:" diagnostic prefix -- that prefix is for the *unexpected*-exception
        # branch below; a SessionError is an anticipated "you called this out of order" case worded to read on its own.
        self.assertEqual(r["content"][0]["text"], "not connected; call connect first")

    def test_an_unexpected_exception_is_also_a_tool_error(self):
        server = mcp_server.Server(session=BoomSession())
        r = server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                            "params": {"name": "status", "arguments": {}}})["result"]
        self.assertTrue(r.get("isError"))
        self.assertIn("boom", r["content"][0]["text"])

    def test_unknown_tool(self):
        r = self.call("not_a_real_tool", {})
        self.assertTrue(r.get("isError"))
        self.assertIn("unknown tool", r["content"][0]["text"])

    def test_missing_arguments_defaults_to_empty(self):
        # arguments omitted entirely (None) must not crash a tool that only has optional fields.
        r = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                                 "params": {"name": "get_instructions"}})["result"]
        self.assertEqual(r["content"][0]["text"], "SYSTEM PROMPT TEXT")


class FakeGame:
    """A minimal stand-in for keeperfx's TCP API: get_seats/claim_seat, subscribe_event, set_decision_policy,
    set_takeover, get_player_view (always a full view with an incrementing view_id -- diffs have their own coverage
    in test_bridge.py and the real ftest), submit_action (recorded), and push_event() for the test to raise a
    DECISION_DUE from its own thread, mid-wait."""

    def __init__(self):
        import socket
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.conn = None
        self.turn = 10
        self.view_id = 0
        self.submitted = []
        self.turns_per_second = 20
        self.victory_state = "undecided"
        self.memory = {}         # player -> the agent memory text the game holds (set_agent_memory)
        self.names = {0: "Robin"}  # player -> name (set_player_name)
        self.saved = None        # (turn, memory) of the one save slot
        self._write_lock = threading.Lock()
        self._buf = b""
        self._thread = threading.Thread(target=self._accept, daemon=True)
        self._thread.start()

    def _accept(self):
        self.conn, _ = self.sock.accept()
        self.conn.settimeout(5)
        while True:
            line = self._readline()
            if line is None:
                return
            try:
                req = json.loads(line)
            except ValueError:
                continue
            resp = self._handle(req)
            if resp is not None:
                self._send(resp)

    def _readline(self):
        while b"\n" not in self._buf:
            try:
                chunk = self.conn.recv(65536)
            except OSError:
                return None
            if not chunk:
                return None
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return line

    def _send(self, obj):
        data = (json.dumps(obj) + "\n").encode()
        with self._write_lock:
            self.conn.sendall(data)

    def _handle(self, req):
        action, ack = req.get("action"), req.get("ack")
        if action in ("claim_seat", "get_seats"):
            return {"ack": ack, "success": True, "data": {"seats": [{"player": 1, "user": 1}], "player": 1, "user": 1}}
        if action in ("subscribe_event", "set_decision_policy", "set_takeover"):
            return {"ack": ack, "success": True}
        if action == "get_player_view":
            self.turn += 1
            self.view_id += 1
            v = make_view(turn=self.turn)
            v["seat"]["victory_state"] = self.victory_state
            v["level"] = {"number": 7, "campaign": "keeporig.cfg", "name": "Flowerhat"}
            v["players"] = [{"id": 0, "player": 0, "kind": "human", "name": self.names.get(0), "alive": True},
                            {"id": 1, "player": 1, "kind": "external", "name": self.names.get(1, "External 1"), "alive": True},
                            {"id": 2, "player": 2, "kind": "computer", "ai_type": "Build and defend Computer.", "alive": True}]
            v["view_id"] = self.view_id
            v["mode"] = "full"
            return {"ack": ack, "success": True, "data": v}
        if action == "submit_action":
            if req.get("dry_run"):
                return {"ack": ack, "success": True, "data": {"would_succeed": True, "steps": 1}}
            self.submitted.append(req)
            return {"ack": ack, "success": True, "data": {"id": len(self.submitted), "queued_behind": 0, "steps": 1}}
        if action == "set_player_name":
            self.names[req["player"]] = req["name"]
            return {"ack": ack, "success": True}
        if action == "set_agent_memory":
            self.memory[req["player"]] = req["data"]
            return {"ack": ack, "success": True}
        if action == "get_agent_memory":
            return {"ack": ack, "success": True, "data": {"data": self.memory.get(req["player"])}}
        if action == "set_game_speed":
            self.turns_per_second = req.get("turns_per_second") or 20
            return {"ack": ack, "success": True, "data": {"turns_per_second": self.turns_per_second}}
        if action == "get_log_tail":
            want = req.get("lines", 100)
            return {"ack": ack, "success": True, "data": {"lines": ["fake log line %d" % i for i in range(want)]}}
        return {"ack": ack, "success": False, "error": "UNKNOWN_ACTION"}

    def push_event(self, reasons):
        self._send({"event": "DECISION_DUE", "data": {"reasons": ",".join(reasons), "seq": 1, "turn": self.turn, "quarter": 1}})

    def save(self):
        self.saved = (self.turn, dict(self.memory))
        self._send({"event": "GAME_SAVED", "data": {"turn": self.turn, "slot": 1}})

    def load(self):
        self.turn, self.memory = self.saved[0], dict(self.saved[1])
        self._send({"event": "GAME_LOADED", "data": {"turn": self.turn}})

    def push_game_ended(self):
        self._send({"event": "GAME_ENDED"})

    def close(self):
        try:
            if self.conn:
                self.conn.close()
        finally:
            self.sock.close()


class SessionIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.game = FakeGame()
        self.server = mcp_server.Server()

    def tearDown(self):
        self.game.close()

    def call(self, name, arguments=None, req_id=1):
        return self.server.handle({"jsonrpc": "2.0", "id": req_id, "method": "tools/call",
                                    "params": {"name": name, "arguments": arguments or {}}})["result"]

    def _connect_and_start(self):
        self.assertFalse(self.call("connect", {"host": "127.0.0.1", "port": self.game.port}).get("isError"))
        self.call("wait_for_decision", {"timeout_seconds": 0.2})   # the "start" decision

    def test_get_recipes(self):
        self._connect_and_start()
        text = self.call("get_recipes")["content"][0]["text"]
        self.assertIn("No temple", text)   # the fake game's view has no temple, so no recipes
        self.assertFalse(self.call("disconnect").get("isError"))

    def test_victory_ends_the_session(self):
        self._connect_and_start()
        self.game.victory_state = "won"
        self.game.push_event(["victory"])
        r = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertEqual((r["due"], r["game_over"], r["victory_state"], r["reasons"]), (True, True, "won", ["victory"]))
        self.assertIn("victory: won", r["state"])
        # Every later play tool is refused; the log tail and disconnect still work.
        refused = self.call("submit_orders", {"orders": []})
        self.assertTrue(refused.get("isError"))
        self.assertIn("the level is over (won)", refused["content"][0]["text"])
        self.assertFalse(self.call("get_log_tail", {"lines": 2}).get("isError"))
        self.assertFalse(self.call("disconnect").get("isError"))

    def test_game_ended_push_ends_the_session_while_waiting(self):
        self._connect_and_start()
        threading.Timer(0.2, self.game.push_game_ended).start()
        r = json.loads(self.call("wait_for_decision", {"timeout_seconds": 3})["content"][0]["text"])
        self.assertEqual((r["game_over"], r["victory_state"], r["reasons"]), (True, "ended", ["game_ended"]))
        self.assertTrue(self.call("status").get("isError"))
        self.assertFalse(self.call("disconnect").get("isError"))

    def test_the_game_keeps_the_memory_and_a_reconnect_resumes_it(self):
        self._connect_and_start()
        self.call("submit_orders", {"orders": [], "reasoning": "r", "plan": "take the gold seam", "notes": "enemy north"})
        held = json.loads(self.game.memory[1])
        self.assertEqual((held["plan"], held["notes"], held["count"]), ("take the gold seam", "enemy north", 1))
        self.call("disconnect")
        self.game.close()
        # A new bridge process against the same running game: the plan comes back, and the first decision says restart.
        game = FakeGame()
        game.memory = dict(self.game.memory)
        self.game = game
        self.server = mcp_server.Server()
        self.call("connect", {"host": "127.0.0.1", "port": game.port})
        r = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])
        self.assertEqual(r["reasons"], ["restart"])
        self.assertIn("take the gold seam", r["state"])
        self.assertEqual(json.loads(game.memory[1])["game_id"], held["game_id"])
        self.call("disconnect")

    def test_a_new_game_gets_a_fresh_memory_and_a_game_id(self):
        self._connect_and_start()
        held = json.loads(self.game.memory[1])
        self.assertTrue(held["game_id"] and held["branch"])
        self.assertEqual((held["plan"], held["parent_branch"], held["level"]["key"]), ("", None, "keeporig:7"))
        self.call("disconnect")

    def _exp_path(self):
        d = tempfile.mkdtemp(prefix="kfx-mcp-exp-")
        self.addCleanup(shutil.rmtree, d, True)
        return os.path.join(d, "experience.sqlite")

    def test_a_won_game_is_recorded_debriefed_and_its_lessons_come_back_next_game(self):
        path = self._exp_path()
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": path, "agent": "Claude"})
        self.assertEqual(self.game.names[1], "Claude")                  # the seat took the agent's name
        first = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])
        self.assertIn("EXPERIENCE", first["state"])
        self.assertIn("never played before", first["state"])
        self.assertIn("PLAYERS: 0 Robin (human)", first["state"])
        self.assertIn("2 Player 2 (computer, AI: Build and defend Computer)", first["state"])
        refused = self.call("record_debrief", {"summary": "too early"})
        self.assertTrue(refused.get("isError"))                          # not before the game ends
        self.call("submit_orders", {"orders": [], "reasoning": "r", "plan": "rush the heart"})
        self.game.victory_state = "won"
        self.game.push_event(["victory"])
        over = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertEqual((over["game_over"], over["record"]), (True, "won"))
        self.assertIn("DEBRIEF", over["debrief"])
        self.assertIn("rush the heart", over["debrief"])
        self.assertIn("[opponent:computer:Build and defend Computer", over["debrief"])
        self.assertIn("[opponent:human:Robin", over["debrief"])
        r = json.loads(self.call("record_debrief", {
            "summary": "Won by rushing.", "playbook": "Build a treasure room first.", "level_lessons": "The gold is north.",
            "opponent_lessons": {"human:Robin": "Robin turtles.", "human:Nobody": "?"}})["content"][0]["text"])
        self.assertEqual(r["stored"], ["summary", "general", "level:keeporig:7", "opponent:human:Robin"])
        self.assertIn("no opponent 'human:Nobody'", r["warnings"][0])
        self.assertTrue(self.call("record_debrief", {"summary": "again"}).get("isError"))   # one debrief per game
        exp = self.call("get_experience")["content"][0]["text"]
        self.assertIn("Build a treasure room first.", exp)
        self.assertIn("won t", exp)
        self.call("disconnect")

        # The next game on the same level: a fresh game (no memory in it), the lessons and the record shown at its start.
        self.game.close()
        self.game = FakeGame()
        self.server = mcp_server.Server()
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": path})
        nxt = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])
        self.assertEqual(nxt["reasons"], ["start"])
        self.assertIn("1 earlier game(s): won", nxt["state"])
        self.assertIn("vs Robin (human): 1 won", nxt["state"])
        self.assertIn("PLAYBOOK (any level):\n    Build a treasure room first.", nxt["state"])
        self.assertIn("The gold is north.", nxt["state"])
        self.assertIn("Robin turtles.", nxt["state"])
        self.game.push_event(["quarter_2"])
        later = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertIn("EXPERIENCE: 1 earlier game(s) on this level (1 won)", later["state"])   # one line after the first
        self.assertNotIn("Build a treasure room first.", later["state"])
        self.call("disconnect")

    def test_leaving_right_after_a_save_suspends_the_game_without_a_debrief(self):
        path = self._exp_path()
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": path})
        self.call("wait_for_decision", {"timeout_seconds": 0.2})
        self.game.save()
        self.game.push_game_ended()
        over = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertEqual((over["game_over"], over["record"]), (True, "suspended"))
        self.assertNotIn("debrief", over)
        self.assertTrue(self.call("record_debrief", {"summary": "x"}).get("isError"))
        self.call("disconnect")

    def test_a_session_without_experience(self):
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": ""})
        first = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])
        self.assertNotIn("EXPERIENCE", first["state"])
        self.assertIn("no experience store", self.call("get_experience")["content"][0]["text"])
        self.call("disconnect")

    def test_a_load_brings_back_the_plan_as_saved(self):
        self._connect_and_start()
        self.call("submit_orders", {"orders": [], "reasoning": "r", "plan": "plan A"})
        before = json.loads(self.game.memory[1])
        self.game.save()
        # A save alone is not a decision; its turn is noted.
        r = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.3})["content"][0]["text"])
        self.assertFalse(r["due"])
        self.assertEqual(self.server.session.last_save_turn, self.game.saved[0])
        self.call("submit_orders", {"orders": [], "reasoning": "r", "plan": "plan B"})
        self.assertEqual(json.loads(self.game.memory[1])["plan"], "plan B")
        # The load arrives together with a decision that was due in the game that is now gone.
        self.game.push_event(["enemy_fight"])
        self.game.load()
        r = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertEqual(r["reasons"], ["reloaded"])
        self.assertIn("The game was reloaded to turn", r["state"])
        self.assertIn("plan A", r["state"])
        self.assertNotIn("plan B", r["state"])
        after = json.loads(self.game.memory[1])
        self.assertEqual((after["plan"], after["game_id"], after["parent_branch"]), ("plan A", before["game_id"], before["branch"]))
        self.assertNotEqual(after["branch"], before["branch"])     # a new attempt at the same game
        self.call("disconnect")

    def test_loading_a_save_without_memory_starts_afresh(self):
        self._connect_and_start()
        self.call("submit_orders", {"orders": [], "reasoning": "r", "plan": "plan A"})
        first_game = json.loads(self.game.memory[1])["game_id"]
        self.game.saved = (5, {})       # a save made with no agent
        self.game.load()
        r = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertEqual(r["reasons"], ["reloaded"])
        self.assertIn("holds no memory of yours", r["state"])
        self.assertNotIn("plan A", r["state"])
        self.assertNotEqual(json.loads(self.game.memory[1])["game_id"], first_game)
        self.call("disconnect")

    def test_connect_wait_status_look_submit_disconnect(self):
        r = self.call("connect", {"host": "127.0.0.1", "port": self.game.port})
        self.assertFalse(r.get("isError"), r)
        self.assertIn("connected: player 1", r["content"][0]["text"])

        # The first call after connect always decides at once ("start"), whatever else may already be pending.
        r = self.call("wait_for_decision", {"timeout_seconds": 0.2})
        first = json.loads(r["content"][0]["text"])
        self.assertEqual((first["due"], first["reasons"]), (True, ["start"]))

        # The next one really does wait for the game, and reports due=False if nothing arrives in time.
        r = self.call("wait_for_decision", {"timeout_seconds": 0.2})
        self.assertEqual(json.loads(r["content"][0]["text"])["due"], False)

        threading.Timer(0.05, self.game.push_event, args=(["quarter_1"],)).start()
        r = self.call("wait_for_decision", {"timeout_seconds": 3.0})
        data = json.loads(r["content"][0]["text"])
        self.assertTrue(data["due"], data)
        self.assertEqual(data["reasons"], ["quarter_1"])
        self.assertIn("DECISION DUE because: quarter_1", data["state"])
        self.assertIn("YOUR PLAN", data["state"])

        r = self.call("status")
        self.assertIn("turn", r["content"][0]["text"])

        r = self.call("look", {"slab_rect": [0, 0, 3, 3]})
        self.assertIn("legend", r["content"][0]["text"])

        r = self.call("submit_orders", {"orders": [{"verb": "mark_dig", "slab_rect": [1, 1, 2, 2]}],
                                         "reasoning": "dig it", "plan": "grow the dungeon"})
        result = json.loads(r["content"][0]["text"])
        self.assertEqual(len(result["sent"]), 1)
        self.assertEqual(self.game.submitted[-1]["verb"], "mark_dig")

        r = self.call("connect", {"host": "127.0.0.1", "port": self.game.port})
        self.assertTrue(r.get("isError"))
        self.assertIn("already connected", r["content"][0]["text"])

        r = self.call("disconnect")
        self.assertEqual(r["content"][0]["text"], "disconnected")
        r = self.call("status")
        self.assertTrue(r.get("isError"))

    def test_check_orders_and_set_game_speed_against_a_real_session(self):
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port})
        self.call("wait_for_decision", {"timeout_seconds": 0.1})  # consume the "start" decision

        r = self.call("check_orders", {"orders": [{"verb": "mark_dig", "slab_rect": [1, 1, 2, 2]}]})
        result = json.loads(r["content"][0]["text"])
        self.assertEqual(result["would_succeed"][0]["would_succeed"], True)
        self.assertEqual(result["would_fail"], [])
        self.assertEqual(self.game.submitted, [], "a dry run must not reach the real submission list")

        r = self.call("set_game_speed", {"turns_per_second": 7})
        self.assertEqual(r["content"][0]["text"], "turns_per_second is now 7")
        self.assertEqual(self.game.turns_per_second, 7)

        r = self.call("get_log_tail", {"lines": 3})
        self.assertEqual(r["content"][0]["text"], "fake log line 0\nfake log line 1\nfake log line 2")

        self.call("disconnect")

    def test_wait_for_decision_can_treat_a_timeout_as_a_decision(self):
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port})
        self.call("wait_for_decision", {"timeout_seconds": 0.1})  # the first call is always "start"; get it out of the way
        r = self.call("wait_for_decision", {"timeout_seconds": 0.1, "treat_timeout_as_decision": True})
        data = json.loads(r["content"][0]["text"])
        self.assertEqual(data["reasons"], ["timer"])
        self.call("disconnect")


class SubprocessTransportTest(unittest.TestCase):
    """Proves the actual stdio JSON-RPC framing works, not just the in-process Server.handle logic."""

    def test_initialize_and_tools_list_over_real_stdio(self):
        server_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mcp_server.py")
        proc = subprocess.Popen([sys.executable, server_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True, bufsize=1)
        try:
            def send(obj):
                proc.stdin.write(json.dumps(obj) + "\n")
                proc.stdin.flush()

            def recv():
                line = proc.stdout.readline()
                if not line:
                    # only touch stderr on failure: proc.stderr.read() blocks until EOF, i.e. until the child exits,
                    # so evaluating it unconditionally (e.g. as assertTrue's eagerly-evaluated message argument) would
                    # hang every successful call waiting for a process that is still running.
                    self.fail("no output from the server (stderr: %s)" % proc.stderr.read())
                return json.loads(line)

            send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
            resp = recv()
            self.assertEqual(resp["result"]["serverInfo"]["name"], "daimonkeeper-bridge")

            send({"jsonrpc": "2.0", "method": "notifications/initialized"})  # no reply

            send({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
            resp = recv()
            self.assertIn("submit_orders", {t["name"] for t in resp["result"]["tools"]})

            send({"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "get_instructions", "arguments": {}}})
            resp = recv()
            self.assertIn("Dungeon Keeper", resp["result"]["content"][0]["text"])

            send({"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "status", "arguments": {}}})
            resp = recv()
            self.assertTrue(resp["result"].get("isError"))
            self.assertIn("not connected", resp["result"]["content"][0]["text"])

            proc.stdin.write("not even json\n")
            proc.stdin.flush()
            resp = recv()
            self.assertEqual(resp["error"]["code"], -32700)
        finally:
            proc.stdin.close()
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)
            proc.stdout.close()
            proc.stderr.close()


if __name__ == "__main__":
    unittest.main()
