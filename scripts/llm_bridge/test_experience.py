"""Tests for experience.py: game records across attempts, checkpoints, lessons with history, and the rules that decide
what a result means (docs/refactor/AI/omissions/09-persistent-memory.md section 6)."""
import json
import os
import re
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

    def test_lessons_are_versioned_and_a_rollback_brings_the_same_lessons_back(self):
        self.assertEqual(self.store.lessons("general"), "")
        self.assertEqual(self.store.set_lessons("general", "dig gold early"), "")
        first = self.store.items("general")[0]["id"]
        warn = self.store.set_lessons("level:keeporig:5", "x" * 5000)
        self.assertIn("cut to %d" % experience.ITEM_CAP, warn)
        self.assertEqual(len(self.store.items("level:keeporig:5")[0]["text"]), experience.ITEM_CAP)
        self.assertIn("1 lesson(s) of general retired: L%d" % first, self.store.set_lessons("general", "wrong lesson"))
        hist = self.store.lessons_history("general")
        self.assertEqual([h["text"] for h in hist], ["- dig gold early", "- wrong lesson"])
        self.store.rollback_lessons("general", hist[0]["id"])
        self.assertEqual(self.store.lessons("general"), "- dig gold early")
        self.assertEqual(self.store.items("general")[0]["id"], first)        # the same lesson, restored
        self.assertEqual(len(self.store.lessons_history("general")), 3)     # the rollback is a version too
        self.store.set_lessons("general", "")
        self.assertEqual(self.store.lessons("general"), "")
        self.assertEqual(self.store.scopes(), ["level:keeporig:5"])
        self.assertEqual({i["status"] for i in self.store.items("general", status=None)}, {"retired"})   # never deleted

    def test_two_stores_on_one_file_and_a_reopen_keep_everything(self):
        other = Experience(self.path)
        m = Memory.new_game(view(0))
        other.open_branch(m, 2)
        self.store.set_lessons("general", "shared")
        self.assertIsNotNone(self.store.branch(m.game_id, m.branch))
        self.assertEqual(other.lessons("general"), "- shared")
        other.close()
        again = Experience(self.path)                                       # no migration runs twice
        self.assertEqual(again.lessons("general"), "- shared")
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


V1_GENERAL = """- SETUP ON THE FIRST CALM DECISION:
  1. set_rally_points safe = a sealed pocket.
  2. set_combat_rules: heal, evacuate_diggers.
- ECONOMY: treasury beside every mining field.
- NEVER sell the library."""
V1_LEVEL = ("Tickle (keeporig:8) WON t29713. Heart x14-16 y72-74, portal x8-10. Rival heart x46-48 y39-41, hatchery x38-43 "
            "y38-42. Hero gate (47,19) sends a late knight party.")


def make_v1_store(path, lessons):
    """A store as a version-1 bridge left it: one text per scope."""
    db = sqlite3.connect(path)
    db.executescript("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);" + experience._SCHEMA[0] +
                     "INSERT INTO meta VALUES ('schema_version', '1');")
    for scope, text in lessons.items():
        db.execute("INSERT INTO lessons (scope, text, updated_at, game_id) VALUES (?,?,?,?)", (scope, text, 1000.0, "g1"))
        db.execute("INSERT INTO lessons_history (scope, text, updated_at, game_id) VALUES (?,?,?,?)", (scope, text, 1000.0, "g1"))
    db.execute("INSERT INTO games (game_id, branch, level_key, result, summary, agent, started_at, updated_at, ended_at) "
               "VALUES ('g1', 'b1', 'keeporig:8', 'won', 'won it', 'Claude', 1, 2, 3)")
    db.commit()
    db.close()


def words(text):
    """A text's words, bullets and line breaks aside: what a migration must keep, in order."""
    return " ".join(re.sub(r"^\s*(?:[-*\u2022]|\d+[.)])\s+", "", l) for l in text.splitlines()).split()


class LessonItemTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="kfx-items-")
        self.path = os.path.join(self.dir, "e.sqlite")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_a_version_1_store_is_split_into_lessons_and_nothing_is_lost(self):
        make_v1_store(self.path, {"general": V1_GENERAL, "level:keeporig:8": V1_LEVEL})
        store = Experience(self.path)
        gen = store.items("general")
        self.assertEqual([i["text"].split(":")[0].split(" ")[0] for i in gen], ["SETUP", "ECONOMY", "NEVER"])
        self.assertIn("1. set_rally_points", gen[0]["text"])                     # the sub-points stay with their bullet
        self.assertEqual([i["topic"] for i in gen], ["setup", "economy", "economy"])
        lvl = store.items("level:keeporig:8")
        self.assertGreater(len(lvl), 1)
        self.assertTrue(all(len(i["text"]) <= 300 for i in lvl))
        self.assertEqual({i["topic"] for i in lvl} & {"map"}, {"map"})
        for scope, text in (("general", V1_GENERAL), ("level:keeporig:8", V1_LEVEL)):
            self.assertEqual(words("\n".join(i["text"] for i in store.items(scope))), words(text), scope)
        self.assertEqual({e["event"] for e in store.item_events(gen[0]["id"])}, {"migrated"})
        self.assertEqual(len(store.lessons_history("general")), 1)                 # the old versions stay
        row = store.branch("g1", "b1")
        self.assertEqual(row["debrief_state"], "done")                             # it had its debrief
        store.close()
        again = Experience(self.path)                                              # migrated once only
        self.assertEqual(len(again.items("general")), 3)
        again.close()

    def test_the_real_store_copy_migrates_without_losing_a_word(self):
        src = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "core_files", "mcp", "experience.sqlite")
        if not os.path.exists(src):
            self.skipTest("no core_files/mcp/experience.sqlite here")
        dst = sqlite3.connect(self.path)
        with sqlite3.connect("file:%s?mode=ro" % os.path.abspath(src), uri=True) as s:
            s.backup(dst)                                                          # the copy and its WAL, consistently
        version = int(dst.execute("SELECT value FROM meta WHERE key='schema_version'").fetchone()[0])
        old = dict(dst.execute("SELECT scope, text FROM lessons").fetchall()) if version == 1 else {}
        dst.close()
        store = Experience(self.path)
        for scope, text in old.items():
            items = store.items(scope)
            self.assertTrue(items, scope)
            self.assertEqual(words("\n".join(i["text"] for i in items)), words(text), scope)
        self.assertEqual(int(store.db.execute("SELECT value FROM meta WHERE key='schema_version'").fetchone()[0]),
                         experience.SCHEMA_VERSION)
        store.close()

    def _game(self, store, opponents=None):
        m = Memory.new_game(view(0))
        store.open_branch(m, 1, "tester", opponents)
        return m

    def test_a_debrief_edits_by_id_and_keeps_what_it_does_not_mention(self):
        store = Experience(self.path)
        a, _ = store.add_item("general", "Dig gold early.")
        b, _ = store.add_item("general", "Wall in at once.")
        c, _ = store.add_item("general", "Lightning hits movers.")
        d, _ = store.add_item("level:keeporig:5", "The gold is north.")
        other, _ = store.add_item("level:keeporig:9", "Another level's lesson.")
        m = self._game(store, [{"player": 2, "kind": "human", "name": "Robin"}])
        opps = [{"player": 2, "kind": "human", "name": "Robin"}]
        out = experience.apply_debrief(store, m, {
            "summary": "won", "confirm": [a, "L%d" % d, other], "revise": [{"id": b, "text": "Wall in before the first raid."}],
            "retire": [{"id": c, "why": "it missed"}],
            "add": [{"scope": "level", "text": "Fairies hover over the north lava.", "topic": "heroes"},
                    {"scope": "opponent:human:Robin", "text": "Robin turtles."}, {"scope": "campaign:other", "text": "x"}]}, opps)
        self.assertEqual(out["confirmed"], [a, d])
        self.assertIn("confirm: %r is not one of this game's lessons" % other, out["warnings"][0])
        self.assertIn("add: no scope 'campaign:other'", out["warnings"][1])
        new_b = out["revised"][0][1]
        self.assertEqual(out["retired"], [c])
        self.assertEqual(out["stored"], ["summary", "general", "level:keeporig:5", "opponent:human:Robin"])
        self.assertEqual([i["text"] for i in store.items("general")], ["Dig gold early.", "Wall in before the first raid."])
        old_b = store.item(b)
        self.assertEqual((old_b["status"], old_b["superseded_by"]), ("retired", new_b))
        self.assertEqual(store.item(c)["why"], "it missed")
        self.assertEqual(store.item(a)["confirmed"], 1)
        self.assertEqual([i["topic"] for i in store.items("level:keeporig:5")], ["economy", "heroes"])   # guessed, then given
        self.assertEqual(store.items("opponent:human:Robin")[0]["topic"], "opponent")
        self.assertEqual(store.branch(m.game_id, m.branch)["debrief_state"], "done")
        # A revision carries the counts: confirmed once, reworded, still confirmed once.
        out = experience.apply_debrief(store, m, {"revise": [{"id": a, "text": "Dig the gold seams first."}]}, opps)
        self.assertEqual(store.item(out["revised"][0][1])["confirmed"], 1)
        store.close()

    def test_an_old_style_rewrite_keeps_matches_revises_close_ones_and_names_what_it_dropped(self):
        store = Experience(self.path)
        store.set_lessons("general", "- Dig gold early.\n- Wall in at once.\n- Never sell the library.")
        ids = [i["id"] for i in store.items("general")]
        store.confirm_item(ids[0])
        m = self._game(store)
        out = experience.apply_debrief(store, m, {"playbook": "- Dig gold early.\n- Wall in at once, before the raid.\n- Train warlocks."})
        self.assertEqual(out["revised"], [[ids[1], out["revised"][0][1]]])
        self.assertEqual([d["id"] for d in out["dropped"]], [ids[2]])
        self.assertIn("your rewrite left out were retired: L%d Never sell the library." % ids[2], out["warnings"][-1])
        texts = [i["text"] for i in store.items("general")]
        self.assertEqual(texts, ["Dig gold early.", "Wall in at once, before the raid.", "Train warlocks."])
        self.assertEqual(store.items("general")[0]["confirmed"], 1)                # kept: the same lesson
        store.close()

    def test_play_shows_the_best_ranked_lessons_within_a_budget_and_get_experience_all(self):
        store = Experience(self.path)
        ids = [store.add_item("level:keeporig:5", "lesson %d %s" % (n, "x" * 280))[0] for n in range(6)]
        store.confirm_item(ids[0])
        m = self._game(store)
        text = experience.render_experience(store, m, None)
        shown = [n for n in range(6) if "lesson %d " % n in text]
        self.assertIn(0, shown)                                                    # the confirmed one first
        self.assertLess(len(shown), 6)
        self.assertIn("(+%d more lesson(s) here, not shown: get_experience shows them all)" % (6 - len(shown)), text)
        self.assertIn("[L%d] lesson 0" % ids[0], text)
        every = experience.render_experience(store, m, None, every=True)
        self.assertTrue(all("lesson %d " % n in every for n in range(6)))
        deb = experience.debrief_text(store, m, "won", {}, None)
        self.assertIn("[L%d] (map, confirmed 1) lesson 0" % ids[0], deb)
        self.assertIn("[scope level; %d shown in play]" % len(shown), deb)
        store.close()


