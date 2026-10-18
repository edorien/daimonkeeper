"""What the agent learns across games: a record of every game it played, and lessons it wrote itself when one ended
(docs/refactor/AI/omissions/09-persistent-memory.md section 6).

A game's working memory (memory.py) lives in the game and its saves; this outlives both. One sqlite file, next to the
bridge itself (default_path(): e.g. <game>/mcp/experience.sqlite), standard library only, safe to share between the
processes of an agent-vs-agent run.

- **games**: one row per attempt at a game (game_id + branch; a load starts a new branch whose parent is the attempt the
  save was made in). Written as the game goes (turn reached, counts), closed with a result: won / lost / quit / suspended
  (left right after a save, so it will be continued) / reloaded (a load replaced it). Mechanical: nothing here needs the
  model.
- **checkpoints**: the dungeon's numbers once per pay day, per attempt.
- **lesson items** (schema 2; docs/plans/llm-bridge-memory.md, Phase A): one row per lesson, per scope -- `general` (the
  playbook), `campaign:<name>`, `level:<campaign>:<n>`, `opponent:<identity>` -- each with a topic, where it came from, and
  how often a debrief confirmed it. A debrief edits them by id (add / confirm / revise / retire); one it does not mention
  stays as it is, so nothing is lost by omission. A lesson is never deleted: a revised or retired one is kept, marked,
  with why (lesson_events has every change). The text of a whole scope, as it reads now, still goes into lessons_history
  after every change. Version 1 kept one text per scope (`lessons`); the migration split each into items.
- Lessons come back when they apply (Phase C): a decision whose reasons map to a topic (a fight -> combat, defence; a
  pay day -> economy; ...) recalls that topic's best lessons (RECALL), at most once every RECALL_GAP decisions per topic;
  a flash decision gets two combat lines; every REFRESH_EVERY decisions a REFRESHER repeats the setup lessons and the
  best ones -- what a client's compacted context lost. What was shown, and how often, is kept in the attempt's memory
  (memory.recall) for the ranking (lessons shown less come first among equals) and the debrief.
- Lessons learned in play (Phase D): the agent notes one (remember_lesson, or `lessons` on submit_orders) and it is kept
  `provisional`, recalled marked (new), until a debrief confirms (keeps), revises or retires it; one no debrief mentions
  stays provisional for the next. A human's chat line "tip: ..." becomes a provisional lesson too (source user). The
  agent can confirm or doubt a lesson in play: the counts the ranking uses.
- An attempt nobody closed (the bridge stopped, the level restarted from the menu) is closed as `abandoned` the next time
  the same agent connects, and offered for a debrief like a lost one (Phase B). A new game on a level whose last attempt by
  the same agent, a day ago or less, was not won is linked to it (`previous_game`), so a win's debrief sees the failures
  before it.
"""
import difflib
import json
import os
import re
import sqlite3
import sys
import time

import places as places_mod
from gamelink import URGENT_SEVERITY
from latency import LatencyChain

SCHEMA_VERSION = 2
# How much of each scope a decision shows (its best-ranked lessons first); a scope may hold more (get_experience shows all).
SCOPE_CAPS = {"general": 3000, "campaign": 1500, "level": 1000, "opponent": 800}
SUMMARY_CAP = 600
ITEM_CAP = 500             # one lesson's text; a longer one is cut (with a warning)
TOPICS = ("setup", "economy", "defence", "combat", "attack", "heroes", "creatures", "rooms", "research", "map", "tools",
          "opponent", "general")
LINK_WINDOW = 86400        # seconds: a new game on a level links to the same agent's attempt there this recent (not won)
ABANDON_IDLE = 600         # seconds without progress before another session's open attempt counts as abandoned
DEBRIEF_MIN_TURNS = 1200   # an attempt shorter than about a pay day is not worth a debrief
DEBRIEF_MAX_OFFERS = 3     # an earlier attempt is offered for a debrief this many times, then let go
DEBRIEF_MAX_AGE = 30 * 86400
LIVE = ("active", "provisional")   # the lessons that are shown and recalled
RECALL_GAP = 20            # decisions before a topic is recalled again
RECALL_ITEMS, RECALL_BUDGET = 4, 600
FLASH_RECALL_ITEMS, FLASH_RECALL_BUDGET = 2, 300
REFRESH_EVERY, REFRESH_ITEMS, REFRESH_BUDGET = 40, 6, 1500
# The topics a decision's reasons call for, most pressing first (the rest of the reasons recall nothing).
REASON_TOPICS = {
    "heart_attacked": ("defence", "combat"), "enemy_fight": ("combat", "defence"), "room_under_attack": ("defence", "combat"),
    "breach": ("defence", "combat"), "intruder": ("defence", "combat"), "alarm": ("defence", "combat"),
    "room_captured": ("defence",), "room_destroyed": ("defence",), "ground_lost": ("defence",), "room_lost": ("defence",),
    "payday": ("economy",), "treasure_room_full": ("economy",), "need_treasure_room": ("economy",),
    "creature_hungry": ("rooms",), "no_more_living_set": ("rooms",), "work_room_unreachable": ("rooms",),
    "creature_annoyed": ("creatures",), "new_room_research": ("research",), "new_spell_research": ("research",),
    "sight_open": ("attack",), "new_prisoner": ("rooms",), "assist_budget": ("combat",), "assist_reserve": ("combat",),
}
TIP_PREFIX = "tip:"

_SCHEMA = [
    # version 1
    """
    CREATE TABLE games (
        game_id TEXT NOT NULL, branch TEXT NOT NULL, parent_branch TEXT,
        level_key TEXT, level_name TEXT, campaign TEXT, level_number INTEGER,
        player INTEGER, opponents TEXT, agent TEXT,
        started_at REAL, updated_at REAL, ended_at REAL, start_turn INTEGER, end_turn INTEGER,
        result TEXT, decisions INTEGER DEFAULT 0, orders_sent INTEGER DEFAULT 0, orders_refused INTEGER DEFAULT 0,
        refusals_by_error TEXT, final TEXT, final_plan TEXT, final_notes TEXT, summary TEXT,
        PRIMARY KEY (game_id, branch));
    CREATE INDEX games_level ON games (level_key);
    CREATE TABLE checkpoints (game_id TEXT NOT NULL, branch TEXT NOT NULL, turn INTEGER NOT NULL, stats TEXT,
        PRIMARY KEY (game_id, branch, turn));
    CREATE TABLE lessons (scope TEXT PRIMARY KEY, text TEXT NOT NULL, updated_at REAL, game_id TEXT);
    CREATE TABLE lessons_history (id INTEGER PRIMARY KEY AUTOINCREMENT, scope TEXT NOT NULL, text TEXT NOT NULL,
        updated_at REAL, game_id TEXT);
    """,
    # version 2: lessons as items (the old texts are split into them by _items_from_v1 in the same transaction), and
    # what Phase B needs on the attempts.
    """
    CREATE TABLE lesson_items (id INTEGER PRIMARY KEY AUTOINCREMENT, scope TEXT NOT NULL, topic TEXT NOT NULL DEFAULT 'general',
        text TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'active', source TEXT, game_id TEXT, branch TEXT,
        created_at REAL, updated_at REAL, confirmed INTEGER NOT NULL DEFAULT 0, contradicted INTEGER NOT NULL DEFAULT 0,
        last_game TEXT, supersedes INTEGER, superseded_by INTEGER, why TEXT);
    CREATE INDEX lesson_items_scope ON lesson_items (scope, status);
    CREATE TABLE lesson_events (id INTEGER PRIMARY KEY AUTOINCREMENT, item_id INTEGER NOT NULL, event TEXT NOT NULL,
        game_id TEXT, branch TEXT, at REAL, note TEXT);
    ALTER TABLE games ADD COLUMN previous_game TEXT;
    ALTER TABLE games ADD COLUMN previous_branch TEXT;
    ALTER TABLE games ADD COLUMN debrief_state TEXT;
    ALTER TABLE games ADD COLUMN debrief_offers INTEGER NOT NULL DEFAULT 0;
    UPDATE games SET debrief_state='done' WHERE summary IS NOT NULL AND summary != '';
    """,
]

_BULLET = re.compile(r"^(?:[-*\u2022]|\d+[.)])\s+")
_SENTENCE_END = re.compile(r"(?<=[.!?])\s+(?=[A-Z(\[])")
_COORD = re.compile(r"\b[xy]\d+|\(\d+,\s*\d+\)")
_TOPIC_WORDS = {
    "setup": ("setup", "first calm", "at game start", "at the start", "first decision"),
    "economy": ("gold", "treasur", "mine", "mining", "gem", "seam", "wage", "pay day", "payday", "sell", "cash", "income"),
    "defence": ("fortif", "defend", "defence", "defense", "wall", "breach", "door", "trap", "guard", "pocket", "protect"),
    "combat": ("fight", "heal", "wounded", "reinforce", "piecemeal", "battle", "army", "call to arms", "cta", "lightning",
               "lift", "massed"),
    "attack": ("attack", "tunnel", "assault", "his heart", "enemy heart", "rival heart", "push", "offence", "offense"),
    "heroes": ("hero", "knight", "avatar", "party", "barbarian", "fairies", "fairy"),
    "creatures": ("warlock", "bile", "mistress", "demon", "troll", "fly", "flies", "beetle", "orc", "imp", "creature", "train"),
    "rooms": ("library", "workshop", "lair", "hatchery", "temple", "prison", "torture", "graveyard", "room", "bridge"),
    "research": ("research", "spell", "book"),
    "tools": ("subtile", "slab", "pos", "verb", "priority", "flash", "alert", "queue", "trigger", "rally", "bank", "on_urgent",
              "cell.py", "look", "submit", "set_", "tool", "order"),
}


