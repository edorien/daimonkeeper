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
- **lessons**: text the model rewrites when a game ends, per scope -- `general` (the playbook), `campaign:<name>`,
  `level:<campaign>:<n>`, `opponent:<identity>` -- with every version kept in lessons_history.
"""
import json
import os
import sqlite3
import sys
import time

SCHEMA_VERSION = 1
SCOPE_CAPS = {"general": 3000, "campaign": 1500, "level": 1000, "opponent": 800}
SUMMARY_CAP = 600

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
]


def default_path():
    """experience.sqlite in the bridge's own folder (the game's mcp/ folder once installed), where a person can see it --
    not a hidden per-user directory. $KEEPERFX_EXPERIENCE overrides it (the tests use that to stay out of the tree)."""
    return os.environ.get("KEEPERFX_EXPERIENCE") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "experience.sqlite")


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
        self.db = sqlite3.connect(self.path, timeout=10.0, isolation_level=None)
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
            # One script, one transaction: a migration either lands whole or not at all. (executescript commits any
            # open transaction first, so the BEGIN has to be inside it.)
            self.db.executescript("BEGIN IMMEDIATE;" + _SCHEMA[v] +
                                  "INSERT OR REPLACE INTO meta (key, value) VALUES ('schema_version', '%d'); COMMIT;" % (v + 1))

    # ---- game records ----------------------------------------------------------------------------------------------------
    def branch(self, game_id, branch):
        """The attempt's row as a dict, or None when this store has never seen it."""
        if self.db is None:
            return None
        row = self.db.execute("SELECT * FROM games WHERE game_id=? AND branch=?", (game_id, branch)).fetchone()
        return dict(row) if row else None

    def open_branch(self, mem, player, agent=None, opponents=None, turn=None):
        """Records that this attempt is being played (nothing if it is already recorded)."""
        if self.db is None:
            return
        lv = mem.level or {}
        now = time.time()
        self.db.execute(
            "INSERT OR IGNORE INTO games (game_id, branch, parent_branch, level_key, level_name, campaign, level_number, player,"
            " opponents, agent, started_at, updated_at, start_turn, end_turn) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (mem.game_id, mem.branch, mem.parent_branch, lv.get("key"), lv.get("name"), lv.get("campaign"), lv.get("number"),
             player, json.dumps(opponents or []), agent, now, now, turn if turn is not None else mem.start_turn,
             turn if turn is not None else mem.start_turn))

    def progress(self, mem, turn, opponents=None):
        """The attempt has reached `turn`: its counts and latest checkpoints, so an attempt the bridge never got to close
        (killed, or the game quit under it) still says how far it went."""
        if self.db is None:
            return
        st = mem.stats
        args = [time.time(), turn, st.get("decisions", 0), st.get("sent", 0), st.get("refused", 0), json.dumps(st.get("refusals", {}))]
        extra = ""
        if opponents is not None:
            extra = ", opponents=?"
            args.append(json.dumps(opponents))
        self.db.execute("UPDATE games SET updated_at=?, end_turn=?, decisions=?, orders_sent=?, orders_refused=?, refusals_by_error=?"
                        + extra + " WHERE game_id=? AND branch=? AND result IS NULL", args + [mem.game_id, mem.branch])
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
            (result, time.time(), turn, json.dumps(final or {}), mem.plan, mem.notes, mem.game_id, mem.branch))
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
        """The finished games on a level (won / lost / quit; not suspended or reloaded attempts), oldest first."""
        if self.db is None:
            return []
        rows = self.db.execute(
            "SELECT result, end_turn, start_turn, ended_at FROM games WHERE level_key=? AND result IN ('won','lost','quit') "
            "ORDER BY ended_at DESC LIMIT ?", (level_key, limit)).fetchall()
        return [dict(r) for r in reversed(rows)]

    # ---- lessons ---------------------------------------------------------------------------------------------------------
    def lessons(self, scope):
        if self.db is None:
            return ""
        row = self.db.execute("SELECT text FROM lessons WHERE scope=?", (scope,)).fetchone()
        return row["text"] if row else ""

    def set_lessons(self, scope, text, game_id=None):
        """Replaces a scope's lessons (cut to its cap; empty text clears them). Every version goes into the history.
        Returns a warning string if the text was cut."""
        if self.db is None:
            return ""
        cap = cap_for(scope)
        warn = ""
        if len(text) > cap:
            warn = "%s lessons cut to %d characters; compress them" % (scope, cap)
            text = text[:cap]
        now = time.time()
        self.db.execute("BEGIN IMMEDIATE")
        try:
            if text:
                self.db.execute("INSERT OR REPLACE INTO lessons (scope, text, updated_at, game_id) VALUES (?,?,?,?)", (scope, text, now, game_id))
            else:
                self.db.execute("DELETE FROM lessons WHERE scope=?", (scope,))
            self.db.execute("INSERT INTO lessons_history (scope, text, updated_at, game_id) VALUES (?,?,?,?)", (scope, text, now, game_id))
            self.db.execute("COMMIT")
        except Exception:
            self.db.execute("ROLLBACK")
            raise
        return warn

    def lessons_history(self, scope):
        if self.db is None:
            return []
        return [dict(r) for r in self.db.execute(
            "SELECT id, text, updated_at, game_id FROM lessons_history WHERE scope=? ORDER BY id", (scope,))]

    def rollback_lessons(self, scope, history_id):
        """Makes an earlier version (a lessons_history id) the current one again, as a new version."""
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
        if self.db is None:
            return []
        return [r["scope"] for r in self.db.execute("SELECT scope FROM lessons ORDER BY scope")]


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
    heart = None
    for r in own.get("rooms", []):
        rooms[r["kind"]] = rooms.get(r["kind"], 0) + 1
        if r["kind"] == "DUNGEON_HEART" and r.get("max_health"):
            heart = round(100 * r.get("health", 0) / r["max_health"])
    enemy = sum(f.get("fighter_score", 0) for o, f in (view.get("visible", {}).get("force_by_owner") or {}).items()
                if str(o) != str(view.get("seat", {}).get("player")))
    cp = {"turn": view.get("turn"), "gold": own.get("gold"), "creatures": len(own.get("creatures", [])),
          "fighters": force.get("fighters"), "workers": force.get("workers"), "score": force.get("fighter_score"),
          "rooms": rooms, "enemy_score": enemy}
    if heart is not None:
        cp["heart"] = heart
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

    def decision(self, mem, view, ctx=None):
        """Before each decision: a checkpoint when a pay day has passed, and the attempt's progress. With a policy's `ctx`
        it also puts the EXPERIENCE text there for the policy to show (in full once per attempt, then one line)."""
        self.opponents = opponents_from_view(view, self.player) or self.opponents
        observe(mem, view)
        self.store.progress(mem, view.get("turn"), self.opponents)
        if ctx is not None:
            ctx["experience"] = render_experience(self.store, mem, self.opponents, full=not self.shown)
            self.shown = True

    def decided(self, mem, turn):
        """After a decision is recorded: its counts reach the record at once, not with the next decision."""
        self.store.progress(mem, turn)

    def reloaded(self, old_mem, reached, new_mem, view):
        """A save was loaded: the attempt that was being played ends there, and the loaded one begins."""
        self.shown = False
        if old_mem is not None:
            self.store.close_branch(old_mem, "reloaded", reached)
        self.store.open_branch(new_mem, self.player, self.agent, self.opponents, turn=view.get("turn"))

    def ended(self, mem, view, result):
        """The seat won or lost: the attempt is over. Returns True if this closed it (False if already closed)."""
        observe(mem, view)
        return self.store.close_branch(mem, result, view.get("turn"), final_from_view(view))

    def left(self, mem, view, last_save_turn):
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
        self.store.close_branch(mem, result, turn, final_from_view(view))
        return result, debrief


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
    for r in store.db.execute("SELECT result, opponents FROM games WHERE result IN ('won','lost','quit') ORDER BY ended_at DESC LIMIT ?", (limit,)):
        try:
            opps = json.loads(r["opponents"] or "[]")
        except ValueError:
            continue
        if any(opponent_identity(p) == ident for p in opps):
            counts[r["result"]] = counts.get(r["result"], 0) + 1
    return counts


