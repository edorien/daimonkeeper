#!/usr/bin/env python3
"""Offline tests of the reference bridge (no game, no network beyond a loopback mock): python3 scripts/llm_bridge/test_bridge.py"""
import http.server
import json
import os
import socket
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import prompt  # noqa: E402
from api import Api  # noqa: E402
from memory import Memory  # noqa: E402
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
        self.assertIn("(id 7, ORC L1, hp40)", text)
        self.assertIn("POWER_HEAL_CREATURE (L0 300, L2 500)", text)
        self.assertIn("MAP 4x4", text)
        win = prompt.render_window(st.view, [1, 1, 2, 2])
        self.assertIn("legend: A=PRETTY_PATH", win)

    def test_prettify_state(self):
        self.assertEqual(prompt._prettify_state("IDLE"), "IDLE")
        self.assertEqual(prompt._prettify_state("CreatureCombatFlee"), "Combat Flee")
        self.assertEqual(prompt._prettify_state("CreatureCannotFindWork"), "Cannot Find Work")
        self.assertEqual(prompt._prettify_state("ManualControl"), "Manual Control")
        self.assertEqual(prompt._prettify_state(""), "")

    def test_creatures_get_a_persistent_name_and_show_angry_fleeing_tags(self):
        st = ViewState()
        st.view = make_view()
        st.view["own"]["creatures"][0].update(angry=True, angry_reason="hungry", fleeing=False, digger=True)
        mem = Memory()
        text = prompt.render_state(st, memory=mem)
        self.assertIn("Orc #1 (id 7, ORC L1, hp40): IDLE [imp, angry: hungry] at (4,4)", text)
        self.assertEqual(mem.name_for(7, "ORC"), "Orc #1")  # the same call render_state made: same name back
        st.view["own"]["creatures"].append({"id": 8, "kind": "TROLL", "level": 2, "health": 90, "digger": False,
                                             "pos": [5, 5], "state": "CreatureCombatFlee", "fleeing": True})
        text2 = prompt.render_state(st, memory=mem)
        self.assertIn("Orc #1", text2)          # the first creature's name did not change
        self.assertIn("Troll #1 (id 8, TROLL L2, hp90): Combat Flee [fleeing] at (5,5)", text2)

    def test_research_line(self):
        st = ViewState()
        st.view = make_view()
        st.view["own"]["research"] = {"current": {"category": "room", "name": "RESEARCH", "progress_pct": 40}, "queue": ["RESEARCH", "POWER_SPEED"]}
        self.assertIn("RESEARCH: working on room RESEARCH (40%) | queue: RESEARCH, POWER_SPEED", prompt.render_state(st))
        st.view["own"]["research"] = {"queue": []}
        self.assertIn("working on nothing", prompt.render_state(st))

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

    def test_order_to_request_resolves_a_name_via_memory(self):
        mem = Memory()
        self.assertEqual(mem.name_for(7, "ORC"), "Orc #1")
        r = prompt.order_to_request({"verb": "slap", "name": "Orc #1"}, 2, 500, 900, memory=mem)
        self.assertEqual(r["thing_id"], 7)
        # an explicit thing_id is never overridden by a name
        r2 = prompt.order_to_request({"verb": "slap", "name": "Orc #1", "thing_id": 99}, 2, 500, 900, memory=mem)
        self.assertEqual(r2["thing_id"], 99)
        # an unknown name is left unresolved -- the real submit reports MISSING_THING rather than this guessing
        r3 = prompt.order_to_request({"verb": "slap", "name": "Nobody #9"}, 2, 500, 900, memory=mem)
        self.assertNotIn("thing_id", r3)
        # no memory at all: also left unresolved, not an error
        r4 = prompt.order_to_request({"verb": "slap", "name": "Orc #1"}, 2, 500, 900)
        self.assertNotIn("thing_id", r4)


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
                        "input": {"reasoning": "heal", "plan": "grow the army", "notes": "orcs train", "orders": [{"verb": "slap", "thing_id": 7}]}}]
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
        mem = Memory()
        mem.update(plan="old plan", notes="old notes")
        ctx = {"memory": mem, "reasons": ["enemy_fight"]}
        d = pol.decide(st, ctx)
        srv.shutdown()
        srv.server_close()
        self.assertEqual(d["orders"], [{"verb": "slap", "thing_id": 7}])
        self.assertEqual((d["plan"], d["notes"]), ("grow the army", "orcs train"))
        self.assertEqual(len(MockAnthropic.calls), 2)
        key, first = MockAnthropic.calls[0]
        self.assertEqual(key, "k")
        self.assertEqual(first["model"], "test-model")
        self.assertEqual({t["name"] for t in first["tools"]}, {"submit_orders", "look"})
        self.assertIn("TURN 100", first["messages"][0]["content"])
        self.assertIn("YOUR PLAN", first["messages"][0]["content"])
        self.assertIn("old plan", first["messages"][0]["content"])
        self.assertIn("DECISION DUE because: enemy_fight", first["messages"][0]["content"])
        self.assertIn("plan", first["tools"][0]["input_schema"]["properties"])
        second = MockAnthropic.calls[1][1]
        self.assertEqual(second["messages"][-1]["content"][0]["type"], "tool_result")
        self.assertIn("legend: A=PRETTY_PATH", second["messages"][-1]["content"][0]["content"])
        self.assertEqual((ctx["metrics"]["calls"], ctx["metrics"]["input_tokens"], ctx["metrics"]["output_tokens"]), (2, 3000, 100))