def split_lessons(text):
    """A scope's text -> its lessons, one per top-level bullet ("- ", "* ", "1. " at the start of a line) with the lines
    under it. Text without bullets (prose) is split into its sentences, one a lesson (a very short one joins the next).
    Nothing is cut."""
    items, cur, prose = [], None, []
    for raw in (text or "").splitlines():
        line = raw.rstrip()
        if not line.strip():
            continue
        if not raw[:1].isspace() and _BULLET.match(line):
            if prose:
                items += _split_prose(" ".join(prose))
                prose = []
            if cur is not None:
                items.append(cur)
            cur = _BULLET.sub("", line, count=1).strip()
        elif cur is not None:
            cur += "\n" + line.strip()
        else:
            prose.append(line.strip())
    if cur is not None:
        items.append(cur)
    if prose:
        items += _split_prose(" ".join(prose))
    return [i for i in items if i]


def _split_prose(text, short=60, pack=300):
    out = []
    for sentence in _SENTENCE_END.split(text.strip()):
        if out and len(out[-1]) < short and len(out[-1]) + 1 + len(sentence) <= pack:
            out[-1] += " " + sentence
        else:
            out.append(sentence)
    return out


def guess_topic(text, scope="general"):
    """The topic a lesson most likely belongs to, from its words (experience_report.py move corrects a bad guess)."""
    kind = scope_kind(scope)
    if kind == "opponent":
        return "opponent"
    low = text.lower()
    scores = {t: sum(low.count(w) for w in words) for t, words in _TOPIC_WORDS.items()}
    if kind == "level":
        scores["map"] = scores.get("map", 0) + 2 * len(_COORD.findall(text))
    if any(w in low for w in _TOPIC_WORDS["setup"]):
        return "setup"
    best = max(scores.items(), key=lambda kv: (kv[1], -list(scores).index(kv[0])))
    if best[1] > 0:
        return best[0]
    return "map" if kind == "level" else "general"


def _norm(text):
    return " ".join(_BULLET.sub("", (text or "").strip(), count=1).split()).lower()


def _items_from_v1(db):
    """Migration 1 -> 2: each scope's text becomes its lessons (source 'migrated'), in their order."""
    now = time.time()
    for row in db.execute("SELECT scope, text, updated_at, game_id FROM lessons ORDER BY scope").fetchall():
        for text in split_lessons(row["text"]):
            cur = db.execute(
                "INSERT INTO lesson_items (scope, topic, text, source, game_id, created_at, updated_at) VALUES (?,?,?,?,?,?,?)",
                (row["scope"], guess_topic(text, row["scope"]), text, "migrated", row["game_id"], row["updated_at"] or now,
                 row["updated_at"] or now))
            db.execute("INSERT INTO lesson_events (item_id, event, game_id, at, note) VALUES (?,?,?,?,?)",
                       (cur.lastrowid, "migrated", row["game_id"], now, "from the version-1 text of " + row["scope"]))


_MIGRATIONS_PY = {2: _items_from_v1}


def default_path():
    """experience.sqlite in the bridge's own folder (the game's mcp/ folder once installed), where a person can see it --
    not a hidden per-user directory. $DAIMONKEEPER_EXPERIENCE (or the older $KEEPERFX_EXPERIENCE) overrides it (the tests
    use that to stay out of the tree)."""
    return os.environ.get("DAIMONKEEPER_EXPERIENCE") or os.environ.get("KEEPERFX_EXPERIENCE") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "experience.sqlite")


def scope_kind(scope):
    return scope.split(":", 1)[0]


def cap_for(scope):
    return SCOPE_CAPS.get(scope_kind(scope), 800)


