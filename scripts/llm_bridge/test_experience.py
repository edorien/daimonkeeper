"""Tests for experience.py: game records across attempts, checkpoints, lessons with history, and the rules that decide
what a result means (docs/refactor/AI/omissions/09-persistent-memory.md section 6)."""
import os
import shutil
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import experience  # noqa: E402
from experience import Experience, Recorder  # noqa: E402
from memory import Memory  # noqa: E402


def view(turn, gold=1000, ttp=600, progress=None, gap=12000, victory="undecided", rooms=None, events=None):
    progress = gap - ttp * 10 if progress is None else progress
    return {"turn": turn, "level": {"number": 5, "campaign": "keeporig.cfg", "name": "Tickle"},
            "seat": {"player": 1, "victory_state": victory, "payday": {"gap": gap, "progress": progress, "turns_to_payday": ttp}},
            "own": {"gold": gold, "creatures": [{"id": 3}], "force": {"fighters": 4, "workers": 2, "fighter_score": 300},
                    "rooms": rooms or [{"kind": "DUNGEON_HEART", "health": 500, "max_health": 1000}, {"kind": "LAIR"}],
                    "events": events or [], "objective": "Destroy the Avatar"},
            "visible": {"force_by_owner": {"1": {"fighter_score": 300}, "2": {"fighter_score": 120}}}}


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="kfx-exp-")
        self.path = os.path.join(self.dir, "sub", "experience.sqlite")
        self.store = Experience(self.path)

    def tearDown(self):
        self.store.close()
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_an_attempt_is_opened_progressed_and_closed_once(self):
        m = Memory.new_game(view(10))
        self.store.open_branch(m, 1, "tester", [{"player": 2, "kind": "computer"}], turn=10)
        self.store.open_branch(m, 1, "someone else", None, turn=99)          # already recorded: unchanged
        row = self.store.branch(m.game_id, m.branch)
        self.assertEqual((row["agent"], row["start_turn"], row["level_key"], row["result"]), ("tester", 10, "keeporig:5", None))
        m.record_decision(20, ["start"], "r", [{"id": 1, "verb": "mark_dig"}], [{"verb": "build_room", "error": "NOT_ENOUGH_GOLD"}])
        m.record_decision(30, ["timer"], "r", [], [{"verb": "build_room", "error": "NOT_ENOUGH_GOLD"}])
        experience.observe(m, view(30))
        self.store.progress(m, 30)
        row = self.store.branch(m.game_id, m.branch)
        self.assertEqual((row["end_turn"], row["decisions"], row["orders_sent"], row["orders_refused"]), (30, 2, 1, 2))
        self.assertIn('"NOT_ENOUGH_GOLD": 2', row["refusals_by_error"])
        self.assertTrue(self.store.close_branch(m, "won", 40, {"x": 1}))
        self.assertFalse(self.store.close_branch(m, "lost", 50))               # a closed attempt stays as it ended
        row = self.store.branch(m.game_id, m.branch)
        self.assertEqual((row["result"], row["end_turn"]), ("won", 40))
        self.assertEqual(self.store.checkpoints(m.game_id, m.branch)[0]["gold"], 1000)
        self.assertNotIn("_ttp", self.store.checkpoints(m.game_id, m.branch)[0])

    def test_level_record_counts_finished_games_only(self):
        for result in ("lost", "suspended", "reloaded", "won", "quit"):
            m = Memory.new_game(view(0))
            self.store.open_branch(m, 1)
            self.store.close_branch(m, result, 1000)
        self.assertEqual([r["result"] for r in self.store.level_record("keeporig:5")], ["lost", "won", "quit"])

    def test_lessons_are_capped_versioned_and_can_be_rolled_back(self):
        self.assertEqual(self.store.lessons("general"), "")
        self.assertEqual(self.store.set_lessons("general", "dig gold early"), "")
        warn = self.store.set_lessons("level:keeporig:5", "x" * 5000)
        self.assertIn("cut to 1000", warn)
        self.assertEqual(len(self.store.lessons("level:keeporig:5")), 1000)
        self.store.set_lessons("general", "wrong lesson")
        hist = self.store.lessons_history("general")
        self.assertEqual([h["text"] for h in hist], ["dig gold early", "wrong lesson"])
        self.store.rollback_lessons("general", hist[0]["id"])
        self.assertEqual(self.store.lessons("general"), "dig gold early")
        self.assertEqual(len(self.store.lessons_history("general")), 3)     # the rollback is a version too
        self.store.set_lessons("general", "")
        self.assertEqual(self.store.lessons("general"), "")
        self.assertEqual(self.store.scopes(), ["level:keeporig:5"])

    def test_two_stores_on_one_file_and_a_reopen_keep_everything(self):
        other = Experience(self.path)
        m = Memory.new_game(view(0))
        other.open_branch(m, 2)
        self.store.set_lessons("general", "shared")
        self.assertIsNotNone(self.store.branch(m.game_id, m.branch))
        self.assertEqual(other.lessons("general"), "shared")
        other.close()
        again = Experience(self.path)                                       # no migration runs twice
        self.assertEqual(again.lessons("general"), "shared")
        again.close()

    def test_a_store_from_a_newer_bridge_is_refused_and_play_goes_on_without_one(self):
        db = sqlite3.connect(self.path)
        db.execute("UPDATE meta SET value='99' WHERE key='schema_version'")
        db.commit()
        db.close()
        with self.assertRaises(RuntimeError):
            Experience(self.path)
        store = experience.open_store(self.path)
        self.assertFalse(store.enabled)
        self.assertEqual(store.lessons("general"), "")       # every call is a harmless no-op

    def test_disabled_store(self):
        for off in (Experience(None), Experience("")):
            self.assertFalse(off.enabled)
            m = Memory.new_game(view(0))
            off.open_branch(m, 1)
            off.progress(m, 5)
            self.assertFalse(off.close_branch(m, "won", 5))
            self.assertEqual(off.set_lessons("general", "x"), "")


