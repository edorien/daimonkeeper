#!/usr/bin/env python3
"""Offline tests of the reference bridge (no game, no network beyond a loopback mock): python3 scripts/llm_bridge/test_bridge.py"""
import http.server
import json
import os
import sys
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import prompt  # noqa: E402
from anthropic_policy import AnthropicPolicy  # noqa: E402
from policies import ScriptedPolicy  # noqa: E402
from viewstate import ViewState, apply_diff  # noqa: E402


def make_view(turn=100, gold=500, hp=40):
    rows = ["........", "..A1A1..", "..A1C1..", "........"]
    return {
        "turn": turn, "paused": False, "agent_pause": False, "advancing": False,
        "seat": {"player": 1, "user": 1, "victory_state": "undecided", "queued_steps": 0, "queued_verbs": 0, "results": [],
                 "payday": {"progress": 100, "gap": 10000, "quarter": 0, "turns_to_payday": 9900, "turns_to_next_quarter": 2400}},
        "own": {"gold": gold, "creatures": [{"id": 7, "kind": "ORC", "level": 1, "health": hp, "digger": False, "pos": [4, 4], "state": "IDLE"}],
                "rooms": [{"id": 1, "kind": "DUNGEON_HEART", "slabs": 9, "pos": [4, 4]}], "stock": {"traps": {}, "doors": {}},
                "powers": ["POWER_HEAL_CREATURE"], "power_costs": {"POWER_HEAL_CREATURE": [300, 400, 500, 600, 700, 800, 900, 1000, 1100]},
                "events": [], "dig_marks": [], "dig_marks_limit": 300, "traps": [], "doors": []},
        "map": {"width": 4, "height": 4, "cell": "x", "rows": rows, "legend": [{"c": "A", "kind": "PRETTY_PATH"}, {"c": "C", "kind": "DIRT"}]},
        "visible": {"creatures": [], "rooms": [], "traps": [], "doors": []},
    }


class DiffTests(unittest.TestCase):
    def test_scalar_array_and_map_patches(self):
        v = make_view()
        diff = {"mode": "diff", "base": 1, "view_id": 2, "turn": 150, "own": {
            "gold": 200, "creatures": {"changed": [{"id": 7, "health": 90}], "added": [{"id": 8, "kind": "TROLL", "level": 1, "health": 9, "digger": False, "pos": [1, 1], "state": "X"}]},
            "events": {"added": [{"id": 3, "kind": "new_creature", "pos": [1, 1], "target": 8}]},
            "dig_marks": {"added": [[2, 2]]}},
            "map": {"changes": [{"y": 3, "x": 1, "cells": "B0"}], "revealed": [{"y": 3, "x": 1, "len": 1}]}}
        apply_diff(v, diff)
        self.assertEqual(v["turn"], 150)
        self.assertEqual(v["own"]["gold"], 200)
        self.assertEqual([c["health"] for c in v["own"]["creatures"] if c["id"] == 7], [90])
        self.assertEqual(sorted(c["id"] for c in v["own"]["creatures"]), [7, 8])
        self.assertEqual(v["own"]["dig_marks"], [[2, 2]])
        self.assertEqual(v["map"]["rows"][3], "..B0....")
        self.assertNotIn("view_id", v)

    def test_removal_by_id_and_by_content_and_removed_keys(self):
        v = make_view()
        v["own"]["dig_marks"] = [[1, 1], [2, 2]]
        v["own"]["creatures"].append({"id": 9, "kind": "ORC", "level": 1, "health": 1, "digger": False, "pos": [0, 0], "state": "X"})
        apply_diff(v, {"own": {"creatures": {"removed": [9]}, "dig_marks": {"removed": [[1, 1]]}}, "seat": {"_removed": ["payday"]}})
        self.assertEqual([c["id"] for c in v["own"]["creatures"]], [7])
        self.assertEqual(v["own"]["dig_marks"], [[2, 2]])
        self.assertNotIn("payday", v["seat"])

    def test_coordinate_arrays_replace_whole(self):
        v = make_view()
        apply_diff(v, {"own": {"creatures": {"changed": [{"id": 7, "pos": [5, 6]}]}}})
        self.assertEqual(v["own"]["creatures"][0]["pos"], [5, 6])

    def test_viewstate_keeps_a_full_view_then_patches(self):
        replies = [dict(make_view(), view_id=1, mode="full"), {"mode": "diff", "base": 1, "view_id": 2, "turn": 300}]

        class FakeApi:
            bytes_received = 0
            reqs = []

            def data(self, **req):
                FakeApi.reqs.append(req)
                FakeApi.bytes_received += 10
                return replies[len(FakeApi.reqs) - 1]

        st = ViewState()
        st.update(FakeApi(), 1)
        st.update(FakeApi(), 1)
        self.assertNotIn("since", FakeApi.reqs[0])
        self.assertEqual(FakeApi.reqs[1]["since"], 1)
        self.assertEqual(st.view["turn"], 300)
        self.assertEqual(st.view["view_id"], 2)
        self.assertEqual((st.full_bytes, st.diff_bytes), (10, 10))