class Experience:
    """The store; `Experience(None)` or `Experience("")` is a disabled one whose methods do nothing (a clean benchmark)."""

    def __init__(self, path):
        self.path = path or None
        self.db = None
        if not self.path:
            return
        d = os.path.dirname(os.path.abspath(self.path))
        os.makedirs(d, exist_ok=True)
        # Any thread may use it (an HTTP server runs each tool call on its request's thread); the session's lock keeps
        # them one at a time.
        self.db = sqlite3.connect(self.path, timeout=10.0, isolation_level=None, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA busy_timeout=10000")
        self._migrate()

    @property
    def enabled(self):
        return self.db is not None

    def close(self):
        if self.db is not None:
            self.db.close()
            self.db = None

    def _migrate(self):
        self.db.execute("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)")
        row = self.db.execute("SELECT value FROM meta WHERE key='schema_version'").fetchone()
        have = int(row["value"]) if row else 0
        if have > SCHEMA_VERSION:
            raise RuntimeError("%s was written by a newer bridge (schema %d, this one knows %d)" % (self.path, have, SCHEMA_VERSION))
        for v in range(have, SCHEMA_VERSION):
            # One transaction per version: a migration either lands whole or not at all. (executescript commits any
            # open transaction first, so the BEGIN has to be inside it; the transaction stays open for the Python step.)
            self.db.executescript("BEGIN IMMEDIATE;" + _SCHEMA[v])
            try:
                step = _MIGRATIONS_PY.get(v + 1)
                if step is not None:
                    step(self.db)
                self.db.execute("INSERT OR REPLACE INTO meta (key, value) VALUES ('schema_version', ?)", (str(v + 1),))
                self.db.execute("COMMIT")
            except Exception:
                self.db.execute("ROLLBACK")
                raise

    # ---- game records ----------------------------------------------------------------------------------------------------
    def branch(self, game_id, branch):
        """The attempt's row as a dict, or None when this store has never seen it."""
        if self.db is None:
            return None
        row = self.db.execute("SELECT * FROM games WHERE game_id=? AND branch=?", (game_id, branch)).fetchone()
        return dict(row) if row else None

    def open_branch(self, mem, player, agent=None, opponents=None, turn=None):
        """Records that this attempt is being played (nothing if it is already recorded). A new game (not a load of an
        earlier one) on a level where this agent's latest attempt, a day ago or less, was not won follows that attempt
        (previous_game / previous_branch): the level restarted after a failure."""
        if self.db is None:
            return
        lv = mem.level or {}
        now = time.time()
        prev = (None, None)
        if mem.parent_branch is None and lv.get("key") and self.branch(mem.game_id, mem.branch) is None:
            row = self.db.execute(
                "SELECT game_id, branch, result FROM games WHERE level_key=? AND agent IS ? AND game_id != ? "
                "ORDER BY updated_at DESC LIMIT 1", (lv["key"], agent, mem.game_id)).fetchone()
            if row is not None and row["result"] != "won":
                recent = self.db.execute("SELECT 1 FROM games WHERE game_id=? AND branch=? AND updated_at > ?",
                                         (row["game_id"], row["branch"], now - LINK_WINDOW)).fetchone()
                if recent:
                    prev = (row["game_id"], row["branch"])
        self.db.execute(
            "INSERT OR IGNORE INTO games (game_id, branch, parent_branch, level_key, level_name, campaign, level_number, player,"
            " opponents, agent, started_at, updated_at, start_turn, end_turn, previous_game, previous_branch)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (mem.game_id, mem.branch, mem.parent_branch, lv.get("key"), lv.get("name"), lv.get("campaign"), lv.get("number"),
             player, json.dumps(opponents or []), agent, now, now, turn if turn is not None else mem.start_turn,
             turn if turn is not None else mem.start_turn, prev[0], prev[1]))

    def progress(self, mem, turn, opponents=None):
        """The attempt has reached `turn`: its counts and latest checkpoints, so an attempt the bridge never got to close
        (killed, or the game quit under it) still says how far it went."""
        if self.db is None:
            return
        st = mem.stats
        # The plan and notes as they stand: an attempt that is never closed (abandoned) still has its last ones.
        args = [time.time(), turn, st.get("decisions", 0), st.get("sent", 0), st.get("refused", 0), json.dumps(st.get("refusals", {})),
                mem.plan, _notes_of(mem)]
        extra = ""
        if opponents is not None:
            extra = ", opponents=?"
            args.append(json.dumps(opponents))
        self.db.execute("UPDATE games SET updated_at=?, end_turn=?, decisions=?, orders_sent=?, orders_refused=?, refusals_by_error=?,"
                        " final_plan=?, final_notes=?" + extra + " WHERE game_id=? AND branch=? AND result IS NULL",
                        args + [mem.game_id, mem.branch])
        self._write_checkpoints(mem)

    def _write_checkpoints(self, mem):
        rows = [(mem.game_id, mem.branch, cp["turn"], json.dumps({k: v for k, v in cp.items() if not k.startswith("_")}))
                for cp in mem.checkpoints if cp.get("turn") is not None]
        if rows:
            self.db.executemany("INSERT OR REPLACE INTO checkpoints (game_id, branch, turn, stats) VALUES (?,?,?,?)", rows)

    def close_branch(self, mem, result, turn, final=None):
        """Closes the attempt with its result. Returns False if it was already closed (then nothing changes)."""
        if self.db is None:
            return False
        self.progress(mem, turn)
        cur = self.db.execute(
            "UPDATE games SET result=?, ended_at=?, end_turn=?, final=?, final_plan=?, final_notes=? "
            "WHERE game_id=? AND branch=? AND result IS NULL",
            (result, time.time(), turn, json.dumps(final or {}), mem.plan, _notes_of(mem), mem.game_id, mem.branch))
        return cur.rowcount > 0

    def set_summary(self, game_id, branch, summary):
        if self.db is None:
            return
        self.db.execute("UPDATE games SET summary=? WHERE game_id=? AND branch=?", (summary[:SUMMARY_CAP], game_id, branch))

    def checkpoints(self, game_id, branch):
        if self.db is None:
            return []
        return [json.loads(r["stats"]) for r in self.db.execute(
            "SELECT stats FROM checkpoints WHERE game_id=? AND branch=? ORDER BY turn", (game_id, branch))]

    def games(self, level_key=None, limit=50):
        """Closed attempts, newest first (all levels, or one)."""
        if self.db is None:
            return []
        q = "SELECT * FROM games WHERE result IS NOT NULL"
        args = []
        if level_key:
            q += " AND level_key=?"
            args.append(level_key)
        q += " ORDER BY ended_at DESC LIMIT ?"
        args.append(limit)
        return [dict(r) for r in self.db.execute(q, args)]

    def level_record(self, level_key, limit=6):
        """The finished games on a level (won / lost / quit / abandoned; not suspended or reloaded attempts), oldest first."""
        if self.db is None:
            return []
        rows = self.db.execute(
            "SELECT result, end_turn, start_turn, ended_at FROM games WHERE level_key=? AND result IN ('won','lost','quit','abandoned') "
            "ORDER BY ended_at DESC LIMIT ?", (level_key, limit)).fetchall()
        return [dict(r) for r in reversed(rows)]

    # ---- attempts nobody closed, and their debriefs (Phase B) ------------------------------------------------------------
    def close_abandoned(self, agent, keep=None, idle=ABANDON_IDLE):
        """Closes this agent's attempts that are still open but no longer played -- the bridge stopped, or the level was
        restarted from the menu -- as `abandoned`, their last checkpoint as the final. `keep` (game_id, branch) is the
        attempt being played now; one another session updated in the last `idle` seconds is left alone. One too short to
        learn from is marked as not worth a debrief. Returns the rows closed."""
        if self.db is None:
            return []
        now = time.time()
        rows = [dict(r) for r in self.db.execute(
            "SELECT * FROM games WHERE result IS NULL AND agent IS ? AND updated_at < ?", (agent, now - idle))]
        closed = []
        with self._tx():
            for r in rows:
                if keep is not None and (r["game_id"], r["branch"]) == tuple(keep):
                    continue
                cps = self.checkpoints(r["game_id"], r["branch"])
                final = {"checkpoint": cps[-1] if cps else None, "abandoned": True}
                played = (r["end_turn"] or 0) - (r["start_turn"] or 0)
                self.db.execute("UPDATE games SET result='abandoned', ended_at=?, final=?, debrief_state=? "
                                "WHERE game_id=? AND branch=? AND result IS NULL",
                                (r["updated_at"], json.dumps(final), None if played >= DEBRIEF_MIN_TURNS else "not_worth",
                                 r["game_id"], r["branch"]))
                closed.append(dict(r, result="abandoned"))
        return closed

    def pending_debriefs(self, agent, level_key=None, limit=3):
        """This agent's finished attempts that taught something nobody wrote down yet (won, lost, quit or abandoned --
        a game-over debrief the agent never answered, or an attempt nobody closed -- no debrief, not declined, offered fewer than DEBRIEF_MAX_OFFERS times, in the last DEBRIEF_MAX_AGE): this level's
        first, then the newest."""
        if self.db is None:
            return []
        rows = [dict(r) for r in self.db.execute(
            "SELECT * FROM games WHERE agent IS ? AND result IN ('won','lost','quit','abandoned') AND debrief_state IS NULL "
            "AND debrief_offers < ? AND ended_at > ?", (agent, DEBRIEF_MAX_OFFERS, time.time() - DEBRIEF_MAX_AGE))]
        rows.sort(key=lambda r: (r["level_key"] != level_key, -(r["ended_at"] or 0)))
        return rows[:limit]

    def note_offered(self, rows):
        if self.db is None:
            return
        for r in rows:
            self.db.execute("UPDATE games SET debrief_offers=debrief_offers+1 WHERE game_id=? AND branch=?", (r["game_id"], r["branch"]))

    def set_debrief_state(self, game_id, branch, state):
        """'done', 'declined' or 'not_worth' (None: offer it again)."""
        if self.db is None:
            return
        self.db.execute("UPDATE games SET debrief_state=? WHERE game_id=? AND branch=?", (state, game_id, branch))

    def find_attempt(self, ref):
        """An attempt by its reference, "<game_id prefix>/<branch>" or a branch alone (as attempt_ref writes them)."""
        if self.db is None or not ref:
            return None
        game, _, branch = ref.rpartition("/")
        rows = self.db.execute("SELECT * FROM games WHERE branch=? AND game_id LIKE ?", (branch, (game or "") + "%")).fetchall()
        return dict(rows[0]) if len(rows) == 1 else None

    def attempt_chain(self, game_id, branch, limit=10):
        """The attempts this one followed (previous_game links, after failures on the same level), oldest first."""
        out = []
        row = self.branch(game_id, branch)
        while row is not None and row.get("previous_game") and len(out) < limit:
            row = self.branch(row["previous_game"], row["previous_branch"])
            if row is not None:
                out.append(row)
        return list(reversed(out))

    # ---- lessons ---------------------------------------------------------------------------------------------------------
    def _tx(self):
        return _Transaction(self.db)

    def items(self, scope=None, status="active", scopes=None):
        """The lessons of a scope (or of several, or of all), oldest first, as dicts. status None: every status."""
        if self.db is None:
            return []
        q, args = "SELECT * FROM lesson_items WHERE 1=1", []
        if isinstance(status, (tuple, list)):
            q += " AND status IN (%s)" % ",".join("?" * len(status))
            args += list(status)
        elif status is not None:
            q += " AND status=?"
            args.append(status)
        if scope is not None:
            scopes = [scope]
        if scopes is not None:
            q += " AND scope IN (%s)" % ",".join("?" * len(scopes))
            args += list(scopes)
        return [dict(r) for r in self.db.execute(q + " ORDER BY id", args)]

    def item(self, item_id):
        if self.db is None:
            return None
        row = self.db.execute("SELECT * FROM lesson_items WHERE id=?", (item_id,)).fetchone()
        return dict(row) if row else None

    def item_events(self, item_id):
        if self.db is None:
            return []
        return [dict(r) for r in self.db.execute("SELECT * FROM lesson_events WHERE item_id=? ORDER BY id", (item_id,))]

    def _event(self, item_id, event, game_id=None, branch=None, note=None):
        self.db.execute("INSERT INTO lesson_events (item_id, event, game_id, branch, at, note) VALUES (?,?,?,?,?,?)",
                        (item_id, event, game_id, branch, time.time(), note))

    def _snapshot(self, scopes, game_id=None):
        """Each changed scope's text as it reads now, as one more version in lessons_history (history / rollback)."""
        now = time.time()
        for scope in sorted(set(scopes)):
            self.db.execute("INSERT INTO lessons_history (scope, text, updated_at, game_id) VALUES (?,?,?,?)",
                            (scope, self.lessons(scope), now, game_id))

    @staticmethod
    def _capped(text):
        text = (text or "").strip()
        if len(text) > ITEM_CAP:
            return text[:ITEM_CAP], "a lesson was cut to %d characters (%s...): split it" % (ITEM_CAP, text[:40])
        return text, ""

    def _insert(self, scope, text, topic, source, game_id, branch, status="active", supersedes=None, counts=(0, 0)):
        now = time.time()
        cur = self.db.execute(
            "INSERT INTO lesson_items (scope, topic, text, status, source, game_id, branch, created_at, updated_at, confirmed,"
            " contradicted, last_game, supersedes) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (scope, topic if topic in TOPICS else guess_topic(text, scope), text, status, source, game_id, branch, now, now,
             counts[0], counts[1], game_id, supersedes))
        return cur.lastrowid

    def add_item(self, scope, text, topic=None, source="debrief", game_id=None, branch=None, status="active"):
        """A new lesson. Returns (id, warning); id None when the text is empty or the store is off."""
        if self.db is None:
            return None, ""
        text, warn = self._capped(text)
        if not text:
            return None, ""
        with self._tx():
            item_id = self._insert(scope, text, topic, source, game_id, branch, status)
            self._event(item_id, "added", game_id, branch, source)
            self._snapshot([scope], game_id)
        return item_id, warn

    def confirm_item(self, item_id, game_id=None, branch=None):
        """A debrief (or the agent in play) found the lesson held; a provisional one is kept from now on (active). False if
        there is no such live lesson."""
        if self.db is None:
            return False
        old = self.item(item_id)
        if old is None or old["status"] not in LIVE:
            return False
        with self._tx():
            self.db.execute("UPDATE lesson_items SET status='active', confirmed=confirmed+1, last_game=?, updated_at=? WHERE id=?",
                            (game_id, time.time(), item_id))
            self._event(item_id, "confirmed", game_id, branch, "accepted" if old["status"] == "provisional" else None)
            if old["status"] == "provisional":
                self._snapshot([old["scope"]], game_id)
        return True

    def doubt_item(self, item_id, why="", game_id=None, branch=None):
        """The agent found a lesson did not hold here: counted against it (the ranking), the lesson kept for a debrief to
        judge. False if there is no such live lesson."""
        if self.db is None:
            return False
        old = self.item(item_id)
        if old is None or old["status"] not in LIVE:
            return False
        with self._tx():
            self.db.execute("UPDATE lesson_items SET contradicted=contradicted+1, last_game=?, updated_at=? WHERE id=?",
                            (game_id, time.time(), item_id))
            self._event(item_id, "doubted", game_id, branch, why or None)
        return True

    def revise_item(self, item_id, text, game_id=None, branch=None, topic=None, source="debrief"):
        """A better wording of an active lesson: a new lesson that takes its place (and its counts); the old one is kept,
        retired, pointing to it. Returns (new id or None, warning)."""
        if self.db is None:
            return None, ""
        old = self.item(item_id)
        if old is None or old["status"] not in LIVE:
            return None, "L%s is not an active lesson" % item_id
        text, warn = self._capped(text)
        if not text:
            return None, "L%s: a revision needs a text (retire it to drop it)" % item_id
        with self._tx():
            new_id = self._insert(old["scope"], text, topic or old["topic"], source, game_id, branch, supersedes=item_id,
                                  counts=(old["confirmed"], old["contradicted"]))
            self.db.execute("UPDATE lesson_items SET status='retired', superseded_by=?, why=?, updated_at=? WHERE id=?",
                            (new_id, "revised", time.time(), item_id))
            self._event(item_id, "revised", game_id, branch, "by L%d" % new_id)
            self._event(new_id, "added", game_id, branch, "revises L%d" % item_id)
            self._snapshot([old["scope"]], game_id)
        return new_id, warn

    def retire_item(self, item_id, why="", game_id=None, branch=None):
        """The lesson was wrong, or no longer applies: kept, retired, with why. False if there is no such active lesson."""
        if self.db is None:
            return False
        old = self.item(item_id)
        if old is None or old["status"] not in LIVE:
            return False
        with self._tx():
            self.db.execute("UPDATE lesson_items SET status='retired', why=?, updated_at=? WHERE id=?", (why or None, time.time(), item_id))
            self._event(item_id, "retired", game_id, branch, why or None)
            self._snapshot([old["scope"]], game_id)
        return True

    def merge_items(self, item_ids, text, topic=None, game_id=None):
        """Curation: several active lessons of one scope said once. A new lesson with the summed counts; the old ones
        retired, pointing to it. Returns (new id, warning)."""
        olds = [self.item(i) for i in item_ids]
        if len(olds) < 2 or any(o is None or o["status"] != "active" for o in olds):
            return None, "merge needs two or more active lessons"
        if len({o["scope"] for o in olds}) != 1:
            return None, "merge takes lessons of one scope"
        text, warn = self._capped(text)
        if not text:
            return None, "merge needs the merged text"
        with self._tx():
            new_id = self._insert(olds[0]["scope"], text, topic or olds[0]["topic"], "merge", game_id, None,
                                  counts=(sum(o["confirmed"] for o in olds), sum(o["contradicted"] for o in olds)))
            for o in olds:
                self.db.execute("UPDATE lesson_items SET status='retired', superseded_by=?, why=?, updated_at=? WHERE id=?",
                                (new_id, "merged into L%d" % new_id, time.time(), o["id"]))
                self._event(o["id"], "retired", game_id, None, "merged into L%d" % new_id)
            self._event(new_id, "added", game_id, None, "merges " + ", ".join("L%d" % o["id"] for o in olds))
            self._snapshot([olds[0]["scope"]], game_id)
        return new_id, warn

    def move_item(self, item_id, scope=None, topic=None):
        """Curation (experience_report.py move): another scope or topic for a lesson, same id."""
        old = self.item(item_id)
        if old is None:
            return False
        with self._tx():
            self.db.execute("UPDATE lesson_items SET scope=?, topic=?, updated_at=? WHERE id=?",
                            (scope or old["scope"], topic or old["topic"], time.time(), item_id))
            self._event(item_id, "moved", note="%s/%s -> %s/%s" % (old["scope"], old["topic"], scope or old["scope"], topic or old["topic"]))
            self._snapshot([old["scope"], scope or old["scope"]])
        return True

    def replace_scope_text(self, scope, text, game_id=None, branch=None, source="rewrite"):
        """The older form of a debrief: a whole new text for a scope. It is matched against the scope's lessons --
        unchanged ones stay (ids and counts kept), close rewordings become revisions, a retired lesson written again comes
        back, the rest is added -- and every lesson the text left out is retired as "dropped in a rewrite", and listed.
        Returns {"kept", "added", "revised" [[old, new]], "restored", "dropped" [{id, text}], "warnings"}."""
        out = {"kept": [], "added": [], "revised": [], "restored": [], "dropped": [], "warnings": []}
        if self.db is None:
            return out
        wanted = split_lessons(text)
        active = self.items(scope)
        retired = {_norm(i["text"]): i for i in self.items(scope, status="retired")}
        by_norm = {}
        for i in active:
            by_norm.setdefault(_norm(i["text"]), i)
        left = {i["id"]: i for i in active}
        pending = []
        for t in wanted:
            hit = by_norm.get(_norm(t))
            if hit is not None and hit["id"] in left:
                out["kept"].append(hit["id"])
                del left[hit["id"]]
            else:
                pending.append(t)
        with self._tx():
            now = time.time()
            for t in pending:
                t, warn = self._capped(t)
                if warn:
                    out["warnings"].append(warn)
                best, ratio = None, 0.0
                for i in left.values():
                    r = difflib.SequenceMatcher(None, _norm(i["text"]), _norm(t)).ratio()
                    if r > ratio:
                        best, ratio = i, r
                if best is not None and ratio >= 0.6:
                    new_id = self._insert(scope, t, best["topic"], source, game_id, branch, supersedes=best["id"],
                                          counts=(best["confirmed"], best["contradicted"]))
                    self.db.execute("UPDATE lesson_items SET status='retired', superseded_by=?, why='revised', updated_at=? WHERE id=?",
                                    (new_id, now, best["id"]))
                    self._event(best["id"], "revised", game_id, branch, "by L%d (a rewrite)" % new_id)
                    self._event(new_id, "added", game_id, branch, "revises L%d (a rewrite)" % best["id"])
                    out["revised"].append([best["id"], new_id])
                    del left[best["id"]]
                elif _norm(t) in retired and retired[_norm(t)]["superseded_by"] is None:
                    back = retired.pop(_norm(t))
                    self.db.execute("UPDATE lesson_items SET status='active', why=NULL, updated_at=? WHERE id=?", (now, back["id"]))
                    self._event(back["id"], "restored", game_id, branch, source)
                    out["restored"].append(back["id"])
                else:
                    new_id = self._insert(scope, t, None, source, game_id, branch)
                    self._event(new_id, "added", game_id, branch, source)
                    out["added"].append(new_id)
            for i in left.values():
                self.db.execute("UPDATE lesson_items SET status='retired', why='dropped in a rewrite', updated_at=? WHERE id=?",
                                (now, i["id"]))
                self._event(i["id"], "retired", game_id, branch, "dropped in a rewrite")
                out["dropped"].append({"id": i["id"], "text": i["text"]})
            self._snapshot([scope], game_id)
        return out

    def lessons(self, scope):
        """A scope's active lessons as one text, one "- " line each (what lessons_history keeps; not the provisional)."""
        return "\n".join("- " + i["text"].replace("\n", "\n  ") for i in self.items(scope))

    def set_lessons(self, scope, text, game_id=None):
        """A whole new text for a scope (replace_scope_text; "" retires every lesson in it). Returns a warning string
        naming what was dropped, or ''."""
        if self.db is None:
            return ""
        out = self.replace_scope_text(scope, text, game_id, source="set")
        warns = list(out["warnings"])
        if out["dropped"]:
            warns.append("%d lesson(s) of %s retired: %s" % (len(out["dropped"]), scope,
                                                              ", ".join("L%d" % d["id"] for d in out["dropped"])))
        return "; ".join(warns)

    def lessons_history(self, scope):
        if self.db is None:
            return []
        return [dict(r) for r in self.db.execute(
            "SELECT id, text, updated_at, game_id FROM lessons_history WHERE scope=? ORDER BY id", (scope,))]

    def rollback_lessons(self, scope, history_id):
        """Makes an earlier version (a lessons_history id) the current text again: its lessons come back (retired ones
        are restored, ids and counts kept), the ones it did not have are retired."""
        row = self.db.execute("SELECT text FROM lessons_history WHERE id=? AND scope=?", (history_id, scope)).fetchone()
        if row is None:
            raise KeyError("no version %s of %s" % (history_id, scope))
        return self.set_lessons(scope, row["text"])

    def forget_game(self, game_id):
        """Deletes every attempt of a game and their checkpoints. Returns how many attempts went."""
        if self.db is None:
            return 0
        self.db.execute("DELETE FROM checkpoints WHERE game_id=?", (game_id,))
        return self.db.execute("DELETE FROM games WHERE game_id=?", (game_id,)).rowcount

    def scopes(self):
        """Every scope that holds an active lesson."""
        if self.db is None:
            return []
        return [r["scope"] for r in self.db.execute("SELECT DISTINCT scope FROM lesson_items WHERE status='active' ORDER BY scope")]


