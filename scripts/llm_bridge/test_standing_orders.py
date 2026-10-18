#!/usr/bin/env python3
"""Offline tests of standing_orders.py (the combat assistant on the bridge side): python3 scripts/llm_bridge/test_standing_orders.py"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import memory as memory_mod  # noqa: E402
import standing_orders as so  # noqa: E402


class PrepareTests(unittest.TestCase):
    def test_names_by_slot_and_settings_kept_apart(self):
        req, names = so.prepare([{"name": "heal", "power": "POWER_HEAL_CREATURE"}, {"power": "POWER_SPEED"}],
                                {"reserve": 100, "enabled": None, "bogus": 1})
        self.assertEqual(names, ["heal", "rule 1"])
        self.assertEqual(req["reserve"], 100)
        self.assertNotIn("enabled", req)           # None means: not given
        self.assertNotIn("bogus", req)
        self.assertEqual(req["rules"][0]["name"], "heal")

    def test_rules_left_out_keep_them(self):
        req, names = so.prepare(None, {"enabled": False})
        self.assertNotIn("rules", req)
        self.assertEqual(names, [])

    def test_errors(self):
        with self.assertRaises(so.RulesError) as e:
            so.prepare([{"name": "x"}, {"name": "x"}], {})
        self.assertEqual((e.exception.code, e.exception.rule, e.exception.name), ("DUPLICATE_NAME", 1, "x"))
        with self.assertRaises(so.RulesError) as e:
            so.prepare("all", {})
        self.assertEqual(e.exception.code, "BAD_RULES")
        with self.assertRaises(so.RulesError) as e:
            so.prepare([3], {})
        self.assertEqual((e.exception.code, e.exception.rule), ("BAD_RULE", 0))


class DescribeTests(unittest.TestCase):
    def test_cast_and_power_off(self):
        self.assertEqual(so.describe({"power": "POWER_CHICKEN", "level": 0, "target": "enemy_in_fight", "pick": "most_valuable",
                                      "when": [["force_ratio", ">", 120]], "budget": 4000, "max_allies_hit": 2}),
                         "POWER_CHICKEN L0 on enemy_in_fight (most valuable first) when force_ratio > 120 (budget 4000 per pay day; may hit up to 2 of ours)")
        self.assertEqual(so.describe({"power_off": "POWER_OBEY", "when": []}), "switch POWER_OBEY off always")


class OverrideTests(unittest.TestCase):
    PRESET = {"description": "d", "enabled": True, "reserve": 1000, "rules": [
        {"name": "heal", "power": "POWER_HEAL_CREATURE", "level": 2},
        {"name": "buff", "power": "POWER_SPEED"},
        {"name": "floor", "power_off": "POWER_OBEY"}]}

    def test_no_overrides_is_the_preset_without_its_description(self):
        req = so.apply_overrides(self.PRESET, None)
        self.assertEqual((req["enabled"], req["reserve"], [r["name"] for r in req["rules"]]), (True, 1000, ["heal", "buff", "floor"]))
        self.assertNotIn("description", req)

    def test_settings_drop_change_add(self):
        req = so.apply_overrides(self.PRESET, {"reserve": 5, "drop": ["buff"], "rules": {"heal": {"level": 5}, "new": {"power": "POWER_CHICKEN"}}})
        self.assertEqual(req["reserve"], 5)
        self.assertEqual([r["name"] for r in req["rules"]], ["heal", "floor", "new"])
        self.assertEqual(req["rules"][0]["level"], 5)
        self.assertEqual(self.PRESET["rules"][0]["level"], 2)    # the preset itself is untouched


class RenderTests(unittest.TestCase):
    def view(self, **assist):
        a = {"enabled": True, "interval": 2, "reserve": 500, "spent_this_payday": 0, "rules": [], "casts": []}
        a.update(assist)
        return {"turn": 100, "own": {"assist": a, "creatures": [{"id": 7, "kind": "ORC", "health": 300}]}, "visible": {"creatures": []}}

    def test_nothing_when_off_without_rules(self):
        self.assertEqual(so.render(self.view(enabled=False), None, {}), [])

    def test_deltas_since_the_last_decision_and_names(self):
        mem = memory_mod.Memory()
        mem.rule_names = ["heal"]
        seen = {}
        rule = {"slot": 0, "power": "POWER_HEAL_CREATURE", "level": 1, "target": "ally_in_fight", "when": [], "fired": 1, "spent": 300, "state": "ok"}
        first = so.render(self.view(rules=[rule]), mem, seen)
        self.assertIn("heal: POWER_HEAL_CREATURE L1 on ally_in_fight always | fired 1, spent 300", first[1])
        self.assertNotIn("since last decision", first[1])            # nothing to compare with yet
        rule2 = dict(rule, fired=3, spent=900, state="budget_spent")
        cast = {"turn": 150, "rule": 0, "power": "POWER_HEAL_CREATURE", "level": 1, "target": 7, "cost": 300}
        v2 = self.view(rules=[rule2], casts=[cast])
        v2["turn"] = 200
        later = so.render(v2, mem, seen)
        self.assertIn("(+2, +600 gold since last decision) | budget spent for this pay day", later[1])
        self.assertIn("turn 150 heal POWER_HEAL_CREATURE -> Orc #1 (now 300 hp)", later[2])
        self.assertEqual(so.render(v2, mem, seen)[1].count("+0, +0"), 1)  # a third look: nothing new

    def test_rules_in_words_round_trip(self):
        mem = memory_mod.Memory()
        mem.rule_names = ["floor"]
        out = so.rules_in_words(self.view(rules=[{"slot": 0, "power_off": "POWER_OBEY", "when": [["gold", "<", 800]], "fired": 4, "spent": 0, "state": "ok"}]), mem)
        self.assertEqual(out["rules"], [{"name": "floor", "power_off": "POWER_OBEY", "when": [["gold", "<", 800]]}])
        self.assertEqual((out["enabled"], out["reserve"]), (True, 500))



class HandRuleTests(unittest.TestCase):
    def test_hand_rules_in_words_and_what_they_did(self):
        import standing_orders
        self.assertIn("evacuate_diggers (hand: lift the imps near an enemy to (91,217), up to 6)",
                      standing_orders.describe({"hand": "evacuate_diggers", "to": [91, 217], "count": 6}))
        self.assertIn("to a lair", standing_orders.describe({"hand": "lift_wounded", "count": 8}))
        self.assertIn("each at least 70% of the strongest enemy",
                      standing_orders.describe({"hand": "reinforce", "count": 3, "min_match": 70, "when": [["force_ratio", ">=", 100]]}))
        view = {"turn": 500, "paused": False, "own": {"creatures": [{"id": 7, "kind": "IMP", "health": 50}], "assist": {
            "enabled": True, "interval": 2, "reserve": 0, "spent_this_payday": 0, "max_spend": 0,
            "rules": [{"slot": 0, "hand": "evacuate_diggers", "to": [91, 217], "count": 6, "fired": 0, "spent": 0, "state": "NO_DROP_SPOT"}],
            "reflexes": [{"turn": 480, "rule": 0, "hand": "evacuate_diggers", "things": [7], "pos": [92, 217], "verb_id": 3}],
            "casts": []}}, "visible": {"creatures": []}, "seat": {}}
        lines = "\n".join(standing_orders.render(view, None, {"_turn": 400}))
        self.assertIn("held back: nowhere to drop near it", lines)
        self.assertIn("hand rules since last decision: turn 480 rule 0 moved #7 (now 50 hp) to (92,217)", lines)


if __name__ == "__main__":
    unittest.main()