class ViewNumbersTests(unittest.TestCase):
    def test_checkpoint_and_final(self):
        cp = experience.checkpoint_from_view(view(100))
        self.assertEqual(cp, {"turn": 100, "gold": 1000, "creatures": 1, "fighters": 4, "workers": 2, "score": 300,
                              "rooms": {"DUNGEON_HEART": 1, "LAIR": 1}, "enemy_score": 120, "heart": 50})
        fin = experience.final_from_view(view(100, events=[{"kind": "heart_attacked", "text": "Your heart is under attack"}]))
        self.assertEqual((fin["objective"], fin["events"]), ("Destroy the Avatar", ["Your heart is under attack"]))

    def test_one_checkpoint_per_pay_day(self):
        m = Memory.new_game(view(0))
        experience.observe(m, view(0, ttp=600))
        experience.observe(m, view(200, ttp=400))       # same pay day: none
        experience.observe(m, view(500, ttp=100))
        experience.observe(m, view(700, ttp=1100))      # a pay day passed
        self.assertEqual([c["turn"] for c in m.checkpoints], [0, 700])

    def test_payday_turns(self):
        self.assertEqual(experience.payday_turns(view(0, gap=12000, progress=6000, ttp=600)), 1200)
        self.assertEqual(experience.payday_turns({}), 1200)


class RecorderTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="kfx-rec-")
        self.store = Experience(os.path.join(self.dir, "e.sqlite"))
        self.rec = Recorder(self.store, 1, "tester")

    def tearDown(self):
        self.store.close()
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_a_resume_continues_an_open_attempt(self):
        m = Memory.new_game(view(0))
        self.rec.start(m, view(0), resumed=False)
        self.rec.decision(m, view(500))
        branch = m.branch
        self.rec.start(m, view(520), resumed=True)          # the bridge restarted: same attempt
        self.assertEqual(m.branch, branch)

    def test_resuming_an_ended_attempt_is_a_new_one_from_it(self):
        m = Memory.new_game(view(0))
        self.rec.start(m, view(0), resumed=False)
        self.rec.ended(m, view(3000, victory="lost"), "lost")
        first = m.branch
        self.rec.start(m, view(2000), resumed=True)         # the save from before the loss, loaded later
        self.assertEqual(m.parent_branch, first)
        self.assertIsNone(self.store.branch(m.game_id, m.branch)["result"])

    def test_resuming_from_an_earlier_turn_closes_the_unfinished_attempt_as_reloaded(self):
        m = Memory.new_game(view(0))
        self.rec.start(m, view(0), resumed=False)
        self.rec.decision(m, view(5000))
        first = m.branch
        self.rec.start(m, view(1000), resumed=True)         # loaded while no bridge was watching
        self.assertEqual(self.store.branch(m.game_id, first)["result"], "reloaded")
        self.assertEqual(m.parent_branch, first)

    def test_reloaded(self):
        old = Memory.new_game(view(0))
        self.rec.start(old, view(0), resumed=False)
        new = Memory.from_blob(old.to_blob())
        new.start_branch()
        self.rec.reloaded(old, 4000, new, view(1500))
        self.assertEqual((self.store.branch(old.game_id, old.branch)["result"], self.store.branch(old.game_id, old.branch)["end_turn"]), ("reloaded", 4000))
        self.assertEqual(self.store.branch(new.game_id, new.branch)["start_turn"], 1500)

    def test_leaving_right_after_a_save_is_a_suspension_otherwise_a_quit(self):
        cases = [
            # (last save, turn left at, expected result, debrief)
            (9500, 10000, "suspended", False),     # saved within a pay day (1200 turns): it will be continued
            (2000, 10000, "quit", True),           # saved long ago: a real quit, after more than a pay day of play
            (None, 10000, "quit", True),
            (None, 800, "quit", False),            # quit before a pay day had passed: nothing to learn
        ]
        for last_save, turn, want, debrief in cases:
            m = Memory.new_game(view(0))
            self.rec.start(m, view(0), resumed=False)
            got = self.rec.left(m, view(turn, gap=12000, progress=6000, ttp=600), last_save)
            self.assertEqual(got, (want, debrief), (last_save, turn))
            self.assertEqual(self.store.branch(m.game_id, m.branch)["result"], want)

    def test_opponents_are_taken_from_the_view_once_it_names_them(self):
        m = Memory.new_game(view(0))
        self.rec.start(m, view(0), resumed=False)
        v = view(100)
        v["players"] = [{"player": 1, "kind": "external", "name": "me"}, {"player": 2, "kind": "human", "name": "Robin"}]
        self.rec.decision(m, v)
        self.assertEqual(self.rec.opponents, [{"player": 2, "kind": "human", "name": "Robin"}])
        self.assertIn("Robin", self.store.branch(m.game_id, m.branch)["opponents"])


class ReportTests(unittest.TestCase):
    def test_the_report_lists_shows_and_curates(self):
        import io
        import experience_report
        d = tempfile.mkdtemp(prefix="kfx-report-")
        path = os.path.join(d, "e.sqlite")
        store = Experience(path)
        m = Memory.new_game(view(0))
        store.open_branch(m, 1, "tester")
        experience.observe(m, view(0))
        store.close_branch(m, "won", 9000, {})
        store.set_summary(m.game_id, m.branch, "won by walling in")
        store.set_lessons("general", "v1")
        store.set_lessons("general", "v2")
        store.close()

        def run(*argv):
            out = io.StringIO()
            rc = experience_report.main(["--db", path] + list(argv), out=out)
            return rc, out.getvalue()
        rc, text = run("games")
        self.assertEqual(rc, 0)
        self.assertIn("won", text)
        self.assertIn("keeporig:5", text)
        self.assertIn("won by walling in", text)
        rc, text = run("game", m.game_id[:8])
        self.assertIn("turn 0: gold 1000", text)
        rc, text = run("history", "general")
        first_id = int(text.split("--- version ")[1].split(",")[0])
        run("rollback", "general", str(first_id))
        self.assertIn("v1", run("lessons", "general")[1])
        run("forget-lessons", "general")
        self.assertIn("(empty)", run("lessons", "general")[1])
        self.assertEqual(run("forget-game", m.game_id)[0], 0)
        self.assertIn("no finished games", run("games")[1])
        self.assertEqual(experience_report.main(["--db", os.path.join(d, "none.sqlite"), "games"], out=io.StringIO()), 1)
        shutil.rmtree(d, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