def _notes_of(mem):
    """The notes the record keeps: the free text and the facts (memory.notes_text), or the text alone."""
    return mem.notes_text() if hasattr(mem, "notes_text") else mem.notes


class _Transaction:
    """BEGIN IMMEDIATE ... COMMIT, or ROLLBACK on an exception; nested uses join the outer one."""

    def __init__(self, db):
        self.db = db
        self.outer = False

    def __enter__(self):
        self.outer = not self.db.in_transaction
        if self.outer:
            self.db.execute("BEGIN IMMEDIATE")
        return self

    def __exit__(self, exc_type, exc, tb):
        if self.outer:
            self.db.execute("ROLLBACK" if exc_type else "COMMIT")
        return False


def open_store(path):
    """The store at `path` (None: the default path; "": disabled). A store that cannot be opened is reported on stderr and
    left disabled: learning is never worth failing a game over."""
    if path is None:
        path = default_path()
    try:
        return Experience(path)
    except Exception as e:  # noqa: BLE001
        print("experience store %s unavailable (%s); playing without it (--experience / experience_path picks another file)"
              % (path, e), file=sys.stderr)
        return Experience(None)


# ---- what is recorded from a view -----------------------------------------------------------------------------------------
def checkpoint_from_view(view):
    """The dungeon's numbers at this moment, as one small dict."""
    own = view.get("own", {})
    force = own.get("force", {})
    rooms = {}
    room_slabs = {}
    lair = [0, 0]
    heart = None
    for r in own.get("rooms", []):
        rooms[r["kind"]] = rooms.get(r["kind"], 0) + 1
        if "slabs" in r:
            room_slabs[r["kind"]] = room_slabs.get(r["kind"], 0) + r["slabs"]
        if r["kind"] == "LAIR" and r.get("capacity"):
            lair[0] += r["capacity"].get("used", 0)
            lair[1] += r["capacity"].get("total", 0)
        if r["kind"] == "DUNGEON_HEART" and r.get("max_health"):
            heart = round(100 * r.get("health", 0) / r["max_health"])
    enemy = sum(f.get("fighter_score", 0) for o, f in (view.get("visible", {}).get("force_by_owner") or {}).items()
                if str(o) != str(view.get("seat", {}).get("player")))
    cp = {"turn": view.get("turn"), "gold": own.get("gold"), "creatures": len(own.get("creatures", [])),
          "fighters": force.get("fighters"), "workers": force.get("workers"), "score": force.get("fighter_score"),
          "rooms": rooms, "enemy_score": enemy}
    if heart is not None:
        cp["heart"] = heart
    # For the pay day report (prompt.render_payday_report): ground, room slabs, lair places, the portal's limit.
    if own.get("territory") is not None:
        cp["area"] = own["territory"]
    if room_slabs:
        cp["room_slabs"] = room_slabs
    if lair[1] > 0:
        cp["lair"] = lair
    if own.get("creature_limit") is not None:
        cp["limit"] = [own.get("creatures_active", 0), own["creature_limit"]]
    return cp


