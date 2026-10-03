#!/usr/bin/env python3
"""Offline tests of the reference bridge (no game, no network beyond a loopback mock): python3 scripts/llm_bridge/test_bridge.py"""
import http.server
import json
import os
import shutil
import socket
import sys
import tempfile
import threading
import time
import unittest

import tempfile as _tempfile

# Never let a test touch the real experience store (experience.default_path(), next to the bridge's own files).
_TEST_DATA_HOME = _tempfile.mkdtemp(prefix="kfx-bridge-test-data-")
os.environ["DAIMONKEEPER_EXPERIENCE"] = os.path.join(_TEST_DATA_HOME, "experience.sqlite")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import prompt  # noqa: E402
from api import Api  # noqa: E402
import experience  # noqa: E402
import memory as memory_mod  # noqa: E402
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


class SubmitBatchTests(unittest.TestCase):
    class FakeApi:
        def __init__(self, unreachable_for=None):
            self.reqs, self.unreachable_for = [], unreachable_for

        def call(self, **req):
            self.reqs.append(req)
            data = {"steps": 3, "id": len(self.reqs), "queued_behind": 0}
            if req.get("slab_rect") == self.unreachable_for:
                data.update(warning="UNREACHABLE", unreachable_count=2, unreachable_slabs=[[9, 9], [9, 10]])
            return {"success": True, "data": data}

    def test_dry_run_passes_the_batchs_earlier_digs_on(self):
        from orders import submit_batch
        api = self.FakeApi()
        orders = [{"verb": "mark_dig", "slab_rect": [1, 1, 1, 5]}, {"verb": "build_room", "kind": "LAIR", "slab_rect": [3, 3, 4, 4]},
                  {"verb": "mark_dig", "slab_rect": [1, 6, 1, 9]}]
        submit_batch(api, 1, orders, 100, 400, dry_run=True)
        self.assertNotIn("assume_dig_rects", api.reqs[0])
        self.assertEqual(api.reqs[1]["assume_dig_rects"], [[1, 1, 1, 5]])
        self.assertEqual(api.reqs[2]["assume_dig_rects"], [[1, 1, 1, 5]])
        real = self.FakeApi()
        submit_batch(real, 1, orders, 100, 400)
        self.assertTrue(all("assume_dig_rects" not in r for r in real.reqs), "a real submit queues them: the engine knows")

    def test_a_partly_unbuildable_room_comes_back_with_a_warning(self):
        from orders import submit_batch

        class Api:
            def call(self, **req):
                return {"success": True, "data": {"steps": 7, "id": 1, "queued_behind": 0, "warning": "PARTLY_UNBUILDABLE",
                                                  "unbuildable_count": 3, "unbuildable_slabs": [[4, 5], [5, 5], [6, 5]]}}
        sent, refused = submit_batch(Api(), 1, [{"verb": "build_room", "kind": "TREASURE", "slab_rect": [4, 4, 6, 5]}], 100, 400)
        self.assertIn("PARTLY_UNBUILDABLE: 3 slab(s) will not take the room, e.g. (4,5) (5,5) (6,5)", sent[0]["warning"])

    def test_an_unreachable_dig_comes_back_with_a_warning(self):
        from orders import submit_batch
        sent, refused = submit_batch(self.FakeApi(unreachable_for=[9, 9, 9, 10]), 1,
                                     [{"verb": "mark_dig", "slab_rect": [9, 9, 9, 10]}, {"verb": "mark_dig", "slab_rect": [1, 1, 1, 2]}], 100, 400)
        self.assertEqual(refused, [])
        self.assertIn("UNREACHABLE: 2 slab(s) no imp can reach, e.g. (9,9) (9,10)", sent[0]["warning"])
        self.assertNotIn("warning", sent[1])


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

    def test_placed_things_powers_pickups_and_chat(self):
        view = make_view()
        view["own"].update(
            traps=[{"id": 40, "kind": "BOULDER", "pos": [106, 232], "slappable": True, "aimed": True},
                   {"id": 41, "kind": "TNT", "pos": [120, 229], "slappable": False}],
            doors=[{"id": 50, "kind": "WOOD", "pos": [130, 250], "locked": True}],
            active_powers={"POWER_CALL_TO_ARMS": {"pos": [100, 100]}, "POWER_OBEY": {}},
            specials=[{"id": 60, "kind": "RESURRECT_CREATURE", "pos": [10, 10], "reachable": True},
                      {"id": 61, "kind": "REVEAL_MAP", "pos": [20, 20], "reachable": False}],
            dead_creatures=[{"kind": "ORC", "level": 3}],
            loose_gold={"total": 900, "count": 2, "piles": [{"id": 70, "gold": 400, "pos": [5, 5], "reachable": True}]})
        view["chat"] = [{"player": 1, "text": "Hold the east corridor", "turn": 10}, {"player": 0, "text": "On my way", "turn": 20}]
        lines = "\n".join(prompt.render_placed_and_pickups(view))
        self.assertIn("#40 BOULDER at (106,232) [slappable, give a direction]", lines)
        self.assertIn("#41 TNT at (120,229) [not armed / spent]", lines)
        self.assertIn("DOORS: #50 WOOD at (130,250) LOCKED", lines)
        self.assertIn("ACTIVE POWERS (power_off ends one): POWER_CALL_TO_ARMS at (100,100), POWER_OBEY", lines)
        self.assertIn("SPECIAL BOX #61 REVEAL_MAP at (20,20) (not on your land", lines)
        self.assertIn("YOUR DEAD (for a resurrect box): ORC L3", lines)
        self.assertIn("LOOSE GOLD: 900 in 2 pile(s)", lines)
        self.assertIn("CHAT (latest last): you: Hold the east corridor | player 0: On my way", lines)

    def test_new_order_fields_reach_the_request(self):
        req = prompt.order_to_request({"verb": "slap", "thing_id": 40, "direction": "E"}, 1, 100, 400)
        self.assertEqual(req["direction"], "E")
        req = prompt.order_to_request({"verb": "use_special", "thing_id": 60, "kind": "ORC", "level": 3}, 1, 100, 400)
        self.assertEqual((req["kind"], req["level"]), ("ORC", 3))
        req = prompt.order_to_request({"verb": "send_message", "message": "On my way"}, 1, 100, 400)
        self.assertEqual(req["message"], "On my way")

    def test_temple_and_graveyard_lines(self):
        st = ViewState()
        st.view = make_view()
        st.view["own"].update(graveyard={"rooms": [{"id": 8, "bodies": 2, "capacity": 9}], "toward_vampire": 7, "per_vampire": 10, "bodies_lying": 3},
                              temples=[{"id": 12, "efficiency": 88, "pool": [130, 160], "praying": 2},
                                       {"id": 13, "efficiency": 50, "praying": 0}],
                              sacrifices={"offered": {"FLY": 1}, "outcomes": [{"recipe": "POSUNIQFUNC COMPLETE_RESEARCH <- FLY FLY", "turn": 30211}]})
        st.view["rules"] = {"sacrifices": ["POSUNIQFUNC COMPLETE_RESEARCH <- FLY FLY", "MKCREATURE HORNY <- TROLL BILE_DEMON DARK_MISTRESS"]}
        first = prompt.render_state(st, first=True)
        self.assertIn("GRAVEYARD: 7/10 bodies toward the next vampire; 3 body(ies) lying", first)
        self.assertIn("TEMPLE room 12: efficiency 88%, pool at (130,160), 2 praying", first)
        self.assertIn("TEMPLE room 13: efficiency 50%, no pool", first)
        self.assertIn("OFFERED SO FAR: FLY x1", first)
        self.assertIn("SACRIFICE FIRED turn 30211: POSUNIQFUNC COMPLETE_RESEARCH <- FLY FLY", first)
        self.assertIn("SACRIFICE RECIPES (exact;", first)
        self.assertIn("MKCREATURE HORNY <- TROLL BILE_DEMON DARK_MISTRESS", first)
        st.last_diff = {"turn": 2}
        later = prompt.render_state(st)
        self.assertIn("RECIPES: 2 active", later)
        self.assertNotIn("SACRIFICE RECIPES", later)
        st.last_diff = {"rules": {"sacrifices": st.view["rules"]["sacrifices"]}}   # the temple was just built
        self.assertIn("SACRIFICE RECIPES", prompt.render_state(st))

    def test_custody_lines(self):
        own = {"tendencies": {"imprison": False, "flee": True}, "custody": {
            "prisons": [{"room": 7, "held": 2, "capacity": 6, "pos": [10, 10]}],
            "torture": [{"room": 9, "held": 1, "capacity": 8, "devices_free": 7, "pos": [20, 10]}],
            "prisoners": [
                {"id": 57, "kind": "KNIGHT", "level": 4, "owner": 4, "health": 40, "max_health": 450, "where": "prison", "room": 7,
                 "hungry": True, "on_death": "SKELETON", "pos": [10, 10]},
                {"id": 71, "kind": "WIZARD", "level": 5, "owner": 4, "health": 120, "max_health": 300, "where": "torture", "room": 9,
                 "hungry": False, "on_death": "GHOST", "torture": {"turns_in": 1200, "break_time": 1000}, "pos": [20, 10]}],
            "food": {"5": {"chickens": 3, "ids": [301, 302, 303]}}}}
        lines = prompt.render_custody(own)
        self.assertEqual(lines[0], "PRISON room 7: 2/6 held (imprison is OFF: no new prisoners will arrive)")
        self.assertEqual(lines[1], "TORTURE room 9: 1/8, 7 device(s) free")
        self.assertIn("PRISONER #57 KNIGHT L4 owner 4 hp 40/450 in prison 7, hungry, -> SKELETON for you if it dies there", lines[2])
        self.assertIn("1200 turns on the rack (breaks from ~1000)", lines[3])
        self.assertEqual(lines[4], "FOOD: hatchery 5 has 3 chicken(s) (ids 301 302 303)")
        own["tendencies"]["imprison"] = True
        self.assertEqual(prompt.render_custody(own)[0], "PRISON room 7: 2/6 held")
        self.assertEqual(prompt.render_custody({}), [])

    def test_hand_orders_carry_their_new_fields(self):
        req = prompt.order_to_request({"verb": "pick_up_and_drop", "thing_ids": [301, 302], "to_room": 7, "release": False}, 1, 100, 400)
        self.assertEqual((req["thing_ids"], req["to_room"], req["release"]), ([301, 302], 7, False))
        from orders import summarize
        self.assertEqual(summarize({"verb": "pick_up_and_drop", "thing_ids": [57], "to_room": 9, "release": True}),
                         "pick_up_and_drop to room 9 things [57] release")

    def test_room_quality_lines(self):
        rooms = [{"id": 3, "kind": "TREASURE", "slabs": 9, "pos": [112, 130], "efficiency": 64, "health": 18, "max_health": 18,
                  "capacity": {"used": 4, "total": 27}, "gold": 2400, "occupants": {},
                  "open_sides": {"count": 3, "slabs": [[35, 42, "N"], [36, 42, "N"], [37, 42, "N"]]}},
                 {"id": 4, "kind": "LAIR", "slabs": 9, "pos": [124, 154], "efficiency": 100, "health": 10, "max_health": 20,
                  "occupants": {"BUG": 3, "FLY": 2}, "open_sides": {"count": 0, "slabs": []}}]
        lines = prompt.render_rooms(rooms)
        self.assertEqual(lines[0], "ROOMS:")
        self.assertEqual(lines[1], "  #3 TREASURE 9 slabs at (112,130): efficiency 64%, gold 2400, used 4/27; 3 open sides e.g. (35,42)N (36,42)N (37,42)N")
        self.assertEqual(lines[2], "  #4 LAIR 9 slabs at (124,154): efficiency 100%, hp 10/20, in it: BUG 3, FLY 2")
        self.assertEqual(prompt.render_rooms([]), ["ROOMS: none"])

    def test_workers_are_not_the_army(self):
        own = {"creature_summary": {
                   "IMP": {"count": 6, "max_level": 1, "avg_level_x10": 10, "avg_health": 75, "digger": True},
                   "BUG": {"count": 3, "max_level": 2, "avg_level_x10": 15, "avg_health": 200, "digger": False}},
               "force": {"fighters": 3, "workers": 6, "fighter_score": 42}}
        text = prompt.render_army(own)
        self.assertIn("ARMY (fighters 3, fighter score 42): BUG x3", text)
        self.assertNotIn("IMP", text.split("\n")[0])
        self.assertIn("WORKERS: IMP x6 (max level 1, avg 1.0, avg hp 75) -- dig, claim, mine and carry; not fighters", text)

    def test_visible_creatures_show_level_digger_and_force(self):
        st = ViewState()
        st.view = make_view()
        st.view["visible"]["creatures"] = [
            {"id": 62, "kind": "TUNNELLER", "owner": 4, "level": 3, "health": 251, "digger": True, "pos": [155, 120]},
            {"id": 63, "kind": "THIEF", "owner": 4, "level": 2, "health": 135, "digger": False, "pos": [156, 117]}]
        st.view["visible"]["force_by_owner"] = {"4": {"fighters": 1, "workers": 1, "fighter_score": 19}}
        text = prompt.render_state(st)
        self.assertIn("VISIBLE OTHER CREATURES (owner 4: fighters 1, fighter score 19, workers 1):", text)
        self.assertIn("#62 TUNNELLER L3 owner 4 hp251 [digger] at (155,120)", text)
        self.assertIn("#63 THIEF L2 owner 4 hp135 at (156,117)", text)

    def test_level_objective_and_event_text(self):
        st = ViewState()
        st.view = make_view()
        st.view["level"] = {"number": 1, "name": "Eversmile", "description": "A cheerful land."}
        st.view["own"]["objective"] = "Build a Treasure Room.\nThen find the portal."
        st.view["own"]["objective_history"] = [{"text": "Dig out gold.", "turn": 5}, {"text": "Build a Treasure Room.\nThen find the portal.", "turn": 90}]
        st.view["own"]["events"] = [{"id": 3, "kind": "information", "pos": [0, 0], "target": -6, "text": "Imps dig.\nClick to learn more."}]
        first = prompt.render_state(st, first=True)
        self.assertIn("LEVEL 1: Eversmile -- A cheerful land.", first)
        self.assertIn("OBJECTIVE: Build a Treasure Room. Then find the portal.", first)
        self.assertIn("EARLIER OBJECTIVES: turn 5: Dig out gold.", first)
        self.assertIn('information at (0,0): "Imps dig. Click to learn more."', first)
        later = prompt.render_state(st)
        self.assertNotIn("LEVEL 1", later)
        self.assertNotIn("EARLIER OBJECTIVES", later)
        self.assertIn("OBJECTIVE: Build a Treasure Room.", later)

    def test_unreachable_dig_marks_are_called_out(self):
        st = ViewState()
        st.view = make_view()
        self.assertNotIn("UNREACHABLE", prompt.render_state(st, first=True))
        st.view["own"].update(dig_marks=[[1, 1], [2, 1]], unreachable_dig_marks=[[1, 1], [2, 1]], unreachable_dig_marks_count=2)
        self.assertIn("DIG MARKS: 2 of 300 | UNREACHABLE 2 (no imp can get to them; connect them to walkable floor): (1,1) (2,1)",
                      prompt.render_state(st, first=True))

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
        if body.get("tool_choice", {}).get("name") == "record_debrief":
            content = [{"type": "tool_use", "id": "d1", "name": "record_debrief",
                        "input": {"summary": "lost to a rush", "playbook": "wall in early"}}]
        elif n == 1:
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


    def test_debrief_is_one_forced_record_debrief_call_and_is_stored(self):
        srv = http.server.HTTPServer(("127.0.0.1", 0), MockAnthropic)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        MockAnthropic.calls.clear()
        pol = AnthropicPolicy(model="test-model", base_url="http://127.0.0.1:%d" % srv.server_address[1], api_key="k")
        d = tempfile.mkdtemp(prefix="kfx-debrief-")
        store = experience.Experience(os.path.join(d, "e.sqlite"))
        mem = Memory.new_game({"turn": 0, "level": {"number": 2, "campaign": "keeporig", "name": "Eversmile"}})
        rec = experience.Recorder(store, 1, "test-model")
        rec.start(mem, {"turn": 0}, resumed=False)
        v = make_view()
        rec.ended(mem, v, "lost")
        ctx = {}
        out = experience.run_debrief(store, mem, rec, "lost", v, pol, ctx)
        srv.shutdown()
        srv.server_close()
        self.assertEqual(out["stored"], ["summary", "general"])
        self.assertEqual(store.lessons("general"), "wall in early")
        self.assertEqual(store.branch(mem.game_id, mem.branch)["summary"], "lost to a rush")
        body = MockAnthropic.calls[0][1]
        self.assertEqual(body["tool_choice"], {"type": "tool", "name": "record_debrief"})
        self.assertIn("DEBRIEF: the game is over (lost", body["messages"][0]["content"])
        self.assertEqual(ctx["metrics"]["calls"], 1)
        # The scripted policy only summarises: no lessons are touched.
        from policies import ScriptedPolicy
        mem2 = Memory.new_game({"turn": 0})
        rec.start(mem2, {"turn": 0}, resumed=False)
        rec.ended(mem2, v, "won")
        out = experience.run_debrief(store, mem2, rec, "won", v, ScriptedPolicy(), {})
        self.assertEqual(out["stored"], ["summary"])
        self.assertEqual(store.lessons("general"), "wall in early")
        store.close()
        shutil.rmtree(d, ignore_errors=True)


