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
import time
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

    def submit(self, orders, reasoning="", plan=None, notes=None, lessons=None, facts=None, chapter_note=None, places=None):
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

    def disconnect(self, end=None):
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
                                  "check_orders", "set_game_speed", "set_pause", "get_log_tail", "get_combat_stats", "get_recipes",
                                  "get_experience", "get_debrief", "record_debrief", "remember_lesson", "confirm_lesson", "doubt_lesson", "name_place", "disconnect", "set_combat_rules", "check_combat_rules",
                                  "get_combat_rules", "apply_combat_preset", "use_combat_bank", "set_triggers", "check_triggers",
                                  "get_triggers", "apply_trigger_preset", "set_rally_points"})
        for t in resp["result"]["tools"]:
            self.assertIn("inputSchema", t)

    def test_connect_schema_names_only_real_session_arguments(self):
        import inspect
        from mcp_session import Session
        params = set(inspect.signature(Session.connect).parameters) - {"self"}
        schema = next(t for t in mcp_server.TOOLS if t["name"] == "connect")["inputSchema"]["properties"]
        self.assertLessEqual(set(schema), params)
        self.assertIn("max_age_turns", schema)

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

    def test_a_missing_argument_is_named_not_an_unknown_tool(self):
        r = self.server.handle({"jsonrpc": "2.0", "id": 9, "method": "tools/call", "params": {"name": "look", "arguments": {}}})
        text = r["result"]["content"][0]["text"]
        self.assertTrue(r["result"].get("isError"))
        self.assertIn("look: missing argument slab_rect", text)
        self.assertNotIn("unknown tool", text)

    def test_unknown_tool(self):
        r = self.call("not_a_real_tool", {})
        self.assertTrue(r.get("isError"))
        self.assertIn("unknown tool", r["content"][0]["text"])

    def test_missing_arguments_defaults_to_empty(self):
        # arguments omitted entirely (None) must not crash a tool that only has optional fields.
        r = self.server.handle({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                                 "params": {"name": "get_instructions"}})["result"]
        self.assertEqual(r["content"][0]["text"], "SYSTEM PROMPT TEXT")