def observe(mem, view):
    """Adds a checkpoint when a pay day has passed since the last one (or there is none yet)."""
    pd = view.get("seat", {}).get("payday", {})
    ttp = pd.get("turns_to_payday")
    last = mem.checkpoints[-1] if mem.checkpoints else None
    passed = last is None or (ttp is not None and last.get("_ttp") is not None and ttp > last["_ttp"])
    if not passed and last is not None and ttp is None:
        passed = view.get("turn", 0) - last.get("turn", 0) >= 1200
    if passed:
        cp = checkpoint_from_view(view)
        cp["_ttp"] = ttp
        mem.add_checkpoint(cp)
    elif last is not None:
        last["_ttp"] = ttp


def final_from_view(view):
    """What the end of an attempt looked like: the last numbers, the objective, the latest events."""
    own = (view or {}).get("own", {})
    events = [e.get("text") or e.get("kind") for e in own.get("events", [])][-10:]
    obj = own.get("objective")
    return {"checkpoint": checkpoint_from_view(view) if view else None,
            "objective": obj.get("text") if isinstance(obj, dict) else obj,
            "events": [e for e in events if e],
            "victory_state": (view or {}).get("seat", {}).get("victory_state")}


def payday_turns(view, default=1200):
    """About how many game turns one pay day takes, from the view's pay-day progress (the pay-day speed can change)."""
    pd = (view or {}).get("seat", {}).get("payday", {})
    gap, progress, ttp = pd.get("gap"), pd.get("progress"), pd.get("turns_to_payday")
    if gap and ttp and progress is not None and gap > progress:
        return max(1, round(gap * ttp / (gap - progress)))
    return default


class Recorder:
    """Keeps one seat's game record in step with its play: which attempt this is, how far it got, how it ended. Shared by
    mcp_session.py, bridge.py and agent_vs_agent.py so the three cannot disagree about what a result means."""

    def __init__(self, store, player, agent=None):
        self.store = store
        self.player = player
        self.agent = agent
        self.opponents = None       # [{player, kind, name, ...}] once the view names the players (players[])
        self.shown = False          # whether this attempt's EXPERIENCE has been shown in full
        self.latency = LatencyChain()  # each decision's latency chain (latency.py), kept on the record as final.latency
        self.abandoned = []         # this agent's attempts start() found left open and closed as abandoned
        self.tips = []              # tips from human players taken since the agent was last told: [(id, text, who)]

    def start(self, mem, view, resumed):
        """At connect: a resumed memory whose attempt is already over (lost, suspended, reloaded...) or that the game
        has since gone back from (a save loaded while no bridge was watching) is a new attempt at that game."""
        if resumed:
            row = self.store.branch(mem.game_id, mem.branch)
            turn = (view or {}).get("turn", 0)
            if row is not None and row["result"] is None and row["end_turn"] is not None and turn + 5 < row["end_turn"]:
                self.store.close_branch(mem, "reloaded", row["end_turn"])
                row = self.store.branch(mem.game_id, mem.branch)
            if row is not None and row["result"] is not None:
                mem.start_branch()
                mem.flush()
        self.store.open_branch(mem, self.player, self.agent, self.opponents, turn=(view or {}).get("turn"))
        # This agent's other attempts still open are no longer played (this one is): abandoned.
        self.abandoned = self.store.close_abandoned(self.agent, keep=(mem.game_id, mem.branch))

    def pending_debriefs(self, mem, limit=3):
        """This agent's earlier attempts waiting for a debrief (Experience.pending_debriefs), this level's first."""
        return self.store.pending_debriefs(self.agent, (mem.level or {}).get("key"), limit=limit)

    def decision(self, mem, view, ctx=None):
        """Before each decision: a checkpoint when a pay day has passed, the attempt's progress, and any tip a human just
        gave (take_tips). With a policy's `ctx` (its reasons and severity already set) it also puts there the EXPERIENCE
        text for the policy to show -- in full once per attempt, then one line with this decision's RECALL -- and, for a
        flash decision, `flash_recall` (render_flash shows it)."""
        self.opponents = opponents_from_view(view, self.player) or self.opponents
        observe(mem, view)
        places_mod.refresh(mem, view)
        self.store.progress(mem, view.get("turn"), self.opponents)
        self.tips += take_tips(self.store, mem, self.opponents, view)
        if ctx is not None:
            reasons = ctx.get("reasons") or []
            full = not self.shown
            flash = not full and ctx.get("severity", 3) <= URGENT_SEVERITY and "reloaded" not in reasons
            exp, recall = self.lessons_text(mem, reasons, full, flash)
            ctx["experience"] = exp if not flash else None
            ctx["flash_recall"] = recall if flash else None

    def lessons_text(self, mem, reasons, full, flash=False, pending=None):
        """(EXPERIENCE text, flash recall text) for the decision about to be shown: the full lessons on an attempt's first
        decision, else one line plus RECALL / REFRESHER; with the tips just taken. Notes what it showed."""
        n = mem.count + 1
        tips = render_tips(self.tips)
        self.tips = []
        exp = render_experience(self.store, mem, self.opponents, full=full, decision_n=n if full else None, pending=pending)
        self.shown = True
        if full:
            return "\n".join(x for x in (exp, tips) if x), ""
        recall = render_recall(self.store, mem, self.opponents, reasons, n, flash=flash)
        if flash:
            return "", "\n".join(x for x in (tips, recall) if x)
        return "\n".join(x for x in (exp, recall, tips) if x), ""

    def decided(self, mem, turn):
        """After a decision is recorded: its counts reach the record at once, not with the next decision."""
        self.store.progress(mem, turn)

    def reloaded(self, old_mem, reached, new_mem, view):
        """A save was loaded: the attempt that was being played ends there, and the loaded one begins."""
        self.shown = False
        if old_mem is not None:
            self.store.close_branch(old_mem, "reloaded", reached)
        self.store.open_branch(new_mem, self.player, self.agent, self.opponents, turn=view.get("turn"))

    def ended(self, mem, view, result, combat=None):
        """The seat won or lost: the attempt is over. Returns True if this closed it (False if already closed).
        `combat`: the game's get_combat_stats for the seat, kept on the record (combat_report.py reads it)."""
        observe(mem, view)
        self.keep_places(mem)
        return self.store.close_branch(mem, result, view.get("turn"), self._final(view, combat))

    def keep_places(self, mem):
        """The places of this game worth remembering on the level (the agent's, the enemy hearts, the hero gates), as
        provisional lessons of the level for the debrief to keep. Returns their ids."""
        ids = []
        for text in places_mod.lesson_texts(mem):
            got = add_play_lesson(self.store, mem, self.opponents, text, "level", "map", source="places")
            if got.get("id") is not None and got.get("note") != "already known":
                ids.append(got["id"])
        return ids

    def _final(self, view, combat):
        final = _with_combat(final_from_view(view), combat)
        lat = self.latency.summary()
        if lat:
            final = dict(final or {})
            final["latency"] = lat
        return final

    def left(self, mem, view, last_save_turn, combat=None):
        """The game left the level while undecided. Returns (result, worth_a_debrief): "suspended" when it was saved
        within the last pay day (it will be continued; the debrief comes when it ends), else "quit" -- worth a debrief
        only once the game had run at least a pay day."""
        turn = (view or {}).get("turn", 0)
        pd = payday_turns(view)
        if last_save_turn is not None and turn - last_save_turn <= pd:
            result, debrief = "suspended", False
        else:
            result = "quit"
            debrief = turn - (mem.start_turn or 0) >= pd
        if result == "quit":
            self.keep_places(mem)
        self.store.close_branch(mem, result, turn, self._final(view, combat))
        if result == "quit" and not debrief:
            self.store.set_debrief_state(mem.game_id, mem.branch, "not_worth")
        return result, debrief

    def quit(self, mem, view, combat=None):
        """The agent ends the attempt itself while the game goes on (disconnect(end="quit")). Returns whether it is worth a
        debrief (at least a pay day of play)."""
        turn = (view or {}).get("turn", 0)
        worth = turn - (mem.start_turn or 0) >= payday_turns(view)
        self.keep_places(mem)
        self.store.close_branch(mem, "quit", turn, self._final(view, combat))
        if not worth:
            self.store.set_debrief_state(mem.game_id, mem.branch, "not_worth")
        return worth

    def suspend(self, mem, view, combat=None):
        """The agent leaves the attempt to be continued later (disconnect(end="suspend")): no debrief until it ends."""
        self.store.close_branch(mem, "suspended", (view or {}).get("turn", 0), self._final(view, combat))


def _with_combat(final, combat):
    if combat:
        final = dict(final or {})
        final["combat"] = combat
    return final


def opponents_from_view(view, player):
    """The other keepers in the level as the view names them (players[], 09 section 6.8), or None before it does."""
    players = (view or {}).get("players")
    if not players:
        return None
    return [p for p in players if p.get("player") != player]