class MemoryTests(unittest.TestCase):
    def test_plan_notes_caps_decisions_outcomes_and_the_blob_round_trip(self):
        m = Memory.new_game({"turn": 7, "level": {"number": 3, "campaign": "keeporig.cfg", "name": "Eversmile"}})
        self.assertEqual(m.level, {"key": "keeporig:3", "campaign": "keeporig", "number": 3, "name": "Eversmile"})
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
        again = Memory.from_blob(m.to_blob())                  # what the game keeps and gives back
        self.assertEqual((len(again.plan), again.notes, again.count, again.seen_kinds, again.game_id, again.start_turn),
                         (1800, "n2", 15, ["ORC", "TROLL", "DRAGON"], m.game_id, 7))
        self.assertIsNone(Memory.from_blob(None))
        self.assertIsNone(Memory.from_blob("not json"))
        self.assertIsNone(Memory.from_blob('{"format": 99, "game_id": "x"}'))

    def test_flush_pushes_only_changes_and_the_blob_stays_under_the_cap(self):
        pushed = []
        m = Memory.new_game()
        m._sink = pushed.append
        m.flush()
        m.flush()
        self.assertEqual(len(pushed), 1)
        m.update(plan="x")
        m.flush()
        self.assertEqual(len(pushed), 2)
        for i in range(12):
            m.record_decision(i, ["timer"], "r" * 300, [{"id": i, "verb": "v", "summary": "s" * 2000}] * 3, [])
        for t in range(200):
            m.add_checkpoint({"turn": t, "gold": 1000 + t, "pad": "p" * 200})
        blob = m.to_blob()
        self.assertLessEqual(len(blob.encode()), memory_mod.BLOB_CAP)
        back = Memory.from_blob(blob)
        self.assertEqual(back.plan, "x")
        self.assertEqual(back.checkpoints[-1]["turn"], 199)    # history is lost from the old end, never the latest
        self.assertLessEqual(len(m.checkpoints), memory_mod.CHECKPOINTS_CAP)

    def test_a_branch_after_a_load(self):
        m = Memory.new_game()
        first = m.branch
        m.start_branch()
        self.assertEqual(m.parent_branch, first)
        self.assertNotEqual(m.branch, first)

    def test_creature_names_are_stable_per_kind_and_never_pass_to_a_new_creature(self):
        m = Memory.new_game()
        self.assertEqual(m.name_for(7, "ORC", 100), "Orc #1")
        self.assertEqual(m.name_for(8, "ORC", 120), "Orc #2")           # a second orc gets the next number
        self.assertEqual(m.name_for(7, "ORC", 100), "Orc #1")           # the first is unchanged on a second call
        self.assertEqual(m.name_for(9, "GIANT_SPIDER", 5), "Giant Spider #1")  # per-kind counters; underscores read as spaces
        m.note_live([{"id": 7, "born": 100}, {"id": 8, "born": 120}, {"id": 9, "born": 5}])
        self.assertEqual(m.id_for("Orc #2"), 8)
        self.assertIsNone(m.id_for("Nobody #1"))
        # Orc #1 dies and a fly is born into its thing slot: a new creature, a new name, and the old name no longer
        # resolves (it would order the fly).
        self.assertEqual(m.name_for(7, "FLY", 900), "Fly #1")
        m.note_live([{"id": 7, "born": 900}, {"id": 8, "born": 120}])
        self.assertIsNone(m.id_for("Orc #1"))
        self.assertEqual(m.id_for("Fly #1"), 7)
        again = Memory.from_blob(m.to_blob())
        self.assertEqual(again.name_for(8, "ORC", 120), "Orc #2")       # the counters survive too: the next orc is #3
        self.assertEqual(again.name_for(30, "ORC", 950), "Orc #3")

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