class AttemptTests(unittest.TestCase):
    """Phase B: every attempt gets its debrief, and restarts after a failure are linked."""

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="kfx-att-")
        self.store = Experience(os.path.join(self.dir, "e.sqlite"))

    def tearDown(self):
        self.store.close()
        shutil.rmtree(self.dir, ignore_errors=True)

    def _played(self, agent, turns, idle=3600, level=5):
        """An attempt this agent played `turns` turns of and left open, `idle` seconds ago."""
        v = view(0)
        v["level"]["number"] = level
        m = Memory.new_game(v)
        self.store.open_branch(m, 1, agent, turn=0)        # not Recorder.start: that would close the others
        m.plan = "hold the north"
        experience.observe(m, view(turns))
        self.store.progress(m, turns)
        self.store.db.execute("UPDATE games SET updated_at=updated_at-? WHERE game_id=?", (idle, m.game_id))
        return m

    def test_attempts_left_open_are_closed_as_abandoned_when_the_agent_plays_again(self):
        long_ago = self._played("Claude", 6000)
        short = self._played("Claude", 300)
        recent = self._played("Claude", 6000, idle=5)           # another session may still be playing it
        theirs = self._played("Someone", 6000)
        rec = Recorder(self.store, 1, "Claude")
        now = Memory.new_game(view(0))
        rec.start(now, view(0), resumed=False)
        state = lambda m: (lambda r: (r["result"], r["debrief_state"]))(self.store.branch(m.game_id, m.branch))
        self.assertEqual(state(long_ago), ("abandoned", None))
        self.assertEqual(state(short), ("abandoned", "not_worth"))
        self.assertEqual(state(recent), (None, None))
        self.assertEqual(state(theirs), (None, None))
        self.assertEqual(state(now), (None, None))
        self.assertEqual(len(rec.abandoned), 2)
        row = self.store.branch(long_ago.game_id, long_ago.branch)
        self.assertEqual(row["final_plan"], "hold the north")                       # the last plan, kept as it went
        self.assertEqual(json.loads(row["final"])["checkpoint"]["turn"], 6000)
        self.assertEqual(self.store.level_record("keeporig:5")[-1]["result"], "abandoned")

    def test_an_earlier_attempt_is_offered_a_few_times_debriefed_by_reference_or_declined(self):
        m = self._played("Claude", 6000)
        rec = Recorder(self.store, 1, "Claude")
        cur = Memory.new_game(view(0))
        rec.start(cur, view(0), resumed=False)
        pending = rec.pending_debriefs(cur)
        self.assertEqual([r["branch"] for r in pending], [m.branch])
        text = "\n".join(experience.render_pending(pending))
        ref = experience.attempt_ref(pending[0])
        self.assertIn(ref + ": keeporig:5 Tickle, abandoned at turn 6000", text)
        self.assertEqual(self.store.find_attempt(ref)["branch"], m.branch)
        self.assertEqual(self.store.find_attempt(m.branch)["branch"], m.branch)
        for _ in range(experience.DEBRIEF_MAX_OFFERS):
            self.store.note_offered(pending)
        self.assertEqual(rec.pending_debriefs(cur), [])                             # offered enough: let go
        self.store.db.execute("UPDATE games SET debrief_offers=0")
        old, opps, final = experience.attempt_memory(self.store, self.store.find_attempt(ref))
        deb = experience.debrief_text(self.store, old, "abandoned", final, opps, attempt=ref)
        self.assertIn("DEBRIEF of an earlier attempt, %s (abandoned at turn 6000; the bridge never saw how it ended)" % ref, deb)
        self.assertIn("YOUR FINAL PLAN: hold the north", deb)
        out = experience.apply_debrief(self.store, old, {"summary": "lost the north", "add": [{"scope": "level", "text": "Guard the north."}]}, opps)
        self.assertEqual(out["stored"], ["summary", "level:keeporig:5"])
        self.assertEqual(rec.pending_debriefs(cur), [])                             # debriefed: not offered again
        m2 = self._played("Claude", 6000, level=6)
        self.store.set_debrief_state(m2.game_id, m2.branch, None)
        rec.start(cur, view(0), resumed=True)
        self.assertEqual(len(rec.pending_debriefs(cur)), 1)
        self.store.set_debrief_state(m2.game_id, m2.branch, "declined")
        self.assertEqual(rec.pending_debriefs(cur), [])

    def test_a_restart_after_a_failure_follows_it_and_a_win_debrief_shows_the_failures(self):
        rec = Recorder(self.store, 1, "Claude")
        first = Memory.new_game(view(0))
        rec.start(first, view(0), resumed=False)
        rec.ended(first, view(4000, victory="lost"), "lost")
        self.store.set_summary(first.game_id, first.branch, "rushed too early")
        second = Memory.new_game(view(0))
        rec.start(second, view(0), resumed=False)
        row = self.store.branch(second.game_id, second.branch)
        self.assertEqual((row["previous_game"], row["previous_branch"]), (first.game_id, first.branch))
        rec.ended(second, view(9000, victory="won"), "won")
        third = Memory.new_game(view(0))                       # after a win: a new series, no link
        rec.start(third, view(0), resumed=False)
        self.assertIsNone(self.store.branch(third.game_id, third.branch)["previous_game"])
        deb = experience.debrief_text(self.store, second, "won", {}, None)
        self.assertIn("EARLIER ATTEMPTS AT THIS LEVEL, BEFORE THIS ONE (oldest first):\n  lost at turn 4000, 0 decisions: rushed too early", deb)
        self.assertIn("This game follows 1 earlier attempt(s) at it: lost t4000", experience.render_experience(self.store, second, None))

    def test_quit_and_suspend_by_the_agent(self):
        rec = Recorder(self.store, 1, "Claude")
        m = Memory.new_game(view(0))
        rec.start(m, view(0), resumed=False)
        self.assertTrue(rec.quit(m, view(5000)))
        self.assertEqual(self.store.branch(m.game_id, m.branch)["result"], "quit")
        m2 = Memory.new_game(view(0))
        rec.start(m2, view(0), resumed=False)
        self.assertFalse(rec.quit(m2, view(100)))
        self.assertEqual(self.store.branch(m2.game_id, m2.branch)["debrief_state"], "not_worth")
        m3 = Memory.new_game(view(0))
        rec.start(m3, view(0), resumed=False)
        rec.suspend(m3, view(5000))
        self.assertEqual(self.store.branch(m3.game_id, m3.branch)["result"], "suspended")

    def test_the_autonomous_loops_debrief_earlier_attempts_with_their_policy(self):
        m = self._played("Claude", 6000)

        class Policy:
            def __init__(self):
                self.texts = []

            def debrief(self, text, ctx):
                self.texts.append(text)
                return {"summary": "it was lost in the north", "add": [{"scope": "playbook", "text": "Scout first."}]}
        pol = Policy()
        rec = Recorder(self.store, 1, "Claude")
        cur = Memory.new_game(view(0))
        rec.start(cur, view(0), resumed=False)
        done = experience.run_earlier_debriefs(self.store, rec, cur, pol, {})
        self.assertEqual([ref for ref, _ in done], ["%s/%s" % (m.game_id[:8], m.branch)])
        self.assertIn("DEBRIEF of an earlier attempt", pol.texts[0])
        self.assertEqual(self.store.lessons("general"), "- Scout first.")
        self.assertEqual(self.store.branch(m.game_id, m.branch)["summary"], "it was lost in the north")
        self.assertEqual(experience.run_earlier_debriefs(self.store, rec, cur, pol, {}), [])


