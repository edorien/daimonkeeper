#!/usr/bin/env python3
"""Offline tests for latency.py (each decision's latency chain) and its place on the game's record."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from latency import LatencyChain, due_info  # noqa: E402


def due(turn, raised, reasons="enemy_fight"):
    return {"event": "DECISION_DUE", "data": {"player": 0, "turn": turn, "reasons": reasons, "raised": raised}}


class LatencyTests(unittest.TestCase):
    def test_due_info_takes_the_earliest_of_coalesced_events(self):
        self.assertEqual(due_info([due(500, "420,480"), due(510, "505")]), (500, 420))
        self.assertEqual(due_info([{"event": "DECISION_DUE", "data": {"turn": 700}}]), (700, 700))  # an engine without raised
        self.assertEqual(due_info([]), (None, None))

    def test_a_decision_from_cause_to_gesture(self):
        lat = LatencyChain()
        lat.delivered(["enemy_fight"], [due(500, "420")], 530, wall=10.0)
        lat.answered([{"id": 7}, {"id": 8}], wall=25.5)
        lat.results([{"id": 6, "status": "done", "turn": 600, "submitted": 400, "started": 410}])
        self.assertEqual(lat.done, [])                     # its first order has not run yet
        lat.results([{"id": 7, "status": "done", "turn": 640, "submitted": 600, "started": 625}])
        rec = lat.take_finished()[0]
        self.assertEqual((rec["hold"], rec["delivery"], rec["think"], rec["queue"], rec["gesture"], rec["total"]),
                         (80, 30, 70, 25, 15, 205))
        self.assertEqual(rec["think_seconds"], 15.5)
        self.assertEqual(rec["orders"], 2)
        self.assertEqual(lat.take_finished(), [])
        s = lat.summary()
        self.assertEqual(s["links"]["total"], {"median": 205, "max": 205, "n": 1})

    def test_no_orders_unanswered_refused_and_redelivered(self):
        lat = LatencyChain()
        lat.delivered(["quarter_1"], [], 100, wall=0.0)
        lat.answered([], wall=3.0)                         # waits: nothing to time past the think
        lat.delivered(["alarm"], [due(200, "199", "alarm")], 205, wall=5.0)
        lat.delivered(["intruder"], [due(300, "300", "intruder")], 301, wall=9.0)  # answered nothing, waited again
        lat.redelivering()
        lat.delivered(["intruder", "ground_lost"], [], 340, wall=40.0)            # the same decision, handed out again
        lat.answered([{"id": 3}], wall=41.0)
        lat.results([{"id": 3, "status": "rejected", "error": "EXPIRED", "turn": 900, "submitted": 345, "started": -1}])
        recs = lat.take_finished()
        self.assertEqual([r["reasons"] for r in recs], [["quarter_1"], ["alarm"], ["intruder"]])
        self.assertEqual(recs[0]["orders"], 0)
        self.assertTrue(recs[1]["unanswered"])
        self.assertEqual((recs[2]["redelivered"], recs[2]["error"], recs[2]["think"], recs[2]["started"]), (1, "EXPIRED", 44, None))
        self.assertNotIn("queue", recs[2])
        self.assertEqual(recs[2]["think_seconds"], 32.0)

    def test_a_result_that_fell_out_of_the_ring(self):
        lat = LatencyChain()
        lat.delivered([], [], 10)
        lat.answered([{"id": 1}])
        lat.results([{"id": i, "status": "done", "turn": 50, "submitted": 20, "started": 20} for i in range(10, 26)])
        self.assertTrue(lat.done[0]["lost"])

    def test_the_record_carries_the_summary(self):
        import experience
        store = experience.Experience(None)
        rec = experience.Recorder(store, 0, "test")
        rec.latency.delivered(["start"], [], 0, wall=0.0)
        rec.latency.answered([], wall=1.0)
        final = rec._final({"turn": 5, "seat": {"victory_state": "won"}, "own": {}}, None)
        self.assertEqual(final["latency"]["decisions"], 1)


if __name__ == "__main__":
    unittest.main()