# ---- reading lessons back, and the debrief ---------------------------------------------------------------------------------
def opponent_identity(p):
    """The key an opponent's lessons are kept under, or None when nothing identifies it from game to game: a human by the
    keeper name they chose, a computer keeper by its AI type, an agent by the name it gave its seat."""
    kind, name = p.get("kind"), (p.get("name") or "").strip()
    if kind == "human" and name:
        return "human:" + name
    if kind == "computer" and p.get("ai_type"):
        return "computer:" + p["ai_type"].strip().rstrip(".")
    if kind == "external" and name and not name.startswith("External "):
        return "agent:" + name
    return None


def opponent_label(p):
    ident = opponent_identity(p) or ""
    return ident.split(":", 1)[1] + " (" + p.get("kind", "?") + ")" if ident else "player %s" % p.get("player")


def scopes_for(mem, opponents):
    """[(scope, heading)] that apply to this game, most important first: the playbook, the campaign, the level, then each
    identifiable opponent."""
    lv = mem.level or {}
    out = [("general", "PLAYBOOK (any level)")]
    if lv.get("campaign") and lv["campaign"] != "?":
        out.append(("campaign:" + lv["campaign"], "CAMPAIGN %s (its rules and creatures)" % lv["campaign"]))
    if lv.get("key"):
        out.append(("level:" + lv["key"], "THIS LEVEL (%s)" % (lv.get("name") or lv["key"])))
    for p in opponents or []:
        ident = opponent_identity(p)
        if ident:
            out.append(("opponent:" + ident, "OPPONENT %s" % opponent_label(p)))
    return out


def _record_line(rows):
    return ", ".join("%s t%s" % (r["result"], r["end_turn"]) for r in rows)


def opponent_record(store, ident, limit=200):
    """{result: count} over the finished games that had this opponent."""
    if store.db is None:
        return {}
    counts = {}
    for r in store.db.execute("SELECT result, opponents FROM games WHERE result IN ('won','lost','quit','abandoned') "
                              "ORDER BY ended_at DESC LIMIT ?", (limit,)):
        try:
            opps = json.loads(r["opponents"] or "[]")
        except ValueError:
            continue
        if any(opponent_identity(p) == ident for p in opps):
            counts[r["result"]] = counts.get(r["result"], 0) + 1
    return counts


def _rank(items):
    """Best first: confirmed more often (net of contradictions), then the most recently written."""
    return sorted(items, key=lambda i: (-(i["confirmed"] - i["contradicted"]), -(i["updated_at"] or 0), -i["id"]))


def pick_items(items, budget):
    """The best-ranked lessons that fit in `budget` characters (None: all), in their own order."""
    if budget is None:
        return list(items)
    chosen, used = set(), 0
    for i in _rank(items):
        n = len(i["text"]) + 8
        if chosen and used + n > budget:
            continue
        chosen.add(i["id"])
        used += n
    return [i for i in items if i["id"] in chosen]


def item_lines(item, indent="    ", detail=False):
    """One lesson as text lines, its id first ([L12]) so the agent can name it in a debrief."""
    new = item.get("status") == "provisional"
    tag = "[L%d]" % item["id"] + (" (new)" if new and not detail else "")
    if detail:
        bits = (["new"] if new else []) + [item["topic"]]
        if item["confirmed"]:
            bits.append("confirmed %d" % item["confirmed"])
        if item["contradicted"]:
            bits.append("doubted %d" % item["contradicted"])
        tag += " (%s)" % ", ".join(bits)
    first, *rest = item["text"].splitlines() or [""]
    return [indent + tag + " " + first] + [indent + "  " + r for r in rest]


def attempt_ref(row):
    """How an attempt is named to the agent (record_debrief / get_debrief take it back): "<game id prefix>/<branch>"."""
    return "%s/%s" % (row["game_id"][:8], row["branch"])


def _when(ts):
    return time.strftime("%Y-%m-%d", time.localtime(ts)) if ts else "?"


def render_pending(rows):
    """The earlier attempts offered for a debrief, as lines (empty when there are none)."""
    if not rows:
        return []
    lines = ["  EARLIER ATTEMPTS WITHOUT A DEBRIEF (what they taught is not kept yet; answer when calm, or decline):"]
    for r in rows:
        lines.append("    %s: %s%s, %s at turn %s after %s decisions (%s)" % (
            attempt_ref(r), r["level_key"] or "?", (" " + r["level_name"]) if r["level_name"] else "", r["result"],
            r["end_turn"], r["decisions"], _when(r["ended_at"])))
    lines.append("    get_debrief(attempt) shows one's record; record_debrief(attempt=..., summary, add/confirm/revise/retire) keeps "
                 "its lessons; record_debrief(attempt=..., decline=true) if it taught nothing.")
    return lines


def render_experience(store, mem, opponents, full=True, every=False, pending=None, topic=None, decision_n=None):
    """The EXPERIENCE section: in full (the first decision of an attempt), else one line. '' when there is no store. In
    full each scope shows its best-ranked lessons (provisional ones too, marked new) within its budget (SCOPE_CAPS), or
    all of them with `every` (get_experience); `topic`: only that topic's. `pending`: earlier attempts to offer for a
    debrief (pending_debriefs). `decision_n`: the decision this is shown at -- what it showed is noted in the memory, so
    recall does not repeat it straight away."""
    if store is None or not store.enabled:
        return ""
    lv = mem.level or {}
    record = store.level_record(lv.get("key")) if lv.get("key") else []
    lessons = [(scope, head, [i for i in store.items(scope, status=LIVE) if topic is None or i["topic"] == topic])
               for scope, head in scopes_for(mem, opponents)]
    have = [x for x in lessons if x[2]]
    if not full:
        won = sum(1 for r in record if r["result"] == "won")
        return ("EXPERIENCE: %d earlier game(s) on this level (%d won); %d lesson set(s) shown at the start; get_experience shows "
                "them again." % (len(record), won, len(have)))
    lines = ["EXPERIENCE (your own lessons from earlier games; carry what applies into your plan):"]
    lines.append("  This level: " + ("%d earlier game(s): %s" % (len(record), _record_line(record)) if record else "never played before"))
    chain = store.attempt_chain(mem.game_id, mem.branch) if mem.game_id else []
    if chain:
        lines.append("  This game follows %d earlier attempt(s) at it: %s" % (len(chain), _record_line(chain)))
    for p in opponents or []:
        ident = opponent_identity(p)
        if ident:
            rec = opponent_record(store, ident)
            if rec:
                lines.append("  vs %s: %s" % (opponent_label(p), ", ".join("%d %s" % (n, res) for res, n in sorted(rec.items()))))
    if not have:
        lines.append("  No lessons %s: you will write them when this game ends." % ("yet" if topic is None else "on " + topic))
    shown_all = []
    for scope, head, items in have:
        lines.append("  %s:" % head)
        shown = pick_items(items, None if every else cap_for(scope))
        shown_all += shown
        for i in shown:
            lines += item_lines(i)
        if len(shown) < len(items):
            lines.append("    (+%d more lesson(s) here, not shown: get_experience shows them all)" % (len(items) - len(shown)))
    lines += render_pending(pending)
    if decision_n is not None and topic is None:
        r = _recall_state(mem)
        note_shown(mem, shown_all, decision_n)
        for t in TOPICS:
            r["topics"][t] = decision_n
        r["refresh"] = decision_n
    return "\n".join(lines)


# ---- recall (Phase C) -------------------------------------------------------------------------------------------------------
_SCOPE_RANK = {"level": 0, "opponent": 1, "campaign": 2, "general": 3}


def _recall_state(mem):
    r = mem.recall if isinstance(getattr(mem, "recall", None), dict) else {}
    r.setdefault("shown", {})
    r.setdefault("topics", {})
    mem.recall = r
    return r


def note_shown(mem, items, decision_n):
    shown = _recall_state(mem)["shown"]
    for i in items:
        times = shown.get(str(i["id"]), [0, None])[0]
        shown[str(i["id"])] = [times + 1, decision_n]


def topics_for(reasons):
    """The topics a decision's reasons call for, most pressing first."""
    out = []
    for r in reasons or []:
        for t in REASON_TOPICS.get(r, ("creatures",) if str(r).startswith("new_kind_") else ()):
            if t not in out:
                out.append(t)
    return out


def recall_pick(store, mem, opponents, topic=None, limit=RECALL_ITEMS, budget=RECALL_BUDGET, exclude=()):
    """The lessons to bring back: this game's scopes (the level and its opponents before the campaign and the playbook),
    the most confirmed first, and among equals the ones this attempt has been shown least (so recall rotates)."""
    scopes = [s for s, _ in scopes_for(mem, opponents)]
    shown = _recall_state(mem)["shown"]
    items = [i for i in store.items(scopes=scopes, status=LIVE) if (topic is None or i["topic"] == topic) and i["id"] not in exclude]

    def key(i):
        net = i["confirmed"] - i["contradicted"]
        return (-(2 * net - shown.get(str(i["id"]), [0])[0]), _SCOPE_RANK.get(scope_kind(i["scope"]), 9), -(i["updated_at"] or 0))
    out, used = [], 0
    for i in sorted(items, key=key):
        n = len(i["text"]) + 10
        if len(out) >= limit or (out and used + n > budget):
            continue
        out.append(i)
        used += n
    return out