class RecallTests(unittest.TestCase):
    """Phase C: lessons come back when their situation does."""

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="kfx-recall-")
        self.store = Experience(os.path.join(self.dir, "e.sqlite"))
        self.mem = Memory.new_game(view(0))
        self.store.open_branch(self.mem, 1, "Claude")
        add = lambda scope, text, topic: self.store.add_item(scope, text, topic)[0]
        self.combat = [add("general", "Combat lesson %d." % n, "combat") for n in range(6)]
        self.defence = add("level:keeporig:5", "Wall the north gap.", "defence")
        self.economy = add("general", "Mine seams first.", "economy")
        self.setup = add("general", "Set the rally point on the first calm decision.", "setup")

    def tearDown(self):
        self.store.close()
        shutil.rmtree(self.dir, ignore_errors=True)

    def recall(self, reasons, n, flash=False):
        return experience.render_recall(self.store, self.mem, None, reasons, n, flash=flash)

    def test_reasons_map_to_topics(self):
        self.assertEqual(experience.topics_for(["payday", "heart_attacked", "enemy_fight", "new_kind_TROLL", "chat"]),
                         ["economy", "defence", "combat", "creatures"])

    def test_a_topic_is_recalled_then_rests_then_rotates(self):
        text = self.recall(["enemy_fight"], 5)
        self.assertIn("RECALL (combat:", text)
        self.assertIn("RECALL (defence:", text)
        self.assertIn("[L%d] Wall the north gap." % self.defence, text)      # the level's lesson ranks first
        first = {i for i in self.combat if "[L%d]" % i in text}
        self.assertEqual(len(first), experience.RECALL_ITEMS)
        self.assertEqual(self.recall(["enemy_fight"], 5 + experience.RECALL_GAP - 1), "")     # resting
        again = self.recall(["enemy_fight"], 5 + experience.RECALL_GAP)
        unseen = set(self.combat) - first
        self.assertTrue(all("[L%d]" % i in again for i in unseen))           # the ones not shown yet come first
        self.assertNotIn("Mine seams", again)
        self.assertEqual(self.mem.recall["shown"][str(self.defence)][0], 2)

    def test_confirmed_and_doubted_lessons_rank_up_and_down(self):
        self.store.confirm_item(self.combat[5])
        self.store.doubt_item(self.combat[0], "missed")
        text = self.recall(["enemy_fight"], 1)
        combat_part = text.split("RECALL (defence:")[0]
        self.assertTrue(combat_part.splitlines()[1].startswith("  [L%d]" % self.combat[5]))   # confirmed: first
        self.assertNotIn("[L%d]" % self.combat[0], combat_part)

    def test_a_flash_decision_gets_two_combat_lines(self):
        text = self.recall(["enemy_fight"], 3, flash=True)
        self.assertEqual(text.count("[L"), experience.FLASH_RECALL_ITEMS)
        self.assertIn("RECALL (combat:", text)
        self.assertEqual(self.recall(["payday"], 4, flash=True), "")

    def test_the_full_experience_counts_as_recall_and_a_refresher_comes_every_n_decisions(self):
        full = experience.render_experience(self.store, self.mem, None, decision_n=1)
        self.assertIn("[L%d]" % self.setup, full)
        self.assertEqual(self.recall(["enemy_fight"], 2), "")                 # just shown in full
        self.assertNotIn("REFRESHER", self.recall(["chat"], 1 + experience.REFRESH_EVERY - 1))
        text = self.recall(["chat"], 1 + experience.REFRESH_EVERY)
        self.assertIn("REFRESHER", text)
        self.assertIn("[L%d] Set the rally point" % self.setup, text)
        self.assertNotIn("REFRESHER", self.recall(["chat"], 2 + experience.REFRESH_EVERY))

    def test_get_experience_by_topic(self):
        text = experience.render_experience(self.store, self.mem, None, every=True, topic="economy")
        self.assertIn("Mine seams first.", text)
        self.assertNotIn("Combat lesson", text)

    def test_the_debrief_says_how_often_each_lesson_was_shown(self):
        self.recall(["enemy_fight"], 5)
        deb = experience.debrief_text(self.store, self.mem, "won", {}, None)
        self.assertIn("[L%d] (shown 1x, defence)" % self.defence, deb)