def render_experience(store, mem, opponents, full=True):
    """The EXPERIENCE section: in full (the first decision of an attempt), else one line. '' when there is no store."""
    if store is None or not store.enabled:
        return ""
    lv = mem.level or {}
    record = store.level_record(lv.get("key")) if lv.get("key") else []
    lessons = [(scope, head, store.lessons(scope)) for scope, head in scopes_for(mem, opponents)]
    have = [x for x in lessons if x[2]]
    if not full:
        won = sum(1 for r in record if r["result"] == "won")
        return ("EXPERIENCE: %d earlier game(s) on this level (%d won); %d lesson set(s) shown at the start; get_experience shows "
                "them again." % (len(record), won, len(have)))
    lines = ["EXPERIENCE (your own lessons from earlier games; carry what applies into your plan):"]
    lines.append("  This level: " + ("%d earlier game(s): %s" % (len(record), _record_line(record)) if record else "never played before"))
    for p in opponents or []:
        ident = opponent_identity(p)
        if ident:
            rec = opponent_record(store, ident)
            if rec:
                lines.append("  vs %s: %s" % (opponent_label(p), ", ".join("%d %s" % (n, res) for res, n in sorted(rec.items()))))
    if not have:
        lines.append("  No lessons yet: you will write them when this game ends.")
    for scope, head, text in have:
        lines.append("  %s:" % head)
        lines += ["    " + l for l in text.splitlines() if l.strip()]
    return "\n".join(lines)


