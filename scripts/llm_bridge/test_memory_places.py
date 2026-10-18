"""Tests for the agent memory's format 2 (memory.py: facts, chapters, the upgrade from format 1, a newer format left
alone) and the named places (places.py) -- docs/plans/llm-bridge-memory.md, Phases E and F."""
import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import experience  # noqa: E402
import memory as memory_mod  # noqa: E402
import places  # noqa: E402
from experience import Experience, Recorder  # noqa: E402
from memory import Memory  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def view(turn=0, rooms=None, enemy_rooms=None, gates=None, payday_gap=1200):
    return {"turn": turn, "level": {"number": 8, "campaign": "keeporig.cfg", "name": "Tickle"},
            "seat": {"player": 1, "victory_state": "undecided",
                     "payday": {"gap": payday_gap, "progress": 0, "turns_to_payday": payday_gap}},
            "own": {"gold": 1000, "creatures": [], "rooms": rooms if rooms is not None else [], "events": []},
            "visible": {"rooms": enemy_rooms or [], "hero_gates": gates or [], "force_by_owner": {}}}


class FakeApi:
    """get/set_agent_memory against one text, recording what was written."""

    def __init__(self, text):
        self.text = text
        self.writes = []

    def data(self, action, **kw):
        if action == "get_agent_memory":
            return {"data": self.text}
        if action == "set_agent_memory":
            self.writes.append(kw["data"])
            self.text = kw["data"]
            return None
        raise AssertionError(action)


class FormatTests(unittest.TestCase):
    def test_a_format_1_blob_from_the_previous_bridge_is_upgraded_and_written_back_as_2(self):
        with open(os.path.join(HERE, "testdata", "memory_v1.json")) as f:
            text = f.read()
        self.assertEqual(json.loads(text)["format"], 1)
        m = Memory.from_blob(text)
        self.assertEqual((m.format, m.facts, m.chapters), (2, {}, []))
        self.assertIn(m.places, ({}, {"named": {}, "auto": {}}))
        self.assertTrue(m.plan.startswith("Hold the island treasury"))
        self.assertIn("rival heart x46-48", m.notes)
        self.assertEqual(len(m.decisions), memory_mod.RECENT)
        self.assertEqual(m.rally, {"safe": [91, 217]})
        self.assertEqual(m.name_for(27, "ORC", 1400), "Orc #1")       # the names survive
        again = Memory.from_blob(m.to_blob())
        self.assertEqual(json.loads(m.to_blob())["format"], 2)
        self.assertEqual(again.plan, m.plan)

    def test_a_newer_bridges_memory_is_neither_read_nor_overwritten(self):
        newer = json.dumps({"format": 99, "game_id": "g", "plan": "theirs"})
        api = FakeApi(newer)
        mem, resumed = memory_mod.attach(api, 1, view(10))
        self.assertFalse(resumed)
        self.assertEqual(mem.plan, "")
        mem.update(plan="mine")
        mem.flush()
        self.assertEqual(api.writes, [])                          # never written over
        self.assertEqual(api.text, newer)
        mem2, note = memory_mod.reattach_after_load(api, 1, view(10))
        self.assertIn("a newer bridge wrote", note)
        mem2.flush()
        self.assertEqual(api.writes, [])