class PlayLessonTests(unittest.TestCase):
    """Phase D: what the agent (or a human's tip) notes in play is kept provisional until a debrief judges it."""

    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="kfx-play-")
        self.store = Experience(os.path.join(self.dir, "e.sqlite"))
        self.mem = Memory.new_game(view(0))
        self.opps = [{"player": 2, "kind": "human", "name": "Robin"}]
        self.store.open_branch(self.mem, 1, "Claude", self.opps)

    def tearDown(self):
        self.store.close()
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_a_lesson_noted_in_play_is_new_until_a_debrief_keeps_it(self):
        out = experience.apply_play_lessons(self.store, self.mem, self.opps, {"remember": [
            {"text": "Fairies hover over the lava at x3-8 y56-60."}, {"text": "Heal before the second wave.", "topic": "combat"},
            {"text": "Robin rushes at the first pay day.", "scope": "opponent:human:Robin"}, {"text": "?", "scope": "nowhere"}]})
        lvl, gen, opp = out["remembered"]
        self.assertIn("remember: no scope 'nowhere'", out["warnings"][0])
        self.assertEqual(self.store.item(lvl)["scope"], "level:keeporig:5")            # it names places
        self.assertEqual((self.store.item(gen)["scope"], self.store.item(gen)["status"]), ("general", "provisional"))
        self.assertEqual(self.store.item(opp)["scope"], "opponent:human:Robin")
        again = experience.add_play_lesson(self.store, self.mem, self.opps, "heal before  the second wave.", "playbook")
        self.assertEqual((again["id"], again["note"]), (gen, "already known"))
        self.assertEqual(self.store.lessons("general"), "")                             # not kept yet
        exp = experience.render_experience(self.store, self.mem, self.opps)
        self.assertIn("[L%d] (new) Heal before the second wave." % gen, exp)
        recall = experience.render_recall(self.store, self.mem, self.opps, ["enemy_fight"], 2)
        self.assertIn("[L%d] (new) Heal before" % gen, recall)
        deb = experience.debrief_text(self.store, self.mem, "won", {}, self.opps)
        self.assertIn("LESSONS NOTED DURING PLAY, NOT KEPT YET", deb)
        self.assertIn("Heal before the second wave. (scope playbook)", deb)
        res = experience.apply_debrief(self.store, self.mem, {"confirm": [gen], "retire": [{"id": opp, "why": "he did not"}]}, self.opps)
        self.assertEqual((res["confirmed"], res["retired"]), ([gen], [opp]))
        self.assertEqual(self.store.item(gen)["status"], "active")
        self.assertEqual(self.store.item(lvl)["status"], "provisional")                # unmentioned: waits for the next
        self.assertEqual(self.store.lessons("general"), "- Heal before the second wave.")
        # A rewrite of a scope's text leaves the provisional lessons alone.
        self.store.set_lessons("level:keeporig:5", "- Something else.")
        self.assertEqual(self.store.item(lvl)["status"], "provisional")

    def test_confirm_and_doubt_in_play(self):
        a, _ = self.store.add_item("general", "Wall in early.")
        b, _ = self.store.add_item("level:keeporig:9", "Not this level's.")
        out = experience.apply_play_lessons(self.store, self.mem, self.opps,
                                            {"confirm": [a, b], "doubt": [{"id": a, "why": "breached anyway"}]})
        self.assertEqual((out["confirmed"], out["doubted"]), ([a], [a]))
        self.assertIn("confirm: %d is not one of this game's lessons" % b, out["warnings"][0])
        it = self.store.item(a)
        self.assertEqual((it["confirmed"], it["contradicted"]), (1, 1))
        self.assertEqual([e["event"] for e in self.store.item_events(a)], ["added", "confirmed", "doubted"])

    def test_a_humans_tip_in_chat_becomes_a_new_lesson_once(self):
        v = view(500)
        v["players"] = [{"player": 1, "kind": "external", "name": "me"}, {"player": 2, "kind": "human", "name": "Robin"},
                        {"player": 3, "kind": "computer"}]
        v["chat"] = [{"player": 2, "text": "tip: lure the fairies onto your floor", "turn": 400},
                     {"player": 2, "text": "nice dungeon", "turn": 410},
                     {"player": 3, "text": "tip: computers give no tips", "turn": 420},
                     {"player": 1, "text": "tip: nor do I", "turn": 430}]
        rec = Recorder(self.store, 1, "Claude")
        rec.start(self.mem, v, resumed=True)
        rec.decision(self.mem, v)
        self.assertEqual([(t[1], t[2]) for t in rec.tips], [("lure the fairies onto your floor", "Robin")])
        item = self.store.item(rec.tips[0][0])
        self.assertEqual((item["status"], item["source"], item["scope"]), ("provisional", "user", "general"))
        exp, _ = rec.lessons_text(self.mem, ["chat"], full=False)
        self.assertIn("TIP NOTED: Robin's tip is kept as lesson L%d" % item["id"], exp)
        rec.decision(self.mem, v)                                                       # the same chat again: no new tip
        self.assertEqual(rec.tips, [])
        deb = experience.debrief_text(self.store, self.mem, "won", {}, self.opps)
        self.assertIn("[a tip from a human player]", deb)

    def test_the_recorder_gives_policies_the_recall_or_the_flash_recall(self):
        self.store.add_item("general", "Lift the wounded out.", "combat")
        rec = Recorder(self.store, 1, "Claude")
        rec.start(self.mem, view(0), resumed=True)
        ctx = {"reasons": ["start"]}
        rec.decision(self.mem, view(0), ctx)
        self.assertIn("EXPERIENCE (your own lessons", ctx["experience"])
        self.mem.count = 30
        ctx = {"reasons": ["enemy_fight"], "severity": 1}
        rec.decision(self.mem, view(100), ctx)
        self.assertIsNone(ctx["experience"])
        self.assertIn("RECALL (combat:", ctx["flash_recall"])
        self.mem.count = 60
        ctx = {"reasons": ["payday"], "severity": 3}
        rec.decision(self.mem, view(200), ctx)
        self.assertIn("EXPERIENCE: 0 earlier game(s)", ctx["experience"])
        self.assertIsNone(ctx["flash_recall"])