class MemoryTests(unittest.TestCase):
    def test_plan_notes_caps_decisions_outcomes_and_persistence(self):
        path = tempfile.mktemp(suffix=".json")
        m = Memory(path)
        self.assertEqual(m.update(plan="p" * 5000, notes="short"), "plan cut to 1800 characters")
        self.assertEqual((len(m.plan), m.notes), (1800, "short"))
        m.update(plan=None, notes="n2")                       # omitted plan is kept
        self.assertEqual((len(m.plan), m.notes), (1800, "n2"))
        for i in range(15):
            m.record_decision(100 * i, ["quarter_1"], "reason %d" % i, [{"id": i + 1, "verb": "mark_dig", "summary": "mark_dig slabs[0]"}], [])
        self.assertEqual(len(m.decisions), 12)                # bounded log
        m.note_results([{"id": 15, "status": "done"}, {"id": 14, "status": "rejected", "error": "EXPIRED"}])
        self.assertEqual(m.decisions[-1]["orders"][0]["status"], "done")
        self.assertEqual(m.decisions[-2]["orders"][0]["status"], "rejected EXPIRED")
        self.assertEqual(m.new_kinds(["ORC", "TROLL"]), ["ORC", "TROLL"])
        self.assertEqual(m.new_kinds(["ORC", "DRAGON"]), ["DRAGON"])   # each kind is shown once
        text = m.render()
        self.assertIn("YOUR PLAN", text)
        self.assertIn("#15", text)
        self.assertIn("rejected EXPIRED", text)
        again = Memory(path)                                  # a restart keeps everything
        self.assertEqual((len(again.plan), again.notes, again.count, again.seen_kinds), (1800, "n2", 15, ["ORC", "TROLL", "DRAGON"]))
        os.unlink(path)

    def test_creature_names_are_stable_per_kind_and_survive_a_restart(self):
        path = tempfile.mktemp(suffix=".json")
        m = Memory(path)
        self.assertEqual(m.name_for(7, "ORC"), "Orc #1")
        self.assertEqual(m.name_for(8, "ORC"), "Orc #2")           # a second orc gets the next number
        self.assertEqual(m.name_for(7, "ORC"), "Orc #1")           # the first is unchanged on a second call
        self.assertEqual(m.name_for(9, "GIANT_SPIDER"), "Giant Spider #1")  # per-kind counters, and underscores read as spaces
        self.assertEqual(m.id_for("Orc #2"), 8)
        self.assertIsNone(m.id_for("Nobody #1"))
        again = Memory(path)
        self.assertEqual(again.name_for(8, "ORC"), "Orc #2")       # not "Orc #3": the restart kept the counter too
        self.assertEqual(again.id_for("Giant Spider #1"), 9)
        os.unlink(path)


