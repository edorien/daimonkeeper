#!/usr/bin/env python3
"""Offline tests for gamelink.py (the pump and severity) and prompt.render_flash: python3 scripts/llm_bridge/test_gamelink.py"""
import collections
import os
import re
import sys
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gamelink  # noqa: E402
import prompt  # noqa: E402
from test_bridge import make_view  # noqa: E402
from viewstate import ViewState  # noqa: E402

ENGINE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "kfx_script", "src", "api_seat_decision.c")


class SeverityTests(unittest.TestCase):
    def test_the_table_is_the_engines(self):
        """gamelink mirrors api_seat_decision.c's reason_severity (for an engine without severity, and for decisions no
        push made): the two lists must not drift apart."""
        with open(ENGINE) as f:
            src = f.read()
        body = src[src.index("static int reason_severity"):]
        body = body[:body.index("\n}\n")]
        for name, mine in (("critical", gamelink._CRITICAL), ("combat", gamelink._COMBAT), ("problem", gamelink._PROBLEM)):
            m = re.search(r"%s\[\] = \{(.*?)\};" % name, body, re.S)
            self.assertIsNotNone(m, name)
            self.assertEqual(sorted(re.findall(r'"(\w+)"', m.group(1))), sorted(mine), name)

    def test_severity_of_reasons_and_pushes(self):
        self.assertEqual(gamelink.severity_of(["quarter_1", "intruder"]), gamelink.SEV_COMBAT)
        self.assertEqual(gamelink.severity_of(["heart_attacked", "intruder"]), gamelink.SEV_CRITICAL)
        self.assertEqual(gamelink.severity_of([]), gamelink.SEV_ROUTINE)
        self.assertEqual(gamelink.event_severity({"data": {"reasons": "creature_hungry"}}), gamelink.SEV_PROBLEM)
        self.assertEqual(gamelink.event_severity({"data": {"reasons": "creature_hungry", "severity": 0}}), 0)  # the engine's word
        self.assertEqual(gamelink.event_where({"data": {"where_x": 4, "where_y": 9}}), [4, 9])
        self.assertIsNone(gamelink.event_where({"data": {"where_x": -1, "where_y": -1}}))