class PromptTests(unittest.TestCase):
    def test_render_state_and_window(self):
        st = ViewState()
        st.view = make_view()
        text = prompt.render_state(st, first=True)
        self.assertIn("TURN 100", text)
        self.assertIn("quarter 1 of 4", text)
        self.assertIn("#7 ORC", text)
        self.assertIn("POWER_HEAL_CREATURE (L0 300, L2 500)", text)
        self.assertIn("MAP 4x4", text)
        win = prompt.render_window(st.view, [1, 1, 2, 2])
        self.assertIn("legend: A=PRETTY_PATH", win)

    def test_map_news_from_a_diff(self):
        st = ViewState()
        st.view = make_view()
        st.last_diff = {"map": {"changes": [{"y": 1, "x": 0, "cells": "A1A1"}], "revealed": [{"y": 1, "x": 0, "len": 1}]}, "own": {"events": {"added": [{"kind": "enemy_fight", "pos": [3, 3], "target": 1}]}}}
        text = prompt.render_state(st)
        self.assertIn("1 newly revealed slabs", text)
        self.assertIn("1 slabs changed", text)
        self.assertIn("enemy_fight", text)

    def test_order_to_request(self):
        r = prompt.order_to_request({"verb": "build_room", "kind": "TREASURE", "slab_rect": [1, 2, 3, 4], "junk": 1}, 2, 500, 900)
        self.assertEqual(r, {"action": "submit_action", "player": 2, "queue": True, "view_turn": 500, "max_age_turns": 900,
                             "verb": "build_room", "kind": "TREASURE", "slab_rect": [1, 2, 3, 4]})
        self.assertNotIn("queue", prompt.order_to_request({"verb": "cancel"}, 2, 1, 1))


class ScriptedPolicyTests(unittest.TestCase):
    def test_heals_wounded_builds_a_room_and_digs_once(self):
        v = make_view()
        v["map"] = {"width": 6, "height": 6,
                    "rows": ["A1A1A1CCCCCC", "A1A1A1CCCCCC", "A1A1A1CCCCCC", "............", "............", "............"],
                    "legend": [{"c": "A", "kind": "PRETTY_PATH"}, {"c": "C", "kind": "DIRT"}]}
        st = ViewState()
        st.view = v
        p = ScriptedPolicy()
        d = p.decide(st, {})
        verbs = [o["verb"] for o in d["orders"]]
        self.assertEqual(verbs, ["cast_power", "build_room", "mark_dig"])
        self.assertEqual(d["orders"][1]["slab_rect"], [0, 0, 2, 2])
        self.assertEqual(d["orders"][2]["slab_rect"], [3, 0, 5, 2])
        self.assertEqual(p.decide(st, {})["orders"], [])   # nothing repeats


class MockAnthropic(http.server.BaseHTTPRequestHandler):
    calls = []

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["content-length"])))
        MockAnthropic.calls.append((self.headers.get("x-api-key"), body))
        n = len(MockAnthropic.calls)
        if n == 1:
            content = [{"type": "tool_use", "id": "t1", "name": "look", "input": {"slab_rect": [0, 0, 3, 3]}}]
        else:
            content = [{"type": "tool_use", "id": "t2", "name": "submit_orders",
                        "input": {"reasoning": "heal", "orders": [{"verb": "slap", "thing_id": 7}]}}]
        out = json.dumps({"content": content, "usage": {"input_tokens": 1000 * n, "output_tokens": 50}}).encode()
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)

    def log_message(self, *a):
        pass


class AnthropicPolicyTests(unittest.TestCase):
    def test_tool_loop_against_a_local_mock(self):
        srv = http.server.HTTPServer(("127.0.0.1", 0), MockAnthropic)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        MockAnthropic.calls.clear()
        st = ViewState()
        st.view = make_view()
        pol = AnthropicPolicy(model="test-model", base_url="http://127.0.0.1:%d" % srv.server_address[1], api_key="k")
        ctx = {}
        d = pol.decide(st, ctx)
        srv.shutdown()
        srv.server_close()
        self.assertEqual(d["orders"], [{"verb": "slap", "thing_id": 7}])
        self.assertEqual(len(MockAnthropic.calls), 2)
        key, first = MockAnthropic.calls[0]
        self.assertEqual(key, "k")
        self.assertEqual(first["model"], "test-model")
        self.assertEqual({t["name"] for t in first["tools"]}, {"submit_orders", "look"})
        self.assertIn("TURN 100", first["messages"][0]["content"])
        second = MockAnthropic.calls[1][1]
        self.assertEqual(second["messages"][-1]["content"][0]["type"], "tool_result")
        self.assertIn("legend: A=PRETTY_PATH", second["messages"][-1]["content"][0]["content"])
        self.assertEqual((ctx["metrics"]["calls"], ctx["metrics"]["input_tokens"], ctx["metrics"]["output_tokens"]), (2, 3000, 100))


if __name__ == "__main__":
    unittest.main()