def render_recall(store, mem, opponents, reasons, decision_n, flash=False):
    """The lessons this decision should be reminded of (RECALL lines; a REFRESHER now and then), noted as shown. '' when
    there are none, or no store. A flash decision gets at most FLASH_RECALL_ITEMS, of its first combat or defence topic."""
    if store is None or not store.enabled:
        return ""
    r = _recall_state(mem)
    lines, used = [], set()
    topics = topics_for(reasons)
    if flash:
        topics = [t for t in topics if t in ("combat", "defence")][:1]
    for t in topics:
        last = r["topics"].get(t)
        if last is not None and decision_n - last < RECALL_GAP:
            continue
        r["topics"][t] = decision_n
        if flash:
            items = recall_pick(store, mem, opponents, t, FLASH_RECALL_ITEMS, FLASH_RECALL_BUDGET, used)
        else:
            items = recall_pick(store, mem, opponents, t, exclude=used)
        if not items:
            continue
        lines.append("RECALL (%s: your lessons for this; confirm_lesson / doubt_lesson if one holds or fails here):" % t)
        for i in items:
            lines += item_lines(i, "  ")
        used |= {i["id"] for i in items}
        note_shown(mem, items, decision_n)
        if flash:
            break
    if not flash and decision_n - r.get("refresh", decision_n) >= REFRESH_EVERY:
        r["refresh"] = decision_n
        items = recall_pick(store, mem, opponents, "setup", 3, REFRESH_BUDGET // 2, used)
        items += recall_pick(store, mem, opponents, None, REFRESH_ITEMS, REFRESH_BUDGET - sum(len(i["text"]) for i in items),
                             used | {i["id"] for i in items})
        if items:
            lines.append("REFRESHER (your lessons again, every %d decisions; get_experience shows them all):" % REFRESH_EVERY)
            for i in items:
                lines += item_lines(i, "  ")
            note_shown(mem, items, decision_n)
    return "\n".join(lines)


# ---- learning in play (Phase D) --------------------------------------------------------------------------------------------
def default_scope(text):
    """Where a lesson noted in play goes when the agent does not say: the level if it names places, else the playbook."""
    return "level" if _COORD.search(text or "") else "playbook"


def add_play_lesson(store, mem, opponents, text, scope=None, topic=None, source="play"):
    """A lesson noted during the game: kept provisional (recalled marked new) until a debrief keeps, revises or retires
    it. One already known (same words, in that scope) is not added twice. Returns {"id", "scope", "status"} or
    {"error"}."""
    if store is None or not store.enabled:
        return {"error": "no experience store in this session"}
    text = (text or "").strip()
    if not text:
        return {"error": "a lesson needs its text"}
    scope_key = _scope_named(scope or default_scope(text), mem, opponents)
    if scope_key is None:
        return {"error": "no scope %r in this game (playbook, campaign, level, or opponent:<identity>)" % scope}
    for i in store.items(scope_key, status=LIVE):
        if _norm(i["text"]) == _norm(text):
            return {"id": i["id"], "scope": scope_key, "status": i["status"], "note": "already known"}
    item_id, warn = store.add_item(scope_key, text, topic if topic in TOPICS else None, source, mem.game_id, mem.branch,
                                   status="provisional")
    out = {"id": item_id, "scope": scope_key, "status": "provisional"}
    if warn:
        out["warning"] = warn
    return out


def apply_play_lessons(store, mem, opponents, ops):
    """`lessons` on a decision's answer: {"remember": [{text, scope?, topic?}], "confirm": [ids], "doubt": [{id, why}]}.
    Returns {"remembered": [ids], "confirmed", "doubted", "warnings"}."""
    out = {"remembered": [], "confirmed": [], "doubted": [], "warnings": []}
    if not ops:
        return out
    if store is None or not store.enabled:
        out["warnings"].append("no experience store: lessons were not kept")
        return out
    mine = {i["id"] for i in store.items(scopes=[s for s, _ in scopes_for(mem, opponents)], status=LIVE)}
    for r in ops.get("remember") or []:
        r = r if isinstance(r, dict) else {"text": r}
        got = add_play_lesson(store, mem, opponents, r.get("text"), r.get("scope"), r.get("topic"))
        if "error" in got:
            out["warnings"].append("remember: " + got["error"])
        else:
            out["remembered"].append(got["id"])
            if got.get("warning"):
                out["warnings"].append(got["warning"])
    for raw in ops.get("confirm") or []:
        item_id = _item_id(raw)
        if item_id in mine and store.confirm_item(item_id, mem.game_id, mem.branch):
            out["confirmed"].append(item_id)
        else:
            out["warnings"].append("confirm: %r is not one of this game's lessons" % (raw,))
    for r in ops.get("doubt") or []:
        r = r if isinstance(r, dict) else {"id": r}
        item_id = _item_id(r.get("id"))
        if item_id in mine and store.doubt_item(item_id, r.get("why") or "", mem.game_id, mem.branch):
            out["doubted"].append(item_id)
        else:
            out["warnings"].append("doubt: %r is not one of this game's lessons" % (r.get("id"),))
    return out


def take_tips(store, mem, opponents, view):
    """A human's chat lines starting "tip:" since the last ones taken, kept as provisional lessons (source user).
    Returns [(id, text, who)] of the new ones."""
    if store is None or not store.enabled or not view:
        return []
    r = _recall_state(mem)
    humans = {p.get("player"): p.get("name") or "player %s" % p.get("player")
              for p in view.get("players") or [] if p.get("kind") == "human"}
    last = r.get("tip_turn", -1)
    out = []
    for c in view.get("chat") or []:
        text = (c.get("text") or "").strip()
        if c.get("turn", -1) <= last or c.get("player") not in humans or not text.lower().startswith(TIP_PREFIX):
            continue
        r["tip_turn"] = max(r.get("tip_turn", -1), c.get("turn", -1))
        tip = text[len(TIP_PREFIX):].strip()
        got = add_play_lesson(store, mem, opponents, tip, source="user")
        if "id" in got and got.get("note") != "already known":
            out.append((got["id"], tip, humans[c["player"]]))
    return out


def render_tips(tips):
    return "\n".join("TIP NOTED: %s's tip is kept as lesson L%d (new: the debrief confirms, revises or retires it): %s"
                     % (who, i, text) for i, text, who in tips)


def debrief_text(store, mem, result, final, opponents, attempt=None):
    """What the model is shown when a game ends, and asked to turn into lessons. `attempt`: the reference of an earlier
    attempt being debriefed afterwards (an abandoned one: how it ended is unknown)."""
    turn = (final or {}).get("end_turn") or ((final or {}).get("checkpoint") or {}).get("turn")
    if attempt is None:
        lines = ["DEBRIEF: the game is over (%s at turn %s). Turn what it taught you into lessons for next time." % (result, turn)]
    else:
        lines = ["DEBRIEF of an earlier attempt, %s (%s at turn %s%s). Turn what it taught you into lessons for next time; "
                 "answer with record_debrief(attempt=\"%s\", ...), or decline=true if it taught nothing."
                 % (attempt, result, turn, "; the bridge never saw how it ended" if result == "abandoned" else "", attempt)]
    lv = mem.level or {}
    lines.append("LEVEL: %s (%s)" % (lv.get("name") or "?", lv.get("key") or "?"))
    if (final or {}).get("objective"):
        lines.append("OBJECTIVE: " + final["objective"])
    chain = store.attempt_chain(mem.game_id, mem.branch) if mem.game_id else []
    if chain:
        lines.append("EARLIER ATTEMPTS AT THIS LEVEL, BEFORE THIS ONE (oldest first):")
        for r in chain:
            lines.append("  %s at turn %s, %s decisions%s" % (r["result"] or "unfinished", r["end_turn"], r["decisions"],
                                                            (": " + r["summary"]) if r.get("summary") else ""))
    cps = mem.checkpoints
    if cps:
        step = max(1, len(cps) // 6)
        rows = cps[::step][-6:]
        if rows[-1] is not cps[-1]:
            rows.append(cps[-1])
        lines.append("YOUR DUNGEON OVER THE GAME (one row per few pay days):")
        for c in rows:
            lines.append("  turn %s: gold %s, fighters %s (score %s), workers %s, rooms %s%s, visible enemy score %s" % (
                c.get("turn"), c.get("gold"), c.get("fighters"), c.get("score"), c.get("workers"),
                sum((c.get("rooms") or {}).values()), (", heart %s%%" % c["heart"]) if c.get("heart") is not None else "", c.get("enemy_score")))
    if hasattr(mem, "render_chapters"):
        chapters = mem.render_chapters()
        if chapters:
            lines.append("YOUR GAME, CHAPTER BY CHAPTER (each pay day before the last decisions):")
            lines += chapters[1:]
    st = mem.stats or {}
    refusals = sorted((st.get("refusals") or {}).items(), key=lambda kv: -kv[1])[:5]
    lines.append("ORDERS: %s decisions, %s orders sent, %s refused%s" % (
        st.get("decisions", 0), st.get("sent", 0), st.get("refused", 0),
        (" (most often: " + ", ".join("%s x%d" % kv for kv in refusals) + ")") if refusals else ""))
    if (final or {}).get("events"):
        lines.append("LAST EVENTS: " + " | ".join(final["events"]))
    lines.append("YOUR FINAL PLAN: " + (mem.plan or "(none)"))
    lines.append("YOUR FINAL NOTES AND FACTS: " + (_notes_of(mem).replace("\n", " | ") or "(none)"))
    lines.append("")
    shown = _recall_state(mem)["shown"] if attempt is None else {}
    lines.append("CURRENT LESSONS, every one (id, topic, how often a debrief confirmed it%s):" % (
        ", how often this game showed it" if shown else ""))
    for scope, head in scopes_for(mem, opponents):
        items = store.items(scope)
        lines.append("  %s [scope %s; %d shown in play]:%s" % (head, short_scope(scope), len(pick_items(items, cap_for(scope))),
                                                               "" if items else " (none yet)"))
        for i in items:
            lines += _with_shown(item_lines(i, detail=True), shown.get(str(i["id"])))
    noted = store.items(scopes=[s for s, _ in scopes_for(mem, opponents)], status="provisional")
    if noted:
        lines.append("LESSONS NOTED DURING PLAY, NOT KEPT YET (confirm to keep, revise, or retire; unmentioned they wait for the next debrief):")
        for i in noted:
            src = " [a tip from a human player]" if i["source"] == "user" else ""
            lines += _with_shown(item_lines(i, detail=True), shown.get(str(i["id"])))
            lines[-1] += " (scope %s)%s" % (short_scope(i["scope"]), src)
    lines.append("")
    lines.append("Call record_debrief with: summary (what happened and why, max %d chars); confirm [ids of lessons this game "
                 "bore out]; revise [{id, text}] (a better wording, or a correction); retire [{id, why}] (proved wrong, or no "
                 "longer true); add [{scope, text, topic?}] (new lessons, one idea each, max %d chars; scope playbook, campaign, "
                 "level or opponent:<identity> as listed above; topic one of %s). A lesson you do not mention stays as it is. "
                 "Keep only what you would tell yourself before playing again: concrete, checked against what happened, short. "
                 "Say what to do, not just what went wrong." % (SUMMARY_CAP, ITEM_CAP, ", ".join(TOPICS)))
    return "\n".join(lines)


def _with_shown(lines, seen):
    if seen and lines:
        lines[0] = lines[0].replace("] (", "] (shown %dx, " % seen[0], 1) if "] (" in lines[0] else lines[0]
    return lines


def short_scope(scope):
    """The name an agent uses for a scope in a debrief (playbook, campaign, level, opponent:<identity>)."""
    kind = scope_kind(scope)
    return {"general": "playbook", "campaign": "campaign", "level": "level"}.get(kind, scope)


def _scope_named(name, mem, opponents):
    """The scope an agent's name for one means in this game (None: none of this game's)."""
    lv = mem.level or {}
    name = (name or "playbook").strip()
    allowed = {s for s, _ in scopes_for(mem, opponents)}
    if name in ("playbook", "general"):
        return "general"
    if name == "campaign" and lv.get("campaign") and lv["campaign"] != "?":
        return "campaign:" + lv["campaign"]
    if name == "level" and lv.get("key"):
        return "level:" + lv["key"]
    if not name.startswith(("opponent:", "campaign:", "level:")) and ("opponent:" + name) in allowed:
        return "opponent:" + name
    return name if name in allowed else None


def _item_id(v):
    if isinstance(v, bool):
        return None
    if isinstance(v, int):
        return v
    if isinstance(v, str) and v.strip().upper().lstrip("L").isdigit():
        return int(v.strip().upper().lstrip("L"))
    return None


def _scope_order(mem, opponents):
    return [s for s, _ in scopes_for(mem, opponents)]


def run_debrief(store, mem, recorder, result, final_view, policy, ctx):
    """For the autonomous loops (bridge.py, agent_vs_agent.py): the policy's debrief of a finished game, stored. Returns
    what apply_debrief returned, or None when there was nothing to do."""
    if store is None or not store.enabled or not hasattr(policy, "debrief"):
        return None
    text = debrief_text(store, mem, result, final_from_view(final_view), recorder.opponents)
    try:
        out = policy.debrief(text, ctx) or {}
    except Exception as e:  # noqa: BLE001 - a failed debrief loses the lessons, never the game record
        print("debrief failed: %s" % e, file=sys.stderr)
        return None
    return apply_debrief(store, mem, out, recorder.opponents)


def run_earlier_debriefs(store, recorder, mem, policy, ctx, limit=2):
    """For the autonomous loops, at the start of a game: the policy debriefs up to `limit` of this agent's earlier
    attempts that never had one (abandoned ones included). Returns [(attempt ref, apply_debrief's result)]."""
    if store is None or not store.enabled or not hasattr(policy, "debrief"):
        return []
    out = []
    for row in store.pending_debriefs(recorder.agent, (mem.level or {}).get("key"), limit=limit):
        old, opps, final = attempt_memory(store, row)
        ref = attempt_ref(row)
        try:
            edits = policy.debrief(debrief_text(store, old, row["result"], final, opps, attempt=ref), ctx) or {}
        except Exception as e:  # noqa: BLE001
            print("debrief of %s failed: %s" % (ref, e), file=sys.stderr)
            store.note_offered([row])
            continue
        out.append((ref, apply_debrief(store, old, edits, opps)))
    return out


def attempt_memory(store, row):
    """An earlier attempt as debrief_text / apply_debrief want it: (a Memory holding its record, its opponents, its
    final). The plan and notes are the last ones the record kept."""
    from memory import Memory
    m = Memory()
    m.game_id, m.branch, m.parent_branch = row["game_id"], row["branch"], row["parent_branch"]
    m.level = {"key": row["level_key"], "campaign": row["campaign"], "number": row["level_number"], "name": row["level_name"]}
    m.start_turn = row["start_turn"]
    m.plan, m.notes = row["final_plan"] or "", row["final_notes"] or ""
    m.checkpoints = store.checkpoints(row["game_id"], row["branch"])
    m.stats = {"decisions": row["decisions"] or 0, "sent": row["orders_sent"] or 0, "refused": row["orders_refused"] or 0,
               "refusals": json.loads(row["refusals_by_error"] or "{}")}
    try:
        opps = json.loads(row["opponents"] or "[]")
    except ValueError:
        opps = []
    try:
        final = json.loads(row["final"] or "{}")
    except ValueError:
        final = {}
    if not final.get("checkpoint") and m.checkpoints:
        final["checkpoint"] = m.checkpoints[-1]
    final["end_turn"] = row["end_turn"]
    return m, opps, final


_LEGACY = (("playbook", "general"), ("campaign_lessons", "campaign"), ("level_lessons", "level"))


def apply_debrief(store, mem, edits, opponents=None):
    """Stores a debrief (record_debrief's fields): the summary on the attempt's record; `confirm` [ids], `revise`
    [{id, text}], `retire` [{id, why}] and `add` [{scope, text, topic?}] on the lessons of this game's scopes; and the
    older whole-text fields (playbook, campaign_lessons, level_lessons, opponent_lessons {identity: text}), each matched
    against its scope (replace_scope_text: what it leaves out is retired, and listed in `dropped`). Opponent lessons are
    accepted only for opponents of this game. Marks the attempt debriefed. Returns {"stored": [summary and the scopes
    changed], "added", "confirmed", "revised" [[old, new]], "retired", "dropped" [{id, text, scope}], "warnings"}."""
    out = {"stored": [], "added": [], "confirmed": [], "revised": [], "retired": [], "dropped": [], "warnings": []}
    if store is None or not store.enabled:
        out["warnings"].append("no experience store: nothing was kept")
        return out
    edits = edits or {}
    g, b = mem.game_id, mem.branch
    touched = []
    order = _scope_order(mem, opponents)
    mine = {i["id"]: i for i in store.items(scopes=order, status=LIVE)} if order else {}

    def own(raw, what):
        item_id = _item_id(raw)
        if item_id is None or item_id not in mine:
            out["warnings"].append("%s: %r is not one of this game's lessons (ids as the debrief lists them)" % (what, raw))
            return None
        return item_id

    if edits.get("summary"):
        store.set_summary(g, b, edits["summary"])
        out["stored"].append("summary")
    for raw in edits.get("confirm") or []:
        item_id = own(raw, "confirm")
        if item_id is not None and store.confirm_item(item_id, g, b):
            out["confirmed"].append(item_id)
    for r in edits.get("revise") or []:
        item_id = own((r or {}).get("id"), "revise")
        if item_id is None:
            continue
        new_id, warn = store.revise_item(item_id, r.get("text"), g, b, topic=r.get("topic"))
        if warn:
            out["warnings"].append(warn)
        if new_id is not None:
            out["revised"].append([item_id, new_id])
            touched.append(mine[item_id]["scope"])
    for r in edits.get("retire") or []:
        r = r if isinstance(r, dict) else {"id": r}
        item_id = own(r.get("id"), "retire")
        if item_id is not None and store.retire_item(item_id, r.get("why") or "", g, b):
            out["retired"].append(item_id)
            touched.append(mine[item_id]["scope"])
    for r in edits.get("add") or []:
        r = r if isinstance(r, dict) else {"text": r}
        scope = _scope_named(r.get("scope"), mem, opponents)
        if scope is None:
            out["warnings"].append("add: no scope %r in this game; that lesson was not kept" % r.get("scope"))
            continue
        item_id, warn = store.add_item(scope, r.get("text") or "", r.get("topic"), "debrief", g, b)
        if warn:
            out["warnings"].append(warn)
        if item_id is not None:
            out["added"].append(item_id)
            touched.append(scope)
    wanted = []
    for key, name in _LEGACY:
        if edits.get(key) is not None:
            scope = _scope_named(name, mem, opponents)
            if scope is not None:
                wanted.append((scope, edits[key]))
    known = {opponent_identity(p) for p in opponents or []} - {None}
    for ident, text in (edits.get("opponent_lessons") or {}).items():
        ident = ident[len("opponent:"):] if ident.startswith("opponent:") else ident
        if ident in known:
            wanted.append(("opponent:" + ident, text))
        else:
            out["warnings"].append("no opponent %r in this game; its lessons were not kept" % ident)
    for scope, text in wanted:
        if text is None:
            continue
        got = store.replace_scope_text(scope, text.strip(), g, b)
        out["warnings"] += got["warnings"]
        out["added"] += got["added"] + got["restored"]
        out["revised"] += got["revised"]
        out["dropped"] += [dict(d, scope=scope) for d in got["dropped"]]
        touched.append(scope)
    if out["dropped"]:
        out["warnings"].append("%d lesson(s) your rewrite left out were retired: %s. To keep one, add it again (or prefer "
                               "add/revise/retire next time: a lesson not mentioned is kept)." % (
                                   len(out["dropped"]), "; ".join("L%d %s" % (d["id"], d["text"][:60]) for d in out["dropped"])))
    out["stored"] += [s for s in order if s in touched] + sorted(set(touched) - set(order))
    store.set_debrief_state(g, b, "done")
    return out