class FactsAndChaptersTests(unittest.TestCase):
    def test_facts_are_set_changed_and_dropped_one_at_a_time(self):
        m = Memory()
        warn = m.update(facts={"enemy heart": "x46-48 y39-41", "safe": "pocket at (30,72)"}, turn=1000)
        self.assertEqual(warn, "")
        self.assertEqual(m.facts["enemy_heart"], {"text": "x46-48 y39-41", "turn": 1000})
        m.update(facts={"safe": None, "enemy_heart": "moved: x50 y40"}, turn=3000)
        self.assertEqual(list(m.facts), ["enemy_heart"])
        self.assertEqual(m.facts["enemy_heart"]["turn"], 3000)
        self.assertIn("cut to %d" % memory_mod.FACT_CAP, m.update(facts={"long": "x" * 500}))
        for n in range(memory_mod.FACTS_MAX):
            m.update(facts={"f%d" % n: "v"})
        self.assertEqual(len(m.facts), memory_mod.FACTS_MAX)
        self.assertIn("not kept", m.update(facts={"one_more": "v"}))
        self.assertIn("facts is an object", m.update(facts=["x"]))
        lines = m.render_facts(now=3000 + 6 * 1200, payday=1200)
        self.assertIn("  enemy_heart = moved: x50 y40 (t3000, old)", lines)     # set 6 pay days ago
        old = Memory()
        old.update(facts={"a": "b"}, turn=0)
        self.assertIn("  a = b (t0, old)", old.render_facts(now=10000, payday=1200))
        self.assertIn("  a = b (t0)", old.render_facts(now=1000, payday=1200))
        self.assertIn("a = b", old.notes_text())

    def test_rolled_off_decisions_become_one_chapter_per_pay_day(self):
        m = Memory()
        m.update(plan="grow the army\nthen attack")
        for t in (0, 1200, 2400, 3600):
            m.add_checkpoint({"turn": t})
        m.update(chapter_note="first raid beaten off", turn=100)
        for i in range(24):
            turn = i * 150
            m.record_decision(turn, ["enemy_fight"] if i % 5 == 0 else ["timer"], "r", [{"id": i, "verb": "slap"}],
                              [{"verb": "x", "error": "E"}] if i == 3 else [])
        self.assertEqual(len(m.decisions), memory_mod.RECENT)
        # 12 rolled off: turns 0..1650 -> the pay days from 0 and from 1200.
        self.assertEqual([(c["period"], c["from"], c["to"], c["decisions"]) for c in m.chapters],
                         [(0, 0, 1050, 8), (1200, 1200, 1650, 4)])
        first = m.chapters[0]
        self.assertEqual((first["sent"], first["refused"], first["reasons"], first["plan"], first["note"]),
                         (8, 1, {"enemy_fight": 2, "timer": 6}, "grow the army", "first raid beaten off"))
        text = "\n".join(m.render_chapters())
        self.assertIn("t0-1050: 8 decisions, 8 orders, 1 refused; timer x6, enemy_fight x2; plan: grow the army; note: first raid", text)
        self.assertIn("EARLIER IN THIS GAME", m.render())
        # Past CHAPTERS_MAX, the oldest two become one.
        for t in range(4800, 4800 + 1200 * 12, 1200):
            m.add_checkpoint({"turn": t})
        for i in range(24, 24 + 120):
            m.record_decision(i * 150, ["timer"], "r", [], [])
        self.assertEqual(len(m.chapters), memory_mod.CHAPTERS_MAX)
        self.assertEqual(m.chapters[0]["from"], 0)
        self.assertEqual(sum(c["decisions"] for c in m.chapters), 24 + 120 - memory_mod.RECENT)

    def test_the_debrief_shows_the_chapters_and_the_facts(self):
        d = tempfile.mkdtemp(prefix="kfx-ef-")
        self.addCleanup(shutil.rmtree, d, True)
        store = Experience(os.path.join(d, "e.sqlite"))
        m = Memory.new_game(view(0))
        store.open_branch(m, 1, "Claude")
        m.update(facts={"enemy_heart": "x46 y39"}, turn=10)
        m.add_checkpoint({"turn": 0})
        for i in range(14):
            m.record_decision(i * 100, ["timer"], "r", [], [])
        text = experience.debrief_text(store, m, "won", {}, None)
        self.assertIn("YOUR GAME, CHAPTER BY CHAPTER", text)
        self.assertIn("t0-100: 2 decisions", text)
        self.assertIn("YOUR FINAL NOTES AND FACTS: enemy_heart = x46 y39", text)
        store.progress(m, 1400)
        self.assertEqual(store.branch(m.game_id, m.branch)["final_notes"], "enemy_heart = x46 y39")
        store.close()


ROOMS = [{"id": 1, "kind": "DUNGEON_HEART", "pos": [46, 217]}, {"id": 5, "kind": "TREASURE", "pos": [52, 210]},
         {"id": 9, "kind": "TREASURE", "pos": [60, 210]}, {"id": 7, "kind": "ENTRANCE", "pos": [25, 31]},
         {"id": 8, "kind": "BRIDGE", "pos": [70, 70]}]


