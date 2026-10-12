#!/usr/bin/env python3
"""Offline tests of combat_report.py: python3 scripts/llm_bridge/test_combat_report.py

The log fixture is real COMBATSTAT: output from ftest combat_stats (src/ftests/tests/ftest_combat_stats.c), plus
lines for a second keeper and a log's ordinary noise around them."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import combat_report  # noqa: E402

LOG = """\
Sync: [101] some_other_function: unrelated line
COMBATSTAT: orders player=1 decision_turn=100 latency=2 combat=1
COMBATSTAT: cast player=1 turn=102 power=POWER_HEAL_CREATURE level=0 target=28 gold=300 in_fight=1
COMBATSTAT: death player=1 turn=108 kind=ORC level=1 digger=0 window_low=105 window_critical=5 heal_after_low=99 fight_age=107
COMBATSTAT: fight player=1 start=1 end=108 involved=3 deaths=1 digger_deaths=0 casts=1 cast_gold=300 first_cast_after=101 first_heal_after=101 decision_after=99 orders_after=101
COMBATSTAT: cast player=1 turn=212 power=POWER_CALL_TO_ARMS level=0 target=0 gold=1 in_fight=0
COMBATSTAT: upkeep player=1 power=POWER_CALL_TO_ARMS from=212 to=273 gold=60
COMBATSTAT: cast player=1 turn=274 power=POWER_OBEY level=0 target=0 gold=25 in_fight=0
COMBATSTAT: upkeep player=1 power=POWER_OBEY from=274 to=347 gold=100
COMBATSTAT: payday player=1 turn=350 gold=999514 wages=0
COMBATSTAT: death player=0 turn=500 kind=IMP level=2 digger=1 window_low=-1 window_critical=-1 heal_after_low=-1 fight_age=20
COMBATSTAT: death player=0 turn=510 kind=TROLL level=4 digger=0 window_low=40 window_critical=10 heal_after_low=-1 fight_age=30
COMBATSTAT: death player=0 turn=530 kind=TROLL level=4 digger=0 window_low=80 window_critical=30 heal_after_low=-1 fight_age=50
COMBATSTAT: fight player=0 start=480 end=560 involved=4 deaths=3 digger_deaths=1 casts=0 cast_gold=0 first_cast_after=-1 first_heal_after=-1 decision_after=-1 orders_after=-1
COMBATSTAT: payday player=0 turn=600 gold=100 wages=900
COMBATSTAT: unknown_kind player=0 x=1
"""


class LogTests(unittest.TestCase):
    def setUp(self):
        self.parsed = combat_report.parse_log_lines(LOG.splitlines())

    def test_lines_are_split_by_player_and_kind(self):
        self.assertEqual(sorted(self.parsed), [0, 1])
        self.assertEqual({k: len(v) for k, v in self.parsed[1].items()},
                         {"deaths": 1, "fights": 1, "casts": 3, "paydays": 1, "upkeep": 2, "orders": 1})
        self.assertEqual(self.parsed[1]["casts"][0]["power"], "POWER_HEAL_CREATURE")   # text stays text
        self.assertEqual(self.parsed[1]["deaths"][0]["window_low"], 105)               # numbers become numbers

    def test_headline_for_the_ftest_keeper(self):
        h = combat_report.headline_from_log(self.parsed[1])
        self.assertEqual((h["fights"], h["combat_deaths"], h["deaths_seen_low"], h["deaths_healed"]), (1, 1, 1, 1))
        self.assertEqual((h["median_window_low"], h["median_window_critical"]), (105, 5))
        self.assertEqual((h["median_first_cast_after"], h["median_decision_after"], h["median_orders_after"]), (101, 99, 101))
        self.assertEqual((h["median_decision_latency"], h["median_decision_latency_combat"]), (2, 2))
        self.assertEqual((h["casts"], h["casts_in_fights"], h["cast_gold"]), (3, 1, 326))
        self.assertEqual((h["cta_upkeep_gold"], h["obey_upkeep_gold"]), (60, 100))
        self.assertEqual((h["paydays"], h["paydays_short"]), (1, 0))

    def test_headline_skips_missing_samples(self):
        h = combat_report.headline_from_log(self.parsed[0])
        self.assertEqual((h["combat_deaths"], h["combat_digger_deaths"], h["deaths_seen_low"]), (3, 1, 2))
        self.assertEqual(h["median_window_low"], 40)            # lower middle of 40, 80; the imp's -1 is no sample
        self.assertIsNone(h["median_first_cast_after"])         # no fight had a cast
        self.assertIsNone(h["median_decision_latency"])         # a human: no decisions
        self.assertEqual(h["paydays_short"], 1)

    def test_rows_from_a_log_file_and_one_player(self):
        f = tempfile.NamedTemporaryFile(mode="w", suffix=".log", delete=False)
        f.write(LOG)
        f.close()
        self.addCleanup(os.unlink, f.name)
        self.assertEqual([label.split()[-1] for label, _ in combat_report.rows_from_logs([f.name])], ["0", "1"])
        self.assertEqual(len(combat_report.rows_from_logs([f.name], player=1)), 1)


class StatsTests(unittest.TestCase):
    STATS = {"since_turn": 5, "turn": 900, "low_pct": 35, "critical_pct": 20,
             "fights": {"count": 2, "with_cast": 0, "median_first_cast_after": -1, "median_decision_after": 30,
                        "median_orders_after": -1, "recent": [{"casts": 0}, {"casts": 0}]},
             "deaths": {"combat": 2, "combat_diggers": 1, "seen_low": 1, "healed": 0, "median_window_low": 40,
                        "median_window_critical": -1},
             "casts": {"count": 0, "gold": 0, "by_power": {}},
             "upkeep": {"cta_gold": 500, "obey_gold": 75},
             "paydays": {"count": 3, "short": 1},
             "decisions": {"count": 9, "answered": 0, "median_latency": -1, "combat_answered": 0, "median_latency_combat": -1}}

    def test_minus_one_from_the_game_means_no_sample(self):
        h = combat_report.headline_from_stats(self.STATS)
        self.assertEqual((h["median_window_low"], h["median_window_critical"]), (40, None))
        self.assertEqual((h["median_first_cast_after"], h["median_decision_after"], h["median_orders_after"]), (None, 30, None))
        self.assertIsNone(h["median_decision_latency"])

    def test_text_and_markdown(self):
        text = combat_report.render_text(self.STATS)
        self.assertIn("first power cast: - (in 0 of 2 fights)", text)
        self.assertIn("casts: none", text)
        self.assertIn("call to arms 500 gold, must obey 75 gold", text)
        md = combat_report.markdown([("game A", combat_report.headline_from_stats(self.STATS))])
        self.assertIn("| | game A |", md)
        self.assertIn("| <35% -> death | 40 (2.0s) |", md)
        self.assertIn("| start -> first cast | - |", md)
        self.assertEqual(combat_report.markdown([]), "(no games with combat statistics)")

    def test_median_is_the_lower_middle(self):
        self.assertEqual(combat_report.median([4, 1, 3, 2]), 2)
        self.assertIsNone(combat_report.median([]))


if __name__ == "__main__":
    unittest.main()