class EventTests(unittest.TestCase):
    def test_pushed_events_do_not_confuse_replies_and_can_be_waited_for(self):
        a, b = socket.socketpair()
        api = Api()
        api.sock = a

        def server():
            req = json.loads(b.makefile().readline())
            b.sendall((json.dumps({"event": "DECISION_DUE", "data": {"reasons": "quarter_1", "seq": 1}}) + "\n").encode())
            b.sendall((json.dumps({"ack": req["ack"], "success": True, "data": {"x": 1}}) + "\n").encode())
            time.sleep(0.2)
            b.sendall((json.dumps({"event": "DECISION_DUE", "data": {"reasons": "enemy_fight", "seq": 2}}) + "\n").encode())

        threading.Thread(target=server, daemon=True).start()
        self.assertEqual(api.data(action="anything"), {"x": 1})           # the event that came first was set aside
        self.assertEqual(len(api.events), 1)
        first = api.wait_event("DECISION_DUE", 1.0)
        self.assertEqual(first["data"]["reasons"], "quarter_1")
        second = api.wait_event("DECISION_DUE", 2.0)                       # arrives later
        self.assertEqual(second["data"]["seq"], 2)
        self.assertIsNone(api.wait_event("DECISION_DUE", 0.1))             # nothing more: times out
        a.close(); b.close()

    def test_drain_returns_only_what_is_already_there(self):
        a, b = socket.socketpair()
        api = Api()
        api.sock = a
        b.sendall((json.dumps({"event": "DECISION_DUE", "data": {"seq": 7}}) + "\n").encode())
        b.sendall((json.dumps({"event": "OTHER", "data": {}}) + "\n").encode())
        time.sleep(0.05)
        got = api.drain_events("DECISION_DUE")
        self.assertEqual([e["data"]["seq"] for e in got], [7])
        self.assertEqual(api.drain_events("DECISION_DUE"), [])
        a.close(); b.close()


class NewSectionsTests(unittest.TestCase):
    def test_render_shows_reason_unlocked_army_tendencies_and_fresh_profiles_only(self):
        v = make_view()
        v["own"]["unlocked"] = {"rooms": ["TREASURE", "LAIR"], "traps": ["BOULDER"], "doors": []}
        v["own"]["tendencies"] = {"imprison": True, "flee": False}
        v["own"]["creature_summary"] = {"ORC": {"count": 2, "max_level": 3, "avg_level_x10": 20, "avg_health": 350, "by_level": {"1": 1, "3": 1}}}
        v["own"]["creature_info"] = {"ORC": {"health": 300, "strength": 20, "armour": 10, "defence": 8, "speed": 50, "flying": False, "pay": 100,
                                             "hunger_rate": 5, "lair_size": 1, "abilities": [{"name": "SWING_WEAPON", "from_level": 1}],
                                             "primary_jobs": ["TRAIN"], "secondary_jobs": ["GUARD"], "combat": "average fighter"}}
        st = ViewState()
        st.view = v
        text = prompt.render_state(st, reasons=["enemy_fight", "quarter_2"], fresh_kinds=["ORC"])
        self.assertTrue(text.startswith("DECISION DUE because: enemy_fight, quarter_2"))
        self.assertIn("UNLOCKED: rooms TREASURE, LAIR | traps BOULDER | doors none", text)
        self.assertIn("imprison captured enemies on, hurt creatures flee off", text)
        self.assertIn("ORC x2 (max level 3, avg 2.0, avg hp 350)", text)
        self.assertIn("NEW CREATURE KIND(S)", text)
        self.assertIn("works as TRAIN (secondary GUARD)", text)
        self.assertNotIn("NEW CREATURE KIND", prompt.render_state(st, reasons=[], fresh_kinds=[]))

    def test_set_tendency_order_and_summary(self):
        r = prompt.order_to_request({"verb": "set_tendency", "kind": "imprison", "enabled": True}, 2, 5, 9)
        self.assertEqual((r["verb"], r["kind"], r["enabled"]), ("set_tendency", "imprison", True))
        from orders import summarize
        self.assertEqual(summarize({"verb": "set_tendency", "kind": "flee", "enabled": False}), "set_tendency flee off")


if __name__ == "__main__":
    unittest.main()