def fake_combat_stats(player, turn):
    """get_combat_stats as the game answers it (api_seat_combatstats.c), with two fights and one death."""
    return {"player": player, "since_turn": 10, "turn": turn, "low_pct": 35, "critical_pct": 20,
            "fights": {"count": 2, "turns": 300, "with_cast": 1, "median_first_cast_after": 140,
                       "median_decision_after": 40, "median_orders_after": 160,
                       "recent": [{"start": 100, "end": 250, "casts": 1}, {"start": 400, "end": 550, "casts": 0}]},
            "deaths": {"total": 1, "combat": 1, "combat_diggers": 0, "seen_low": 1, "healed": 0,
                       "median_window_low": 60, "median_window_critical": -1, "recent": []},
            "casts": {"count": 1, "gold": 300, "by_power": {"POWER_HEAL_CREATURE": {"count": 1, "gold": 300}}},
            "upkeep": {"cta_gold": 120, "cta_paying_turns": 120, "obey_gold": 0, "obey_turns": 0},
            "paydays": {"count": 1, "short": 0, "recent": []},
            "decisions": {"count": 5, "answered": 4, "median_latency": 180, "combat_answered": 1,
                          "median_latency_combat": 120, "recent": []}}


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
        self.combat_calls = 0    # get_combat_stats requests
        self.assist = {"enabled": False, "interval": 2, "charge_time": True, "min_gap": 4, "reserve": 0, "max_spend": 0,
                       "spent_this_payday": 0, "rules": [], "casts": []}
        self.assist_requests = []   # set_assist requests, real ones only
        self.assist_deferred = False  # set_assist queued but not live until apply_deferred_assist (a paused game)
        self._deferred_rules = None
        self.results = []           # seat.results the view reports (a refused order)
        self.map_rows = None     # replaces make_view's map rows when set (a slab changed in the game)
        self.agent_only = False  # no human plays a keeper: the agent may pause
        self.paused = False
        self.agent_pause = False
        self.pause_requests = []  # set_pause / advance_turns requests
        self.drop_queries = []    # find_drop_spot positions asked
        self.bank_uses = []       # use_assist_bank banks
        self.acks = []            # ack_decision seqs
        self.policies = []        # set_decision_policy requests
        self._write_lock = threading.Lock()
        self._buf = b""
        self._thread = threading.Thread(target=self._accept, daemon=True)
        self._thread.start()

    def apply_deferred_assist(self):
        rules, self._deferred_rules, self.assist_deferred = self._deferred_rules, None, False
        if rules is not None:
            self._handle({"action": "set_assist", "rules": rules, "ack": 0})

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
        if action == "set_decision_policy":
            self.policies.append(req)
            if req.get("on_urgent") in ("pause", "slow") and not self.agent_only:
                return {"ack": ack, "success": False, "error": "HUMAN_PLAYING"}
            return {"ack": ack, "success": True}
        if action in ("subscribe_event", "set_takeover"):
            return {"ack": ack, "success": True}
        if action == "get_player_view":
            self.turn += 1
            self.view_id += 1
            v = make_view(turn=self.turn)
            if self.map_rows is not None:
                v["map"]["rows"] = list(self.map_rows)
            v["seat"]["victory_state"] = self.victory_state
            v["level"] = {"number": 7, "campaign": "keeporig.cfg", "name": "Flowerhat"}
            v["players"] = [{"id": 0, "player": 0, "kind": "human", "name": self.names.get(0), "alive": True},
                            {"id": 1, "player": 1, "kind": "external", "name": self.names.get(1, "External 1"), "alive": True},
                            {"id": 2, "player": 2, "kind": "computer", "ai_type": "Build and defend Computer.", "alive": True}]
            v["view_id"] = self.view_id
            v["seat"]["results"] = list(self.results)
            v["paused"], v["agent_pause"], v["agent_only"] = self.paused, self.agent_pause, self.agent_only
            v["mode"] = "full"
            v["own"]["assist"] = json.loads(json.dumps(self.assist))
            v["own"]["creatures"].append({"id": 8, "kind": "IMP", "level": 1, "health": 60, "health_pct": 100, "digger": True,
                                          "pos": [5, 5], "state": "Dig"})
            return {"ack": ack, "success": True, "data": v}
        if action == "find_drop_spot":
            self.drop_queries.append(req["pos"])
            return {"ack": ack, "success": True, "data": {"pos": [req["pos"][0] + 1, req["pos"][1]], "distance": 1}}
        if action == "ack_decision":
            self.acks.append(req.get("seq"))
            return {"ack": ack, "success": True}
        if action == "use_assist_bank":
            self.bank_uses.append(req["bank"])
            return {"ack": ack, "success": True, "data": {"id": 50, "steps": 1, "queued_behind": 0}}
        if action == "submit_action":
            if req.get("dry_run"):
                return {"ack": ack, "success": True, "data": {"would_succeed": True, "steps": 1}}
            self.submitted.append(req)
            return {"ack": ack, "success": True, "data": {"id": len(self.submitted), "queued_behind": 0, "steps": 1}}
        if action in ("set_pause", "advance_turns"):
            self.pause_requests.append(req)
            if not self.agent_only and (req.get("paused") or action == "advance_turns"):
                return {"ack": ack, "success": False, "error": "HUMAN_PLAYING"}
            if action == "set_pause":
                self.paused = self.agent_pause = bool(req["paused"])
            return {"ack": ack, "success": True}
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
        if action == "set_assist":
            rules = req.get("rules")
            for i, r in enumerate(rules or []):
                if r.get("power") == "POWER_NOPE":
                    return {"ack": ack, "success": False, "error": "UNKNOWN_POWER", "rule": i}
            n = len(rules or [])
            if req.get("dry_run"):
                return {"ack": ack, "success": True, "data": {"would_succeed": True, "steps": 3 * n + 1, "rules_changed": n}}
            self.assist_requests.append(req)
            for k in ("enabled", "interval", "reserve", "min_gap", "max_spend", "charge_time"):
                if k in req:
                    self.assist[k] = req[k]
            if rules is not None and self.assist_deferred:
                self._deferred_rules = rules
            elif rules is not None:
                self.assist["rules"] = [dict({k: v for k, v in r.items() if k != "name"}, slot=i, fired=0, spent=0, state="ok",
                                             level=r.get("level", 0), target=r.get("target", "ally_in_fight"))
                                        if "power" in r else dict({k: v for k, v in r.items() if k != "name"}, slot=i, fired=0, spent=0, state="ok")
                                        for i, r in enumerate(rules)]
            return {"ack": ack, "success": True, "data": {"steps": 3 * n + 1, "rules_changed": n, "id": 1, "queued_behind": 0}}
        if action == "get_assist_presets":
            return {"ack": ack, "success": True, "data": {"presets": {
                "guardian": {"description": "heal and guard", "enabled": True, "reserve": 1000, "rules": [
                    {"name": "heal", "power": "POWER_HEAL_CREATURE", "level": 2, "when": [["health_pct", "<", 35]]},
                    {"name": "cta_floor", "power_off": "POWER_CALL_TO_ARMS", "when": [["gold", "<", 1000]]}]},
                "miser": {"description": "guards only", "enabled": True, "rules": [
                    {"name": "obey_floor", "power_off": "POWER_OBEY", "when": [["gold", "<", 1000]]}]}}}}
        if action == "get_combat_stats":
            self.combat_calls += 1
            return {"ack": ack, "success": True, "data": fake_combat_stats(req["player"], self.turn)}
        if action == "get_log_tail":
            want = req.get("lines", 100)
            return {"ack": ack, "success": True, "data": {"lines": ["fake log line %d" % i for i in range(want)]}}
        return {"ack": ack, "success": False, "error": "UNKNOWN_ACTION"}

    def push_event(self, reasons, severity=None, where=None):
        data = {"reasons": ",".join(reasons), "seq": 1, "turn": self.turn, "quarter": 1}
        if severity is not None:
            data["severity"] = severity
        if where is not None:
            data["where_x"], data["where_y"] = where
        self._send({"event": "DECISION_DUE", "data": data})

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

    def _text(self, name, args=None):
        return self.call(name, args)["content"][0]["text"]

    def test_the_pump_reads_the_game_between_tool_calls(self):
        self._connect_and_start()
        session = self.server.session
        self.game.push_event(["quarter_2"])
        deadline = time.time() + 3
        while not session.link.events and time.time() < deadline:
            time.sleep(0.02)     # no tool call in flight: only the pump can have read it
        self.assertEqual([e["event"] for e in session.link.events], ["DECISION_DUE"])
        started = time.time()
        d = json.loads(self._text("wait_for_decision", {"timeout_seconds": 3}))
        self.assertLess(time.time() - started, 0.5, "the decision was already in")
        self.assertEqual((d["reasons"], d["severity"], d["flash"]), (["quarter_2"], 3, False))

    def test_an_urgent_decision_is_a_flash_and_the_engine_severity_and_place_are_used(self):
        self._connect_and_start()
        self.game.push_event(["information"], severity=0, where=[13, 7])     # the engine's word goes
        d = json.loads(self._text("wait_for_decision", {"timeout_seconds": 3}))
        self.assertEqual((d["severity"], d["flash"], d["where"]), (0, True, [13, 7]))
        self.assertIn("FLASH DECISION (severity 0, critical)", d["state"])
        self.assertIn("WHERE: subtile (13,7) = slab (4,2)", d["state"])
        self.assertNotIn("YOUR NOTES", d["state"])
        self.assertLess(len(d["state"]), 2500)
        full = self._text("status", {"full": True})
        self.assertIn("CREATURES (", full)   # the fake game's view is tiny: no size comparison with the flash

    def test_submit_hands_back_an_urgent_decision_that_came_meanwhile(self):
        self._connect_and_start()
        session = self.server.session
        self.game.push_event(["quarter_1"])
        d = json.loads(self._text("wait_for_decision", {"timeout_seconds": 3}))
        decided_turn = d["turn"]
        self.game.push_event(["enemy_fight"])          # while the model thinks about the quarter
        deadline = time.time() + 3
        while not session.link.urgent_pending() and time.time() < deadline:
            time.sleep(0.02)
        r = json.loads(self._text("submit_orders", {"orders": [{"verb": "mark_dig", "slab_rect": [1, 1, 2, 2]}]}))
        self.assertEqual(self.game.submitted[-1]["view_turn"], decided_turn, "orders count from the view the decision showed")
        self.assertIn("since your view:", r["since_your_view"])
        nxt = r["next_decision"]
        self.assertEqual((nxt["reasons"], nxt["flash"]), (["enemy_fight"], True))
        # Not answered, the next wait hands it out again rather than letting it go.
        again = json.loads(self._text("wait_for_decision", {"timeout_seconds": 0.5}))
        self.assertEqual((again["reasons"], again.get("repeated")), (["enemy_fight"], True))
        self.assertIn("next_decision", again["note"])
        r2 = json.loads(self._text("submit_orders", {"orders": []}))
        self.assertNotIn("next_decision", r2)
        self.assertFalse(json.loads(self._text("wait_for_decision", {"timeout_seconds": 0.3}))["due"])

    def test_a_trigger_fires_from_the_pump_with_no_tool_call_in_flight(self):
        self._connect_and_start()
        session = self.server.session
        self.assertEqual(json.loads(self._text("apply_trigger_preset", {"name": "evacuate_imps"}))["error"], "NEEDS_RALLY")
        self.assertEqual(json.loads(self._text("set_rally_points", {"points": {"safe": [2, 2]}}))["rally"], {"safe": [2, 2]})
        r = json.loads(self._text("apply_trigger_preset", {"name": "evacuate_imps"}))
        self.assertEqual(r["triggers"], ["evacuate_imps"])
        before = len(self.game.submitted)
        self.game.push_event(["intruder"], severity=1, where=[6, 6])
        deadline = time.time() + 3
        while len(self.game.submitted) == before and time.time() < deadline:
            time.sleep(0.02)
        lift = self.game.submitted[-1]
        self.assertEqual((lift["verb"], lift["thing_ids"], lift["priority"], lift["max_age_turns"]),
                         ("pick_up_and_drop", [8], "now", 200))
        self.assertEqual((lift["pos"], self.game.drop_queries[-1]), ([3, 2], [2, 2]))   # the drop spot the game gave
        d = json.loads(self._text("wait_for_decision", {"timeout_seconds": 2}))
        self.assertIn("FIRED turn", d["state"])
        self.assertIn("evacuate_imps", d["state"])
        # Kept with the agent's memory, which the game holds.
        blob = json.loads(self.game.memory[1])
        self.assertEqual((blob["triggers"][0]["name"], blob["triggers"][0]["fired"], blob["rally"]), ("evacuate_imps", 1, {"safe": [2, 2]}))
        got = json.loads(self._text("get_triggers"))
        self.assertEqual(got["triggers"][0]["fired"], 1)
        self.assertIn("evacuate_imps", got["presets"])
        self.assertEqual(json.loads(self._text("set_triggers", {"triggers": [{"name": "x", "do": [{"verb": "dance"}]}]}))["error"], "BAD_ORDER")
        self.assertEqual(session.memory.triggers[0]["name"], "evacuate_imps", "a refused list changes nothing")

    def test_a_bank_is_stored_named_and_switched_to(self):
        self._connect_and_start()
        rule = {"name": "panic_heal", "power": "POWER_HEAL_CREATURE", "level": 2, "when": [["health_pct", "<", 50]]}
        r = json.loads(self._text("set_combat_rules", {"rules": [rule], "bank": 2, "label": "war", "max_spend": 3000}))
        self.assertNotIn("error", r)
        self.assertEqual(self.game.assist_requests[-1]["bank"], 2)
        self.assertEqual(self.server.session.memory.bank_names["2"], {"label": "war", "names": ["panic_heal"]})
        r = json.loads(self._text("use_combat_bank", {"bank": 2}))
        self.assertEqual((r["bank"], r["label"], self.game.bank_uses), (2, "war", [2]))
        self.assertEqual(self.server.session.memory.pending_rules["names"], ["panic_heal"])

    def test_an_answer_to_a_flash_is_urgent_by_default(self):
        self._connect_and_start()
        self.game.push_event(["enemy_fight"])
        d = json.loads(self._text("wait_for_decision", {"timeout_seconds": 3}))
        self.assertTrue(d["flash"])
        self._text("submit_orders", {"orders": [{"verb": "slap", "thing_id": 7}, {"verb": "mark_dig", "slab_rect": [1, 1, 1, 1], "priority": "routine"}]})
        self.assertEqual([r.get("priority") for r in self.game.submitted[-2:]], ["now", "routine"])
        self.assertEqual(self.game.submitted[-2]["max_age_turns"], 200)

    def test_on_urgent_auto_pauses_only_where_the_agent_may_and_answers_ack_the_decision(self):
        # A human plays: auto holds nothing, and an explicit pause is refused.
        self._connect_and_start()
        self.assertEqual(self.server.session.on_urgent, "none")
        self.assertFalse(any(p.get("on_urgent") for p in self.game.policies))
        self._text("disconnect")
        self.game.close()
        self.game = FakeGame()
        self.game.agent_only = True
        r = self.call("connect", {"host": "127.0.0.1", "port": self.game.port})
        self.assertIn("pauses the game until you answer it", r["content"][0]["text"])
        self.assertEqual(self.game.policies[-1]["on_urgent"], "pause")
        session = self.server.session
        self.assertEqual(session.on_urgent, "pause")
        self._text("wait_for_decision", {"timeout_seconds": 0.2})
        self._text("set_pause", {"paused": False})
        self.game.push_event(["enemy_fight"])
        d = json.loads(self._text("wait_for_decision", {"timeout_seconds": 3}))
        self.assertTrue(d["flash"])
        self._text("submit_orders", {"orders": [{"verb": "slap", "thing_id": 7}]})
        self.assertEqual(self.game.submitted[-1]["decision_seq"], 1)        # the order says which decision it answers
        self.assertEqual(self.game.acks[-1], 1)                             # and the decision is acked, orders or not
        self._text("submit_orders", {"orders": []})
        self.assertEqual(len(self.game.acks), 2)

    def test_the_text_says_when_the_game_is_held(self):
        import prompt
        from test_bridge import make_view
        from viewstate import ViewState
        st = ViewState()
        st.view = make_view()
        st.view["seat"]["decision"] = {"hold": {"mode": "pause", "ms_left": 41500}}
        self.assertIn("THE GAME IS PAUSED FOR YOUR ANSWER (at most 41 s more)", prompt.render_flash(st, None, ["intruder"], 1))
        st.view["seat"]["decision"] = {"hold": {"mode": "slow", "ms_left": 9000}}
        st.view["turns_per_second"] = 4
        self.assertIn("SLOWED FOR YOUR ANSWER (4 turns a second", prompt.render_state(st, reasons=["intruder"]))

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
        self.assertIn("[scope opponent:computer:Build and defend Computer", over["debrief"])
        self.assertIn("[scope opponent:human:Robin", over["debrief"])
        r = json.loads(self.call("record_debrief", {
            "summary": "Won by rushing.",
            "add": [{"scope": "playbook", "text": "Build a treasure room first."}, {"scope": "level", "text": "The gold is north."},
                    {"scope": "opponent:human:Robin", "text": "Robin turtles."}, {"scope": "opponent:human:Nobody", "text": "?"}]})["content"][0]["text"])
        self.assertEqual(r["stored"], ["summary", "general", "level:keeporig:7", "opponent:human:Robin"])
        self.assertIn("no scope 'opponent:human:Nobody'", r["warnings"][0])
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
        self.assertRegex(nxt["state"], r"PLAYBOOK \(any level\):\n    \[L\d+\] Build a treasure room first\.")
        self.assertIn("The gold is north.", nxt["state"])
        self.assertIn("Robin turtles.", nxt["state"])
        self.game.push_event(["quarter_2"])
        later = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertIn("EXPERIENCE: 1 earlier game(s) on this level (1 won)", later["state"])   # one line after the first
        self.assertNotIn("Build a treasure room first.", later["state"])
        self.call("disconnect")

    def test_an_abandoned_attempt_is_offered_at_the_next_game_and_debriefed_by_reference(self):
        import experience
        from memory import Memory
        path = self._exp_path()
        store = experience.Experience(path)
        old = Memory.new_game({"turn": 0, "level": {"number": 7, "campaign": "keeporig.cfg", "name": "Flowerhat"}})
        old.plan = "hold the bridge"
        store.open_branch(old, 1, "Claude", turn=0)
        store.progress(old, 6000)
        store.db.execute("UPDATE games SET updated_at=updated_at-3600")      # left an hour ago, never closed
        store.close()
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": path, "agent": "Claude"})
        first = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])
        ref = "%s/%s" % (old.game_id[:8], old.branch)
        self.assertIn("EARLIER ATTEMPTS WITHOUT A DEBRIEF", first["state"])
        self.assertIn(ref + ": keeporig:7 Flowerhat, abandoned at turn 6000", first["state"])
        deb = self.call("get_debrief", {"attempt": ref})["content"][0]["text"]
        self.assertIn("YOUR FINAL PLAN: hold the bridge", deb)
        self.assertTrue(self.call("get_debrief", {"attempt": "nope/000000000000"}).get("isError"))
        r = json.loads(self.call("record_debrief", {"attempt": ref, "summary": "lost the bridge",
                                                    "add": [{"scope": "level", "text": "Wall the bridge end."}]})["content"][0]["text"])
        self.assertEqual(r["stored"], ["summary", "level:keeporig:7"])
        self.call("disconnect")
        self.game.close()
        self.game = FakeGame()
        self.server = mcp_server.Server()
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": path, "agent": "Claude"})
        nxt = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])
        self.assertNotIn("EARLIER ATTEMPTS WITHOUT A DEBRIEF", nxt["state"])     # debriefed: not offered again
        self.assertIn("Wall the bridge end.", nxt["state"])
        self.assertIn("abandoned t6000", nxt["state"])
        self.call("disconnect")

    def test_lessons_come_back_in_a_fight_and_new_ones_are_noted_in_play(self):
        import experience
        path = self._exp_path()
        store = experience.Experience(path)
        heal, _ = store.add_item("general", "Heal the tank before it drops under a third.", "combat")
        store.close()
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": path, "agent": "Claude"})
        first = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])
        self.assertIn("[L%d] Heal the tank" % heal, first["state"])
        r = self._tool("submit_orders", {"orders": [], "reasoning": "r", "lessons": {
            "remember": [{"text": "The north corridor floods with heroes at the first pay day."}], "confirm": [heal]}})
        new_id = r["lessons"]["remembered"][0]
        self.assertEqual(r["lessons"]["confirmed"], [heal])
        self.server.session.memory.count = 30                # well past the full EXPERIENCE: recall is due again
        self.game.push_event(["enemy_fight"], severity=1)
        fight = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertTrue(fight["flash"])
        self.assertIn("RECALL (combat:", fight["state"])
        self.assertIn("[L%d] Heal the tank" % heal, fight["state"])
        self.call("submit_orders", {"orders": []})
        r = self._tool("remember_lesson", {"text": "Lightning misses moving targets.", "topic": "combat"})
        self.assertEqual(r["status"], "provisional")
        self.assertTrue(self.call("confirm_lesson", {"id": 99999}).get("isError"))
        self.assertFalse(self.call("doubt_lesson", {"id": heal, "why": "it died healed"}).get("isError"))
        combat = self.call("get_experience", {"topic": "combat"})["content"][0]["text"]
        self.assertIn("[L%d] (new) Lightning misses moving targets." % r["id"], combat)
        self.assertNotIn("north corridor", combat)
        self.assertTrue(self.call("get_experience", {"topic": "nope"}).get("isError"))
        self.game.victory_state = "won"
        self.game.push_event(["victory"])
        over = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertIn("LESSONS NOTED DURING PLAY, NOT KEPT YET", over["debrief"])
        self.assertIn("[L%d] (new, economy) The north corridor" % new_id, over["debrief"])
        self.assertIn("(shown 2x, combat, confirmed 1, doubted 1) Heal the tank", over["debrief"])
        self.call("disconnect")

    def test_places_facts_and_chapter_notes_through_the_tools(self):
        self._connect_and_start()
        r = self._tool("name_place", {"name": "north gap", "pos": [30, 12]})
        self.assertEqual((r["name"], r["place"]["pos"]), ("north_gap", [30, 12]))
        out = self._tool("submit_orders", {
            "orders": [{"verb": "move_creature", "thing_id": 8, "pos": "north_gap"},
                       {"verb": "pick_up_and_drop", "thing_ids": [8], "to_room": "heart"},
                       {"verb": "cast_power", "power": "POWER_SIGHT", "pos": "nowhere"}],
            "reasoning": "r", "facts": {"enemy heart": "x46 y39"}, "chapter_note": "scouted the north",
            "places": {"bridge end": [40, 12, 41, 12]}})
        self.assertEqual(out["places"]["named"], ["bridge_end"])
        self.assertEqual([o.get("pos") or o.get("to_room") for o in self.game.submitted[-2:]], [[30, 12], 1])
        self.assertTrue(out["refused"][0]["error"].startswith("UNKNOWN_PLACE"))
        mem = self.server.session.memory
        self.assertEqual(mem.facts["enemy_heart"]["text"], "x46 y39")
        self.assertEqual(list(mem.chapter_notes.values()), ["scouted the north"])
        self.game.push_event(["quarter_2"])
        nxt = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertIn("enemy_heart = x46 y39", nxt["state"])
        self.assertIn("north_gap (30,12)", nxt["state"])
        self.assertIn("bridge_end (slabs 40-41,12-12)", nxt["state"])
        self.assertIn("heart (4,4)", nxt["state"])
        self.assertIn("your note: scouted the north", nxt["state"])
        r = self.call("set_combat_rules", {"rules": [{"name": "evac", "hand": "evacuate_diggers", "to": "nowhere"}]})
        self.assertIn("UNKNOWN_PLACE", r["content"][0]["text"])
        self.call("set_combat_rules", {"rules": [{"name": "evac", "hand": "evacuate_diggers", "to": "north_gap"}]})
        self.assertEqual(self.game.assist_requests[-1]["rules"][0]["to"], [30, 12])
        self.call("disconnect")

    def test_disconnect_can_end_the_attempt_with_a_debrief_or_keep_it_for_later(self):
        path = self._exp_path()
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": path, "agent": "Claude"})
        self.call("wait_for_decision", {"timeout_seconds": 0.2})
        self.game.turn = 12000                                       # past a pay day (10000 turns in this game)
        self.call("status")
        out = json.loads(self.call("disconnect", {"end": "quit"})["content"][0]["text"])
        self.assertEqual((out["record"], out["connected"]), ("quit", True))
        self.assertIn("DEBRIEF: the game is over (quit", out["debrief"])
        self.assertTrue(self.call("submit_orders", {"orders": []}).get("isError"))   # the attempt is over
        r = json.loads(self.call("record_debrief", {"summary": "gave up"})["content"][0]["text"])
        self.assertEqual(r["stored"], ["summary"])
        self.assertEqual(self.call("disconnect")["content"][0]["text"], "disconnected")
        import experience
        store = experience.Experience(path)
        self.assertEqual([(g["result"], g["debrief_state"]) for g in store.games()], [("quit", "done")])
        store.close()
        # A plain disconnect after a pay day of play says the attempt stays open, and how to end it.
        self.game.close()
        self.game = FakeGame()
        self.server = mcp_server.Server()
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": path, "agent": "Claude"})
        self.call("wait_for_decision", {"timeout_seconds": 0.2})
        self.game.turn = 12000                                       # past a pay day (10000 turns in this game)
        self.call("status")
        self.assertIn("The attempt stays open", self.call("disconnect")["content"][0]["text"])

    def test_combat_stats_are_shown_and_kept_on_the_games_record(self):
        path = self._exp_path()
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "experience_path": path})
        self.call("wait_for_decision", {"timeout_seconds": 0.2})
        self.assertGreaterEqual(self.game.combat_calls, 1)               # fetched with the decision
        text = self.call("get_combat_stats")["content"][0]["text"]
        self.assertIn("fights 2; your creatures killed in them 1", text)
        self.assertIn("under 35% -> death: 60 (3.0s); under 20% -> death: -", text)   # -1 from the game = no sample
        self.assertIn("decision due -> your orders: 180 (9.0s)", text)
        self.assertIn("heal_creature 1 (300 gold)", text)
        self.game.victory_state = "won"
        self.game.push_event(["victory"])
        over = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertEqual(over["record"], "won")
        self.assertFalse(self.call("get_combat_stats").get("isError"))   # still answered after the game is over
        self.call("disconnect")
        import combat_report
        rows = combat_report.rows_from_store(path)
        self.assertEqual(len(rows), 1)
        self.assertIn("won", rows[0][0])
        self.assertEqual((rows[0][1]["fights"], rows[0][1]["median_window_low"], rows[0][1]["median_window_critical"]),
                         (2, 60, None))

    def _tool(self, name, args=None):
        r = self.call(name, args)
        self.assertFalse(r.get("isError"), r)
        return json.loads(r["content"][0]["text"])

    def test_combat_rules_named_set_checked_read_back_and_kept_in_memory(self):
        self._connect_and_start()
        rules = [{"name": "save-the-dying", "power": "POWER_HEAL_CREATURE", "level": 2, "when": [["health_pct", "<", 35]]},
                 {"name": "cta-floor", "power_off": "POWER_CALL_TO_ARMS", "when": [["gold", "<", 1500]]}]
        checked = self._tool("check_combat_rules", {"rules": rules, "enabled": True})
        self.assertEqual((checked.get("would_succeed"), checked["rules_changed"]), (True, 2))
        self.assertEqual(self.game.assist_requests, [])                  # a check sends nothing
        done = self._tool("set_combat_rules", {"rules": rules, "enabled": True, "reserve": 1500})
        self.assertEqual(done["rules_changed"], 2)
        self.assertGreater(done["live_in_turns"], done["packets"])
        sent = self.game.assist_requests[-1]
        self.assertEqual((sent["enabled"], sent["reserve"], len(sent["rules"])), (True, 1500, 2))
        held = json.loads(self.game.memory[1])                            # the names live with the game's memory
        self.assertEqual(held["pending_rules"]["names"], ["save-the-dying", "cta-floor"])   # until the rules are live
        back = self._tool("get_combat_rules")
        self.assertEqual(json.loads(self.game.memory[1])["rule_names"], ["save-the-dying", "cta-floor"])
        self.assertEqual([r["name"] for r in back["rules"]], ["save-the-dying", "cta-floor"])
        self.assertEqual(back["rules"][0]["when"], [["health_pct", "<", 35]])
        self.assertIn("save-the-dying: POWER_HEAL_CREATURE L2 on ally_in_fight when health_pct < 35", back["summary"])
        # only the settings: the rules (and their names) stay
        self._tool("set_combat_rules", {"enabled": False})
        self.assertNotIn("rules", self.game.assist_requests[-1])
        self.assertEqual(json.loads(self.game.memory[1])["rule_names"], ["save-the-dying", "cta-floor"])
        self.call("disconnect")

    def test_casting_rules_get_a_spending_cap_unless_one_is_set(self):
        # The live bug: a set with no cap spent 14,300 gold in one pay day.
        self._connect_and_start()
        heal = [{"name": "heal", "power": "POWER_HEAL_CREATURE", "level": 2, "when": [["health_pct", "<", 35]]}]
        done = self._tool("set_combat_rules", {"rules": heal, "enabled": True})
        self.assertEqual((done["max_spend"], self.game.assist_requests[-1]["max_spend"]), (5000, 5000))
        self.assertIn("at most 5000 gold a pay day", done["note"])
        self.assertIn("spent this pay day 0 of its cap 5000", self._tool("get_combat_rules")["summary"])
        # a cap already live is kept: nothing is added
        again = self._tool("set_combat_rules", {"rules": heal})
        self.assertNotIn("max_spend", self.game.assist_requests[-1])
        self.assertNotIn("note", again)
        # the agent's own choice stands, "none" included
        self._tool("set_combat_rules", {"rules": heal, "max_spend": 0})
        self.assertEqual(self.game.assist_requests[-1]["max_spend"], 0)
        self.assertIn("spent this pay day 0 (no cap)", self._tool("get_combat_rules")["summary"])
        # rules that only switch powers off spend nothing, so they get no cap
        self._tool("set_combat_rules", {"rules": [{"name": "cta_floor", "power_off": "POWER_CALL_TO_ARMS", "when": [["gold", "<", 900]]}]})
        self.assertNotIn("max_spend", self.game.assist_requests[-1])
        self.call("disconnect")

    def test_combat_rule_errors_name_the_rule(self):
        self._connect_and_start()
        bad = self._tool("set_combat_rules", {"rules": [{"name": "ok", "power": "POWER_SPEED"}, {"name": "oops", "power": "POWER_NOPE"}]})
        self.assertEqual(bad, {"error": "UNKNOWN_POWER", "rule": 1, "name": "oops"})
        dup = self._tool("set_combat_rules", {"rules": [{"name": "a", "power": "POWER_SPEED"}, {"name": "a", "power": "POWER_SPEED"}]})
        self.assertEqual((dup["error"], dup["rule"], dup["name"]), ("DUPLICATE_NAME", 1, "a"))
        self.assertEqual(self.game.assist_requests, [])
        self.call("disconnect")

    def test_combat_presets_listed_and_applied_with_overrides(self):
        self._connect_and_start()
        listed = self._tool("apply_combat_preset")
        self.assertIn("guardian", listed["presets"])
        self.assertIn("heal: POWER_HEAL_CREATURE L2", listed["presets"]["guardian"]["rules"][0])
        self.assertEqual(self._tool("apply_combat_preset", {"name": "berserker"})["error"], "UNKNOWN_PRESET")
        self._tool("apply_combat_preset", {"name": "guardian", "overrides": {
            "reserve": 3000, "drop": ["cta_floor"], "rules": {"heal": {"level": 4}, "chicken": {"power": "POWER_CHICKEN"}}}})
        sent = self.game.assist_requests[-1]
        self.assertEqual(sent["reserve"], 3000)
        self.assertEqual([r.get("name") for r in sent["rules"]], ["heal", "chicken"])
        self.assertEqual(sent["rules"][0]["level"], 4)
        self.assertNotIn("description", sent)
        self._tool("get_combat_rules")
        self.assertEqual(json.loads(self.game.memory[1])["rule_names"], ["heal", "chicken"])
        self.call("disconnect")

    def test_a_preset_switch_names_the_new_rules_only_once_they_are_live(self):
        # The live bug: striker -> miser renamed striker's casting rules (still live, the change queued) to miser's names.
        self._connect_and_start()
        self._tool("apply_combat_preset", {"name": "guardian"})
        self.assertEqual([r["name"] for r in self._tool("get_combat_rules")["rules"]], ["heal", "cta_floor"])
        self.game.assist_deferred = True          # the next change is queued, not live (a paused game, or a busy seat)
        self._tool("apply_combat_preset", {"name": "miser"})
        back = self._tool("get_combat_rules")
        self.assertEqual([(r["name"], r.get("power") or r.get("power_off")) for r in back["rules"]],
                         [("heal", "POWER_HEAL_CREATURE"), ("cta_floor", "POWER_CALL_TO_ARMS")])   # the live rules keep their own names
        self.assertIn("a change to obey_floor is queued and not live yet", back["pending"])
        self.assertIn("is queued and not live yet", back["summary"])
        self.game.apply_deferred_assist()          # the queued change goes live
        back = self._tool("get_combat_rules")
        self.assertEqual([(r["name"], r.get("power_off")) for r in back["rules"]], [("obey_floor", "POWER_OBEY")])
        self.assertNotIn("pending", back)
        # A change refused when its turn came: its names are dropped, the live rules keep theirs.
        self.game.assist_deferred = True
        self._tool("apply_combat_preset", {"name": "guardian"})
        self.game.results.append({"id": 1, "status": "rejected", "error": "EXPIRED", "turn": self.game.turn})
        back = self._tool("get_combat_rules")
        self.assertEqual([r["name"] for r in back["rules"]], ["obey_floor"])
        self.assertNotIn("pending", back)
        self.assertIn("your last change of standing orders was refused (EXPIRED)", back["summary"])
        # A rule changed some other way (a player's sliders) loses the name it no longer deserves.
        self.game.assist_deferred = False
        self.game.assist["rules"][0]["when"] = [["gold", "<", 5]]
        self.assertEqual([r["name"] for r in self._tool("get_combat_rules")["rules"]], ["rule 0"])
        self.call("disconnect")

    def test_standing_orders_in_the_decision_say_what_each_rule_did(self):
        self._connect_and_start()
        self._tool("set_combat_rules", {"enabled": True, "rules": [
            {"name": "heal", "power": "POWER_HEAL_CREATURE", "when": [["health_pct", "<", 35]]},
            {"name": "floor", "power_off": "POWER_OBEY", "when": [["gold", "<", 800]]}]})
        self.game.push_event(["quarter_2"])
        first = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertIn("STANDING ORDERS (combat assistant ON", first["state"])
        self.assertIn("heal: POWER_HEAL_CREATURE L0 on ally_in_fight when health_pct < 35 | fired 0, spent 0", first["state"])
        # The game heals twice and runs into the reserve before the next decision.
        self.game.assist["rules"][0].update(fired=2, spent=600, state="reserve")
        self.game.assist["casts"] = [{"turn": self.game.turn + 1, "rule": 0, "power": "POWER_HEAL_CREATURE", "level": 0, "target": 4242, "cost": 300},
                                     {"turn": self.game.turn + 2, "rule": 1, "power": "POWER_OBEY", "level": 0, "target": 0, "cost": 0}]
        self.game.push_event(["assist_reserve"])
        nxt = json.loads(self.call("wait_for_decision", {"timeout_seconds": 2})["content"][0]["text"])
        self.assertIn("assist_reserve", nxt["reasons"])
        self.assertIn("fired 2, spent 600 (+2, +600 gold since last decision) | held back by the reserve", nxt["state"])
        self.assertIn("heal POWER_HEAL_CREATURE -> #4242 (gone)", nxt["state"])
        self.assertIn("floor POWER_OBEY (switched off)", nxt["state"])
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

    def test_hand_drop_and_sell_flags_reach_the_game(self):
        self._connect_and_start()
        schema = next(t for t in mcp_server.TOOLS if t["name"] == "submit_orders")["inputSchema"]
        for flag in ("imprison", "torture", "dismiss", "lose_spells"):
            self.assertIn(flag, schema["properties"]["orders"]["items"]["properties"])
        r = self.call("submit_orders", {"orders": [{"verb": "pick_up_and_drop", "thing_ids": [7, 8], "to_room": 3, "dismiss": True},
                                                   {"verb": "pick_up_and_drop", "thing_id": 9, "pos": [10, 10], "imprison": True},
                                                   {"verb": "pick_up_and_drop", "thing_id": 10, "to_room": 4, "torture": True},
                                                   {"verb": "sell", "slab_rect": [1, 1, 2, 2], "lose_spells": True}]})
        self.assertEqual(len(json.loads(r["content"][0]["text"])["sent"]), 4)
        self.assertIs(self.game.submitted[-4]["dismiss"], True)
        self.assertIs(self.game.submitted[-3]["imprison"], True)
        self.assertIs(self.game.submitted[-2]["torture"], True)
        self.assertIs(self.game.submitted[-1]["lose_spells"], True)
        self.call("disconnect")

    def test_a_game_no_human_plays_is_paused_for_the_agent_until_it_resumes(self):
        self.game.agent_only = True
        r = self.call("connect", {"host": "127.0.0.1", "port": self.game.port})
        self.assertIn("PAUSED for you", r["content"][0]["text"])
        self.assertEqual(self.game.pause_requests[-1], dict(self.game.pause_requests[-1], action="set_pause", paused=True))
        first = json.loads(self.call("wait_for_decision", {"timeout_seconds": 5})["content"][0]["text"])
        self.assertEqual(first["reasons"], ["start"])
        self.assertIn("PAUSED by you", first["state"])
        # Held by the agent: nothing will come, so the wait answers at once instead of using up its timeout.
        t0 = time.time()
        held = json.loads(self.call("wait_for_decision", {"timeout_seconds": 10})["content"][0]["text"])
        self.assertLess(time.time() - t0, 3)
        self.assertEqual((held["due"], held["paused"]), (False, True))
        self.assertIn("set_pause(paused=false)", held["note"])
        self.assertIn("running", self.call("set_pause", {"paused": False})["content"][0]["text"])
        self.assertFalse(self.game.paused)
        self.call("set_pause", {"paused": True})
        self.assertTrue(self.game.paused)
        self.assertIn("then paused again", self.call("set_pause", {"paused": False, "turns": 20})["content"][0]["text"])
        self.assertEqual((self.game.pause_requests[-1]["action"], self.game.pause_requests[-1]["turns"]), ("advance_turns", 20))
        self.call("disconnect")

    def test_a_game_a_human_plays_is_not_paused_and_refuses_the_pause(self):
        r = self.call("connect", {"host": "127.0.0.1", "port": self.game.port})
        self.assertNotIn("PAUSED", r["content"][0]["text"])
        self.assertEqual(self.game.pause_requests, [])
        r = self.call("set_pause", {"paused": True})
        self.assertTrue(r.get("isError"))
        self.assertIn("a human plays", r["content"][0]["text"])
        self.assertFalse(self.game.paused)
        self.call("disconnect")

    def test_start_paused_false_leaves_an_agent_only_game_running(self):
        self.game.agent_only = True
        self.call("connect", {"host": "127.0.0.1", "port": self.game.port, "start_paused": False})
        self.assertFalse(self.game.paused)
        self.call("disconnect")

    def test_a_long_wait_is_capped_and_a_decision_it_produced_comes_again_until_answered(self):
        import mcp_session
        old = (mcp_session.MAX_WAIT_SECONDS, mcp_session.RISKY_WAIT_SECONDS)
        mcp_session.MAX_WAIT_SECONDS, mcp_session.RISKY_WAIT_SECONDS = 0.6, 0.3
        try:
            self._connect_and_start()
            self.call("submit_orders", {"orders": []})
            # Asked for longer than a client waits: capped, and told to call again.
            t0 = time.time()
            r = json.loads(self.call("wait_for_decision", {"timeout_seconds": 30})["content"][0]["text"])
            self.assertLess(time.time() - t0, 5)
            self.assertEqual(r["due"], False)
            self.assertIn("call wait_for_decision again", r["note"])
            # A decision at the end of a long wait (its reply may not have reached the client) comes again, at once.
            threading.Timer(0.4, self.game.push_event, args=(["quarter_2"],)).start()
            first = json.loads(self.call("wait_for_decision", {"timeout_seconds": 5})["content"][0]["text"])
            self.assertEqual((first["due"], first["reasons"]), (True, ["quarter_2"]))
            t0 = time.time()
            again = json.loads(self.call("wait_for_decision", {"timeout_seconds": 5})["content"][0]["text"])
            self.assertLess(time.time() - t0, 2)
            self.assertEqual((again["due"], again["reasons"], again.get("repeated")), (True, ["quarter_2"], True))
            self.assertIn("here it is again", again["note"])
            # Once answered, it is not handed out again.
            self.call("submit_orders", {"orders": []})
            r = json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])
            self.assertEqual(r["due"], False)
            # A decision after a short wait went out safely: the next wait waits.
            threading.Timer(0.05, self.game.push_event, args=(["quarter_3"],)).start()
            self.assertEqual(json.loads(self.call("wait_for_decision", {"timeout_seconds": 5})["content"][0]["text"])["reasons"], ["quarter_3"])
            self.assertEqual(json.loads(self.call("wait_for_decision", {"timeout_seconds": 0.2})["content"][0]["text"])["due"], False)
            self.call("disconnect")
        finally:
            mcp_session.MAX_WAIT_SECONDS, mcp_session.RISKY_WAIT_SECONDS = old

    def test_look_shows_the_map_as_it_is_now_not_as_at_the_last_decision(self):
        self._connect_and_start()
        self.assertIn("..A1A1..", self.call("look", {"slab_rect": [0, 0, 3, 3]})["content"][0]["text"])
        # A slab changes after the decision (a bridge built, a slab dug): look must show it without a new decision.
        self.game.map_rows = ["........", "..A1C1..", "..A1C1..", "........"]
        text = self.call("look", {"slab_rect": [0, 0, 3, 3]})["content"][0]["text"]
        self.assertIn("..A1C1..", text.split("\n")[2])
        self.call("disconnect")

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