class FakeApi:
    """Api's surface the link uses, with pushes the test queues (no socket: the link waits on api.wait_event)."""

    def __init__(self):
        self.events = collections.deque()
        self.buf = b""
        self.sock = None
        self.pushed = collections.deque()
        self.views = 0
        self.bytes_received = 0

    def drain_events(self, names):
        out = [ev for ev in self.pushed if ev.get("event") in names]
        self.pushed.clear()
        return out

    def wait_event(self, names, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.pushed:
                return self.pushed.popleft()
            time.sleep(0.01)
        return None

    def data(self, **req):
        self.views += 1
        v = make_view(turn=100 + self.views)
        v.update(view_id=self.views, mode="full")
        return v


def due(reasons, severity=None):
    d = {"reasons": reasons, "turn": 100, "seq": 1}
    if severity is not None:
        d["severity"] = severity
    return {"event": "DECISION_DUE", "data": d}


class GameLinkTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeApi()
        self.state = ViewState()
        self.state.update(self.api, 1)
        self.link = gamelink.GameLink(self.api, 1, self.state)

    def test_poll_collects_pushes_and_an_urgent_one_refreshes_the_view(self):
        self.link.poll()                           # the first poll is due for a refresh anyway
        views = self.api.views
        self.api.pushed.append(due("quarter_1"))
        with self.link.lock:
            self.link.poll()
        self.assertEqual(self.api.views, views, "a routine push waits for the next refresh")
        self.api.pushed.append(due("intruder"))
        with self.link.lock:
            self.link.poll()
        self.assertEqual(self.api.views, views + 1, "an urgent one refreshes the view at once")
        self.assertEqual([e["data"]["reasons"] for e in self.link.urgent_pending()], ["intruder"])
        self.assertEqual(len(self.link.take_events()), 2)
        self.assertEqual(self.link.take_events(), [])
        self.assertEqual(self.link.urgent_pending(), [])

    def test_a_link_without_refresh_only_reads_the_pushes(self):
        link = gamelink.GameLink(self.api, 1, self.state, refresh=False)
        views = self.api.views
        self.api.pushed.append(due("intruder"))
        with link.lock:
            link.poll()
        self.assertEqual(self.api.views, views)
        self.assertEqual(len(link.events), 1)

    def test_a_wait_without_a_pump_reads_the_socket_itself_and_times_out(self):
        threading.Timer(0.1, lambda: self.api.pushed.append(due("quarter_2"))).start()
        started = time.time()
        self.assertTrue(self.link.wait_for_events(2.0))
        self.assertLess(time.time() - started, 1.0)
        self.assertEqual(self.link.take_events()[0]["data"]["reasons"], "quarter_2")
        started = time.time()
        self.assertFalse(self.link.wait_for_events(0.2))
        self.assertGreaterEqual(time.time() - started, 0.2)

    def test_a_pump_error_reaches_the_waiter(self):
        self.link.error = ConnectionError("the game closed the connection")
        self.link._thread = object()               # as if a pump had run and died
        with self.assertRaises(ConnectionError):
            self.link.wait_for_events(1.0)


class FlashTests(unittest.TestCase):
    def view(self):
        v = make_view()
        v["map"]["rows"] = ["A1" * 30] * 30
        v["map"]["width"], v["map"]["height"] = 30, 30
        own = v["own"]
        own["creatures"] = ([{"id": 10 + i, "kind": "IMP", "level": 1, "health": 60, "digger": True, "pos": [40 + i, 40], "state": "Dig"}
                             for i in range(3)]
                            + [{"id": 20, "kind": "ORC", "level": 3, "health": 300, "digger": False, "pos": [44, 42], "state": "Fight"},
                               {"id": 21, "kind": "TROLL", "level": 2, "health": 200, "digger": False, "pos": [80, 80], "state": "Sleep"}])
        own["intruders"] = {"count": 1, "creatures": [{"id": 90, "kind": "KNIGHT", "level": 4, "owner": 4, "pos": [42, 41]}]}
        v["visible"]["creatures"] = [{"id": 90, "kind": "KNIGHT", "level": 4, "owner": 4, "health": 900, "pos": [42, 41]},
                                     {"id": 91, "kind": "ARCHER", "level": 2, "owner": 4, "health": 200, "pos": [85, 85]}]
        v["seat"]["decision"] = {"reasons": ["intruder"], "severity": 1, "where": [42, 41], "losses": []}
        return v

    def test_the_flash_shows_the_spot_both_sides_and_the_imps_and_is_short(self):
        st = ViewState()
        st.view = self.view()
        st.mark_rendered()
        text = prompt.render_flash(st, None, ["intruder"], 1)
        self.assertIn("FLASH DECISION (severity 1, combat) because: intruder", text)
        self.assertIn("WHERE: subtile (42,41) = slab (14,13)", text)    # from the view's decision
        self.assertIn("ENEMIES NEAR (1): #90 KNIGHT L4", text)            # not the archer far away
        self.assertIn("YOURS NEAR (4 of 5)", text)                        # not the troll far away
        self.assertIn("IMPS AT RISK (ids, nearest first): 11 12 10", text)
        self.assertIn("MAP around it (slabs):", text)
        self.assertNotIn("RECENT DECISIONS", text)
        self.assertLess(len(text), 2500)
        self.assertEqual(st.last_diff, {}, "a flash is a decision's text: the news starts again")

    def test_without_a_place_it_falls_back_to_the_first_intruder(self):
        v = self.view()
        v["seat"]["decision"].pop("where")
        self.assertEqual(prompt.flash_where(v), [42, 41])
        v["own"]["intruders"] = {"count": 0, "creatures": []}
        v["seat"]["decision"]["losses"] = [{"what": "ground_lost", "slabs": 1, "pos": [7, 8], "turn": 99}]
        self.assertEqual(prompt.flash_where(v), [7, 8])


if __name__ == "__main__":
    unittest.main()
