#!/usr/bin/env python3
"""Offline tests for triggers.py (contingency triggers): python3 scripts/llm_bridge/test_triggers.py"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import triggers  # noqa: E402
from memory import Memory  # noqa: E402
from test_bridge import make_view  # noqa: E402

RALLY = {"safe": [10, 10]}


def view():
    v = make_view(turn=1000)
    v["own"]["creatures"] = ([{"id": 10 + i, "kind": "IMP", "level": 1, "health": 60, "health_pct": 100, "digger": True,
                               "pos": [44 + i, 42], "state": "Dig"} for i in range(3)]
                             + [{"id": 20, "kind": "ORC", "level": 3, "health": 50, "health_pct": 20, "digger": False, "pos": [42, 41], "state": "CreatureInCombat"},
                                {"id": 21, "kind": "TROLL", "level": 2, "health": 300, "health_pct": 90, "digger": False, "pos": [90, 90], "state": "Sleep"},
                                {"id": 22, "kind": "TROLL", "level": 2, "health": 300, "health_pct": 90, "digger": False, "pos": [70, 70], "state": "Sleep"}])
    v["own"]["intruders"] = {"count": 1, "creatures": [{"id": 90, "kind": "KNIGHT", "level": 4, "owner": 4, "pos": [42, 42]}]}
    v["visible"]["creatures"] = [{"id": 90, "kind": "KNIGHT", "level": 4, "owner": 4, "health": 900, "pos": [42, 42]},
                                 {"id": 91, "kind": "ARCHER", "level": 2, "owner": 4, "health": 200, "pos": [43, 42]}]
    return v


def evac(**kw):
    t = {"name": "evac", "when": {"reasons": ["intruder"], "facts": [["imps_near", ">", 0]]},
         "do": [{"verb": "pick_up_and_drop", "thing_ids": "$imps", "pos": "$drop_near:rally:safe"}]}
    t.update(kw)
    return t


class ValidateTests(unittest.TestCase):
    def test_defaults_and_counters(self):
        t = triggers.validate([{"name": "x", "do": [{"verb": "cast_power", "power": "POWER_HEAL_CREATURE", "thing_id": "$wounded"}]}], RALLY)[0]
        self.assertEqual(t["when"]["reasons"], triggers.DEFAULT_REASONS)
        self.assertEqual((t["cooldown"], t["armed"], t["fired"]), (triggers.DEFAULT_COOLDOWN, True, 0))

    def test_errors_name_the_trigger(self):
        cases = [
            ([evac(do=[{"verb": "pick_up_and_drop", "thing_ids": "$imps", "pos": "$rally:nowhere"}])], "UNKNOWN_RALLY"),
            ([evac(do=[{"verb": "pick_up_and_drop", "thing_ids": "$everyone", "pos": "$where"}])], "BAD_PLACEHOLDER"),
            ([evac(do=[{"verb": "dance"}])], "BAD_ORDER"),
            ([evac(do=[{"verb": "slap", "thing_id": 1}] * 5)], "BAD_ORDERS"),
            ([evac(), evac()], "DUPLICATE_NAME"),
            ([evac(when={"region": [1, 2, 3]})], "BAD_REGION"),
            ([evac(when={"facts": [["mood", ">", 1]]})], "BAD_FACT"),
            ([{"do": [{"verb": "slap", "thing_id": 1}]}], "MISSING_NAME"),
            ([evac()] * 13, "TOO_MANY_TRIGGERS"),
        ]
        for trigs, code in cases:
            with self.assertRaises(triggers.TriggerError) as cm:
                triggers.validate(trigs, RALLY)
            self.assertEqual(cm.exception.code, code)
        with self.assertRaises(triggers.TriggerError) as cm:
            triggers.validate([evac(do=[{"verb": "slap", "thing_id": "$nobody"}])], RALLY)
        self.assertEqual((cm.exception.index, cm.exception.name), (0, "evac"))

    def test_an_unchanged_trigger_keeps_its_counters(self):
        old = triggers.validate([evac()], RALLY)
        old[0].update(fired=3, last_turn=900)
        new = triggers.keep_counters(triggers.validate([evac()], RALLY), old)
        self.assertEqual((new[0]["fired"], new[0]["last_turn"]), (3, 900))
        changed = triggers.keep_counters(triggers.validate([evac(cooldown=5)], RALLY), old)
        self.assertEqual(changed[0]["fired"], 3, "the cooldown is not part of when/do")
        rewritten = triggers.keep_counters(triggers.validate([evac(when={"reasons": ["alarm"]})], RALLY), old)
        self.assertEqual(rewritten[0]["fired"], 0)


class MatchTests(unittest.TestCase):
    def test_reasons_region_rally_facts_cooldown_expiry(self):
        v = view()
        t = triggers.validate([evac()], RALLY)[0]
        self.assertTrue(triggers.matches(t, ["intruder"], [42, 42], v, RALLY))
        self.assertFalse(triggers.matches(t, ["enemy_fight"], [42, 42], v, RALLY))
        self.assertFalse(triggers.matches(t, ["intruder"], [150, 150], v, RALLY), "no imp near it: the fact fails")
        t2 = triggers.validate([evac(when={"reasons": ["intruder"], "region": [13, 13, 15, 15]})], RALLY)[0]
        self.assertTrue(triggers.matches(t2, ["intruder"], [42, 42], v, RALLY))        # slab (14,14)
        self.assertFalse(triggers.matches(t2, ["intruder"], [60, 42], v, RALLY))
        self.assertFalse(triggers.matches(t2, ["intruder"], None, v, RALLY), "a region needs a place")
        t3 = triggers.validate([evac(when={"reasons": ["intruder"], "near_rally": "safe"})], RALLY)[0]
        self.assertTrue(triggers.matches(t3, ["intruder"], [20, 20], v, RALLY))
        self.assertFalse(triggers.matches(t3, ["intruder"], [42, 42], v, RALLY))
        t["last_turn"] = 900
        self.assertFalse(triggers.matches(t, ["intruder"], [42, 42], v, RALLY), "cooling down")
        t["last_turn"] = 600
        self.assertTrue(triggers.matches(t, ["intruder"], [42, 42], v, RALLY))
        t["expires_turn"] = 999
        self.assertFalse(triggers.matches(t, ["intruder"], [42, 42], v, RALLY))
        t["expires_turn"], t["armed"] = None, False
        self.assertFalse(triggers.matches(t, ["intruder"], [42, 42], v, RALLY))


class ResolveTests(unittest.TestCase):
    def test_placeholders(self):
        v = view()
        spots = []
        o, why = triggers.resolve({"verb": "pick_up_and_drop", "thing_ids": "$imps:2", "pos": "$drop_near:rally:safe"}, [42, 42], v, RALLY,
                                  lambda p: spots.append(p) or [p[0] + 1, p[1]])
        self.assertIsNone(why)
        self.assertEqual(o["thing_ids"], [10, 11])                 # nearest first, two
        self.assertEqual((o["pos"], spots), ([11, 10], [[10, 10]]))
        self.assertEqual((o["priority"], o["max_age_turns"]), ("now", triggers.NOW_MAX_AGE))
        o, _ = triggers.resolve({"verb": "pick_up_and_drop", "thing_ids": "$wounded", "to_room": "$heart"}, [42, 42], v, RALLY, None)
        self.assertEqual((o["thing_ids"], o["to_room"]), ([20], 1))
        o, _ = triggers.resolve({"verb": "pick_up_and_drop", "thing_ids": "$reserve:6", "pos": "$where"}, [42, 42], v, RALLY, None)
        self.assertEqual((o["thing_ids"], o["pos"]), ([22, 21], [42, 42]))   # away from it, nearest first; not the one fighting
        o, _ = triggers.resolve({"verb": "cast_power", "power": "POWER_LIGHTNING", "thing_id": "$enemy"}, [42, 42], v, RALLY, None)
        self.assertEqual(o["thing_id"], 90)                          # the strongest
        o, why = triggers.resolve({"verb": "pick_up_and_drop", "thing_ids": "$imps", "pos": "$where"}, [150, 150], v, RALLY, None)
        self.assertIsNone(o)
        self.assertIn("nobody", why)
        o, why = triggers.resolve({"verb": "drop", "pos": "$drop_near:where"}, [42, 42], v, RALLY, lambda p: None)
        self.assertIn("no spot", why)


class FireTests(unittest.TestCase):
    def setUp(self):
        self.mem = Memory()
        self.mem.rally = dict(RALLY)
        self.mem.triggers = triggers.validate([evac(cooldown=0)], RALLY)

    def test_a_firing_sends_logs_and_counts(self):
        sent_orders = []

        def send(orders):
            sent_orders.extend(orders)
            return [{"verb": o["verb"], "id": 5, "summary": "lift"} for o in orders], []
        fired = triggers.fire(self.mem, view(), ["intruder"], [42, 42], RALLY, lambda p: p, send)
        self.assertEqual(len(fired), 1)
        self.assertEqual(sent_orders[0]["thing_ids"], [10, 11, 12])
        self.assertEqual((self.mem.triggers[0]["fired"], self.mem.triggers[0]["last_turn"]), (1, 1000))
        self.assertEqual(self.mem.trigger_log[-1]["sent"][0]["id"], 5)
        triggers.note_results(self.mem, [{"id": 5, "status": "rejected", "error": "EXPIRED"}])
        self.assertEqual(self.mem.trigger_log[-1]["sent"][0]["error"], "EXPIRED")
        text = "\n".join(triggers.render(self.mem, since_turn=900))
        self.assertIn("evac: on intruder", text)
        self.assertIn("FIRED turn 1000 evac", text)
        self.assertIn("RALLY POINTS (subtiles): safe (10,10)", text)
        self.assertNotIn("FIRED", "\n".join(triggers.render(self.mem, since_turn=1000)))

    def test_two_refusals_in_a_row_disarm_it_and_nothing_to_do_is_no_firing(self):
        def refuse(orders):
            return [], [{"verb": o["verb"], "error": "CANNOT_DROP_HERE"} for o in orders]
        triggers.fire(self.mem, view(), ["intruder"], [42, 42], RALLY, lambda p: p, refuse)
        self.assertTrue(self.mem.triggers[0]["armed"])
        triggers.fire(self.mem, view(), ["intruder"], [42, 42], RALLY, lambda p: p, refuse)
        self.assertFalse(self.mem.triggers[0]["armed"])
        self.assertIn("CANNOT_DROP_HERE", self.mem.triggers[0]["disarmed"])
        self.assertIn("DISARMED", "\n".join(triggers.render(self.mem)))
        mem = Memory()
        mem.triggers = triggers.validate([evac(when={"reasons": ["intruder"]})], RALLY)
        self.assertEqual(triggers.fire(mem, view(), ["intruder"], [150, 150], RALLY, lambda p: p, refuse), [])
        self.assertEqual((mem.triggers[0]["fired"], mem.triggers[0]["last_turn"]), (0, None))

    def test_once(self):
        self.mem.triggers = triggers.validate([evac(once=True)], RALLY)
        triggers.fire(self.mem, view(), ["intruder"], [42, 42], RALLY, lambda p: p, lambda o: ([{"verb": "x", "id": 1}], []))
        self.assertFalse(self.mem.triggers[0]["armed"])


class PresetTests(unittest.TestCase):
    def test_presets_are_valid_triggers(self):
        for name in triggers.PRESETS:
            t = triggers.preset(name, {"cooldown": 50})
            v = triggers.validate([t], RALLY)[0]
            self.assertEqual((v["name"], v["cooldown"]), (name, 50))
        with self.assertRaises(triggers.TriggerError):
            triggers.preset("nope")

    def test_memory_keeps_them(self):
        mem = Memory.new_game()
        mem.rally = dict(RALLY)
        mem.triggers = triggers.validate([evac()], RALLY)
        back = Memory.from_blob(mem.to_blob())
        self.assertEqual((back.triggers[0]["name"], back.rally), ("evac", RALLY))


if __name__ == "__main__":
    unittest.main()
