#!/usr/bin/env python3
"""Offline tests of cost_report.py against synthetic JSONL fixtures: python3 scripts/llm_bridge/test_cost_report.py"""
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import cost_report  # noqa: E402


def write_log(lines):
    f = tempfile.NamedTemporaryFile(mode="w", suffix=".jsonl", delete=False)
    for line in lines:
        f.write(json.dumps(line) + "\n")
    f.close()
    return f.name


class CostReportTests(unittest.TestCase):
    def test_scripted_policy_log_has_no_token_metrics(self):
        path = write_log([
            {"decision": 1, "turn": 10, "reasons": ["start"], "think_seconds": 0.01, "sent": [{"verb": "mark_dig"}], "refused": [], "model": {}},
            {"decision": 2, "turn": 120, "reasons": ["timer"], "think_seconds": 0.02, "sent": [], "refused": [], "model": {}},
        ])
        s = cost_report.summarize_file(path)
        os.unlink(path)
        self.assertEqual(s["decisions"], 2)
        self.assertEqual(s["turn_span"], (10, 120))
        self.assertEqual(s["orders_sent"], 1)
        self.assertEqual(s["orders_refused"], 0)
        self.assertAlmostEqual(s["think_seconds_total"], 0.03)
        self.assertEqual(s["calls"], 0)

    def test_anthropic_policy_metrics_are_cumulative_so_the_last_line_is_the_total(self):
        path = write_log([
            {"decision": 1, "turn": 10, "reasons": ["start"], "think_seconds": 1.0, "sent": [{"verb": "mark_dig"}], "refused": [],
             "model": {"calls": 1, "input_tokens": 500, "output_tokens": 50, "seconds": 1.0}},
            {"decision": 2, "turn": 120, "reasons": ["timer"], "think_seconds": 1.5, "sent": [], "refused": [{"verb": "cast_power"}],
             "model": {"calls": 2, "input_tokens": 1100, "output_tokens": 90, "seconds": 2.5}},
        ])
        s = cost_report.summarize_file(path)
        os.unlink(path)
        # Not 500+1100: metrics accumulate across the session, so the last line already holds the whole total.
        self.assertEqual(s["input_tokens"], 1100)
        self.assertEqual(s["output_tokens"], 90)
        self.assertEqual(s["calls"], 2)
        self.assertAlmostEqual(s["model_seconds"], 2.5)
        # think_seconds is per decision, unlike model metrics: it IS summed.
        self.assertAlmostEqual(s["think_seconds_total"], 2.5)
        self.assertEqual(s["orders_refused"], 1)

    def test_multi_seat_log_keeps_its_seat_number(self):
        path = write_log([{"seat": 2, "decision": 1, "turn": 5, "reasons": ["start"], "think_seconds": 0.1, "sent": [], "refused": [], "model": {}}])
        s = cost_report.summarize_file(path)
        os.unlink(path)
        self.assertEqual(s["seat"], 2)

    def test_empty_log_summarizes_to_none(self):
        path = write_log([])
        s = cost_report.summarize_file(path)
        os.unlink(path)
        self.assertIsNone(s)

    def test_cost_estimate_only_appears_with_a_matching_price_entry(self):
        s = {"input_tokens": 2_000_000, "output_tokens": 100_000}
        prices = {"claude-sonnet-5": {"input_per_mtok": 3.0, "output_per_mtok": 15.0}}
        self.assertAlmostEqual(cost_report.estimate_cost(s, prices, "claude-sonnet-5"), 2_000_000 / 1e6 * 3.0 + 100_000 / 1e6 * 15.0)
        self.assertIsNone(cost_report.estimate_cost(s, prices, "some-other-model"))
        self.assertIsNone(cost_report.estimate_cost(s, None, "claude-sonnet-5"))

    def test_render_shows_a_grand_total_only_across_multiple_files(self):
        one = cost_report.summarize_file(write_log([{"decision": 1, "turn": 1, "sent": [], "refused": [], "think_seconds": 0.1, "model": {}}]))
        text_one = cost_report.render([one], None, None)
        self.assertNotIn("total:", text_one)
        two = cost_report.summarize_file(write_log([{"decision": 1, "turn": 1, "sent": [], "refused": [], "think_seconds": 0.1, "model": {}}]))
        text_two = cost_report.render([one, two], None, None)
        self.assertIn("total: 2 decisions across 2 file(s)", text_two)

    def test_main_reports_every_jsonl_file_in_a_directory(self):
        d = tempfile.mkdtemp()
        for i in range(2):
            with open(os.path.join(d, "seat_%d.jsonl" % i), "w") as f:
                f.write(json.dumps({"seat": i, "decision": 1, "turn": 1, "sent": [], "refused": [], "think_seconds": 0.1, "model": {}}) + "\n")
        rc = cost_report.main(["--dir", d])
        self.assertEqual(rc, 0)

    def test_main_fails_loudly_with_no_input(self):
        with self.assertRaises(SystemExit):
            cost_report.main([])


if __name__ == "__main__":
    unittest.main()