def debrief_text(store, mem, result, final, opponents):
    """What the model is shown when a game ends, and asked to turn into lessons."""
    turn = ((final or {}).get("checkpoint") or {}).get("turn")
    lines = ["DEBRIEF: the game is over (%s at turn %s). Turn what it taught you into lessons for next time." % (result, turn)]
    lv = mem.level or {}
    lines.append("LEVEL: %s (%s)" % (lv.get("name") or "?", lv.get("key") or "?"))
    if (final or {}).get("objective"):
        lines.append("OBJECTIVE: " + final["objective"])
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
    st = mem.stats or {}
    refusals = sorted((st.get("refusals") or {}).items(), key=lambda kv: -kv[1])[:5]
    lines.append("ORDERS: %s decisions, %s orders sent, %s refused%s" % (
        st.get("decisions", 0), st.get("sent", 0), st.get("refused", 0),
        (" (most often: " + ", ".join("%s x%d" % kv for kv in refusals) + ")") if refusals else ""))
    if (final or {}).get("events"):
        lines.append("LAST EVENTS: " + " | ".join(final["events"]))
    lines.append("YOUR FINAL PLAN: " + (mem.plan or "(none)"))
    lines.append("YOUR FINAL NOTES: " + (mem.notes or "(none)"))
    lines.append("")
    lines.append("CURRENT LESSONS (rewrite each one you have something to add to or correct; omit one to keep it as it is):")
    for scope, head in scopes_for(mem, opponents):
        lines.append("  %s [%s, max %d chars]: %s" % (head, scope, cap_for(scope), store.lessons(scope) or "(empty)"))
    lines.append("")
    lines.append("Call record_debrief with: summary (what happened and why, max %d chars); playbook (lessons for any level); "
                 "campaign_lessons (this campaign's rules and creatures); level_lessons (this map, its objective, its enemies); "
                 "opponent_lessons ({identity: text} for the opponents above, by the identity in brackets after 'opponent:'). "
                 "Keep only what you would tell yourself before playing again: concrete, checked against what happened, short. "
                 "Drop what this game proved wrong. Say what to do, not just what went wrong." % SUMMARY_CAP)
    return "\n".join(lines)


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
    return apply_debrief(store, mem, out.get("summary"), out.get("playbook"), out.get("campaign_lessons"),
                         out.get("level_lessons"), out.get("opponent_lessons"), recorder.opponents)


def apply_debrief(store, mem, summary=None, playbook=None, campaign_lessons=None, level_lessons=None, opponent_lessons=None,
                  opponents=None):
    """Stores a debrief: the summary on the attempt's record, and each lesson text given under its scope. Returns
    {"stored": [scopes], "warnings": [...]}. Opponent lessons are accepted only for opponents of this game."""
    if store is None or not store.enabled:
        return {"stored": [], "warnings": ["no experience store: nothing was kept"]}
    lv = mem.level or {}
    stored, warnings = [], []
    if summary:
        store.set_summary(mem.game_id, mem.branch, summary)
        stored.append("summary")
    wanted = [("general", playbook)]
    if lv.get("campaign") and lv["campaign"] != "?":
        wanted.append(("campaign:" + lv["campaign"], campaign_lessons))
    if lv.get("key"):
        wanted.append(("level:" + lv["key"], level_lessons))
    known = {opponent_identity(p) for p in opponents or []} - {None}
    for ident, text in (opponent_lessons or {}).items():
        ident = ident[len("opponent:"):] if ident.startswith("opponent:") else ident
        if ident in known:
            wanted.append(("opponent:" + ident, text))
        else:
            warnings.append("no opponent %r in this game; its lessons were not kept" % ident)
    for scope, text in wanted:
        if text is None:
            continue
        warn = store.set_lessons(scope, text.strip(), mem.game_id)
        if warn:
            warnings.append(warn)
        stored.append(scope)
    return {"stored": stored, "warnings": warnings}