class ViewNumbersTests(unittest.TestCase):
    def test_checkpoint_and_final(self):
        cp = experience.checkpoint_from_view(view(100))
        self.assertEqual(cp, {"turn": 100, "gold": 1000, "creatures": 1, "fighters": 4, "workers": 2, "score": 300,
                              "rooms": {"DUNGEON_HEART": 1, "LAIR": 1}, "enemy_score": 120, "heart": 50})
        fin = experience.final_from_view(view(100, events=[{"kind": "heart_attacked", "text": "Your heart is under attack"}]))
        self.assertEqual((fin["objective"], fin["events"]), ("Destroy the Avatar", ["Your heart is under attack"]))

    def test_checkpoint_keeps_the_pay_day_report_numbers(self):
        v = view(100)
        v["own"].update(territory=312, creatures_active=14, creature_limit=20,
                        rooms=[{"kind": "LAIR", "slabs": 9, "capacity": {"used": 7, "total": 9}},
                               {"kind": "LAIR", "slabs": 4, "capacity": {"used": 4, "total": 4}},
                               {"kind": "TREASURE", "slabs": 9}])
        cp = experience.checkpoint_from_view(v)
        self.assertEqual((cp["area"], cp["room_slabs"], cp["lair"], cp["limit"]),
                         (312, {"LAIR": 13, "TREASURE": 9}, [11, 13], [14, 20]))

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

    def test_the_report_curates_single_lessons(self):
        import io
        import experience_report
        d = tempfile.mkdtemp(prefix="kfx-report-")
        self.addCleanup(shutil.rmtree, d, True)
        path = os.path.join(d, "e.sqlite")
        Experience(path).close()

        def run(*argv):
            out = io.StringIO()
            rc = experience_report.main(["--db", path] + list(argv), out=out)
            return rc, out.getvalue()
        self.assertEqual(run("add", "general", "Dig gold early.", "--topic", "economy"), (0, "added L1\n"))
        run("add", "general", "Mine the seams first.")
        run("add", "general", "Wrong idea.")
        self.assertEqual(run("confirm", "1")[0], 0)
        self.assertIn("L1 revised as L4", run("revise", "1", "Dig the gold seams early.")[1])
        self.assertIn("merged L2, L4 into L5", run("merge", "2", "4", "Mine the gold seams first, early.")[1])
        self.assertEqual(run("retire", "3", "it was wrong")[0], 0)
        self.assertEqual(run("retire", "3")[0], 1)                         # already retired: nothing changed
        self.assertEqual(run("move", "5", "--topic", "nope")[0], 1)
        run("move", "5", "--scope", "campaign:keeporig")
        rc, text = run("items", "--all")
        self.assertIn("L5     campaign:keeporig      economy   +1/-0", text)
        self.assertIn("[retired: it was wrong]", text)
        self.assertIn("[retired: merged into L5 -> L5]", text)
        self.assertIn("[L5] Mine the gold seams first, early.", run("lessons", "campaign:keeporig")[1])
        rc, text = run("history", "L1")
        self.assertIn("added (report)", text)
        self.assertIn("confirmed", text)
        self.assertIn("revised (by L4)", text)


if __name__ == "__main__":
    unittest.main()