class PlacesTests(unittest.TestCase):
    def test_the_seats_rooms_gates_and_enemy_rooms_are_named_from_the_view(self):
        m = Memory()
        places.refresh(m, view(100, ROOMS, [{"id": 40, "owner": 2, "kind": "DUNGEON_HEART", "pos": [139, 118]},
                                            {"id": 41, "owner": 2, "kind": "TREASURE", "pos": [130, 118]},
                                            {"id": 42, "owner": 3, "kind": "LAIR", "pos": [10, 10], "ally": True}],
                                [{"id": 60, "number": 1, "pos": [142, 58]}]))
        auto = m.places["auto"]
        self.assertEqual(sorted(auto), ["heart", "heart_P2", "hero_gate_1", "portal", "treasury", "treasury_2", "treasury_P2"])
        self.assertEqual((auto["treasury"]["room"], auto["treasury_2"]["room"]), (5, 9))
        # A treasury goes; the other keeps its name; a new one takes the free name. The enemy's: out of sight, its
        # treasury is dropped, its heart kept with when it was seen.
        rooms = [r for r in ROOMS if r["id"] != 5] + [{"id": 12, "kind": "TREASURE", "pos": [70, 210]}]
        places.refresh(m, view(900, rooms))
        self.assertEqual((auto["treasury"]["room"], auto["treasury_2"]["room"]), (12, 9))
        self.assertNotIn("treasury_P2", auto)
        self.assertEqual((auto["heart_P2"]["pos"], auto["heart_P2"]["seen"]), ([139, 118], 100))
        line = places.render(m, view(900, rooms))[0]
        self.assertIn("heart (46,217)", line)
        self.assertIn("heart_P2 (139,118, seen t100)", line)

    def test_the_agents_places_and_rally_points_come_first_and_resolve_in_orders(self):
        m = Memory()
        places.refresh(m, view(100, ROOMS))
        m.rally = {"safe": [91, 217]}
        places.name_place(m, "north gap", slab_rect=[20, 30, 24, 31])
        places.name_place(m, "heart", pos=[1, 1])                     # the agent's name wins over the bridge's
        self.assertEqual(places.resolve_pos(m, "north_gap"), [67, 91])
        self.assertEqual(places.resolve_pos(m, "heart"), [1, 1])
        self.assertEqual(places.resolve_pos(m, "safe"), [91, 217])
        self.assertEqual(places.resolve_rect(m, "north gap"), [20, 30, 24, 31])
        self.assertEqual(places.resolve_rect(m, "safe"), [30, 72, 30, 72])
        self.assertEqual(places.resolve_room(m, "treasury_2"), 9)
        with self.assertRaisesRegex(places.PlaceError, "PLACE_HAS_NO_RECT"):
            places.resolve_rect(m, "treasury")
        with self.assertRaisesRegex(places.PlaceError, "UNKNOWN_PLACE"):
            places.resolve_pos(m, "nowhere")
        with self.assertRaisesRegex(places.PlaceError, "NOT_YOUR_ROOM"):
            places.resolve_room(m, "safe")
        order = {"verb": "pick_up_and_drop", "thing_ids": [3], "to_room": "treasury"}
        self.assertEqual(places.resolve_order(order, m)["to_room"], 5)
        plain = {"verb": "slap", "thing_id": 3}
        self.assertIs(places.resolve_order(plain, m), plain)
        places.name_place(m, "heart")                                   # drop the agent's: the bridge's comes back
        self.assertEqual(places.resolve_pos(m, "heart"), [46, 217])
        with self.assertRaisesRegex(places.PlaceError, "UNKNOWN_PLACE"):
            places.name_place(m, "never named")
        with self.assertRaisesRegex(places.PlaceError, "BAD_POSITION"):
            places.name_place(m, "x", pos=[1, 2, 3])
        out = places.apply_ops(m, {"gap": [10, 10, 12, 12], "spot": [5, 5], "north_gap": None, "bad": "here"}, turn=7)
        self.assertEqual((out["named"], out["dropped"]), (["gap", "spot"], ["north_gap"]))
        self.assertIn("bad: a place is", out["errors"][0])

    def test_an_order_naming_an_unknown_place_is_refused_before_anything_is_sent(self):
        from orders import submit_batch

        class Api:
            calls = []

            def call(self, **req):
                self.calls.append(req)
                return {"success": True, "data": {"id": 1}}
        m = Memory()
        places.refresh(m, view(100, ROOMS))
        api = Api()
        sent, refused = submit_batch(api, 1, [{"verb": "cast_power", "power": "POWER_SIGHT", "pos": "heart_P9"},
                                              {"verb": "move_creature", "thing_id": 4, "pos": "heart"}], 100, 1500, memory=m)
        self.assertEqual([r["error"][:13] for r in refused], ["UNKNOWN_PLACE"])
        self.assertEqual([c["pos"] for c in api.calls], [[46, 217]])
        self.assertEqual(len(sent), 1)

    def test_the_places_worth_keeping_become_lessons_of_the_level_when_the_game_ends(self):
        d = tempfile.mkdtemp(prefix="kfx-pl-")
        self.addCleanup(shutil.rmtree, d, True)
        store = Experience(os.path.join(d, "e.sqlite"))
        m = Memory.new_game(view(0))
        rec = Recorder(store, 1, "Claude")
        rec.start(m, view(0), resumed=False)
        rec.decision(m, view(100, ROOMS, [{"id": 40, "owner": 2, "kind": "DUNGEON_HEART", "pos": [139, 118]}],
                             [{"id": 60, "number": 1, "pos": [142, 58]}]))
        places.name_place(m, "north_gap", slab_rect=[20, 30, 24, 31])
        rec.ended(m, view(9000), "won")
        texts = [i["text"] for i in store.items("level:keeporig:8", status="provisional")]
        self.assertEqual(texts, ["Place north_gap: slabs 20-24,30-31.",
                                 "Player 2's heart is at slab (46,39) = subtile (139,118).",
                                 "Hero gate 1 is at slab (47,19) = subtile (142,58)."])
        self.assertEqual({i["source"] for i in store.items("level:keeporig:8", status="provisional")}, {"places"})
        deb = experience.debrief_text(store, m, "won", {}, None)
        self.assertIn("Player 2's heart is at slab (46,39)", deb)
        self.assertEqual(rec.keep_places(m), [])                          # the same places again: not added twice
        store.close()


if __name__ == "__main__":
    unittest.main()
