"""What the agent remembers between decisions, so it can keep one coherent plan for a whole game.

Four layers: a `plan` and `notes` the model rewrites itself (long-lived: strategy, what it learned about the opponent and the
map, things it must not forget), a rolling log of recent decisions with what became of each order, bookkeeping (which creature
kinds it has already been told about), and a stable name per creature (kind #n, assigned once and kept for that creature's
whole life) so the model can say "Orc #3" consistently instead of juggling numeric thing_ids turn to turn -- orders may give
`name` in place of `thing_id` (prompt.order_to_request resolves it). Sizes are capped so the prompt stays bounded however long
the game runs; the model is told the caps and is expected to compress its own notes.

**Where it lives: in the game** (docs/refactor/AI/omissions/09-persistent-memory.md section 5). The engine keeps one opaque
text per seat (API set_agent_memory / get_agent_memory) and writes it into the save file, so a loaded save brings back the plan
exactly as it stood when the game was saved, a bridge that restarts picks up where it left off, and a new level starts with
nothing. `from_engine` / `bind_engine` connect a Memory to its seat; `flush` pushes it when it changed.

Each game has a `game_id` (made when the engine held no memory for the seat, i.e. a new game) and the level it is on. Each
attempt at it has a `branch`: a load starts a new one whose `parent_branch` is the attempt the save was made in, which is how
the experience store (experience.py) tells one game's attempts apart.

Format 2 (docs/plans/llm-bridge-memory.md, Phases E-F):
- **facts**: small things to remember, each set or dropped on its own (`facts {key: text}` on submit_orders, null drops
  one) instead of rewriting all the notes; each stamped with the turn it was set, marked (old) after a few pay days.
  The free-text `notes` still work.
- **chapters**: the decisions that roll off the recent log are folded into one chapter per pay day (turns, decisions,
  orders, the reasons that came up, the plan's first line, the agent's own line for it: `chapter_note`), at most
  CHAPTERS_MAX, the oldest merged in pairs -- the long game's story, not just its last 12 decisions.
- **places** (places.py): named spots the bridge keeps from the view itself (the heart, rooms, hero gates, enemy hearts
  once seen) and the ones the agent names; orders may give a place's name as pos, slab_rect or to_room.
- A format-1 blob is upgraded when read (it gains the empty new fields). A blob of a newer format is left alone: this
  bridge plays on a fresh memory and never writes over it (`attach`).
"""
import json
import sys
import time
import uuid

from api import ApiError

PLAN_CAP = 1800
NOTES_CAP = 3500
RECENT = 12
FORMAT = 2
READS = (1, 2)            # formats from_blob upgrades
FACT_CAP = 200
FACTS_MAX = 40
FACT_KEY_CAP = 32
FACT_OLD_PAYDAYS = 5      # a fact set this many pay days ago is marked (old)
CHAPTERS_MAX = 10
CHAPTER_NOTE_CAP = 200
BLOB_CAP = 60000          # the engine refuses a text over 65536 bytes (agent_memory.h AGENT_MEMORY_MAX)
NAMES_PRUNE_AT = 400      # past this many names, those of creatures no longer in the dungeon are dropped
CHECKPOINTS_CAP = 120     # past this, every other old checkpoint is dropped (a coarser history, never a gap at the end)

_FIELDS = ("format", "game_id", "level", "started_at", "start_turn", "branch", "parent_branch", "plan", "notes", "decisions", "seen_kinds",
           "count", "creature_names", "_kind_counters", "checkpoints", "stats", "rule_names", "rule_sigs", "pending_rules",
           "last_sight", "bank_names", "triggers", "trigger_log", "rally", "recall", "facts", "chapters", "chapter_notes",
           "places")


class NewerFormat(Exception):
    """The game holds a memory blob a newer bridge wrote: this one must not read or overwrite it."""


def creature_key(thing_id, born=None):
    """A creature's identity: its thing id plus the turn it came into being, so a later creature reusing the same thing slot
    is a different creature."""
    return "%d@%d" % (thing_id, born) if born is not None else str(thing_id)


class Memory:
    def __init__(self):
        self.format = FORMAT
        self.game_id = None
        self.level = {}              # {"key", "name", "campaign", "number"}
        self.started_at = None       # wall clock, seconds
        self.start_turn = None
        self.branch = None           # this attempt at the game: a new one after every reload (experience.py)
        self.parent_branch = None    # the attempt the loaded save was made in
        self.plan = ""
        self.notes = ""
        self.decisions = []          # newest last: {"n","turn","reasons","reasoning","orders":[{"id","verb","summary","status"}]}
        self.seen_kinds = []         # creature kinds whose profile the model has already been shown
        self.count = 0
        self.creature_names = {}     # {creature_key: name}, e.g. {"27@1400": "Orc #3"}
        self._kind_counters = {}     # {kind: next index to assign}
        self.checkpoints = []        # [{"turn", ...stats}], one per pay day (experience.py)
        self.stats = {"decisions": 0, "sent": 0, "refused": 0, "refusals": {}}   # the whole game's, for its record
        self.rule_names = []         # the combat assistant's rules by slot: the agent's own name for each (set_combat_rules)
        self.rule_sigs = []          # by slot: the rule each name was given to (standing_orders.rule_sig), None if unchecked
        self.pending_rules = None    # a change accepted but not live yet: {"names", "before" (live sigs then), "id"}
        self.last_sight = None       # the latest Sight of Evil report (prompt.render_sight): {"turn", "lines"}, kept once it closes
        self.bank_names = {}         # the combat assistant's banks: {"1": {"label", "names"}} (set_combat_rules(bank=N))
        self.triggers = []           # the agent's contingency triggers, with their counters (triggers.py)
        self.trigger_log = []        # their latest firings, newest last (triggers.LOG_KEPT)
        self.rally = {}              # named spots (subtiles) triggers and the flash view refer to: {"safe": [x, y], ...}
        self.recall = {}             # which lessons this attempt was shown, and when (experience.py, Phase C): {"shown":
                                     # {id: [times, decision]}, "topics": {topic: decision}, "refresh": decision, "tip_turn"}
        self.facts = {}              # {key: {"text", "turn"}}: small facts, set one at a time
        self.chapters = []           # the game before the recent log, one per pay day: {"period", "from", "to", "decisions",
                                     # "sent", "refused", "reasons": {reason: n}, "plan", "note"}
        self.chapter_notes = {}      # {period (str): the agent's line for that pay day}, until its decisions are folded
        self.places = {}             # places.py: {name: {"pos", "rect"?, "kind", "room"?, "owner"?, "seen"}}
        self._assist_note = None     # said once in STANDING ORDERS (a change refused) (not saved)
        self._assist_seen = {}       # each rule's [fired, spent] at the last decision, for STANDING ORDERS (not saved)
        self._trig_seen = None       # the turn TRIGGERS was last shown: firings after it are news (not saved)
        self._live = None            # keys of the creatures in the latest view (not saved)
        self._sink = None            # where flush() sends the blob
        self._pushed = None

    # ---- a new game, a blob, the engine ----------------------------------------------------------------------------------
    @classmethod
    def new_game(cls, view=None):
        m = cls()
        m.game_id = uuid.uuid4().hex
        m.branch = new_branch_id()
        m.started_at = time.time()
        if view is not None:
            m.start_turn = view.get("turn")
            m.level = level_of(view)
        return m

    def start_branch(self):
        """This memory came back from a save: from here on it is a new attempt at the game, begun from that save."""
        self.parent_branch = self.branch
        self.branch = new_branch_id()

    def to_blob(self):
        d = {k: getattr(self, k) for k in _FIELDS}
        blob = json.dumps(d, separators=(",", ":"))
        # Over the cap, lose the oldest history first; never the plan, the notes or the names.
        while len(blob.encode()) > BLOB_CAP and (d["decisions"] or d["checkpoints"]):
            if len(d["checkpoints"]) > 8 or not d["decisions"]:
                d["checkpoints"] = d["checkpoints"][1:]
            else:
                d["decisions"] = d["decisions"][1:]
            blob = json.dumps(d, separators=(",", ":"))
        return blob

    @classmethod
    def from_blob(cls, text):
        """The Memory a blob holds, or None when there is none or it is not ours (another format, unreadable)."""
        if not text:
            return None
        try:
            d = json.loads(text)
        except ValueError:
            return None
        if not isinstance(d, dict) or not d.get("game_id"):
            return None
        if isinstance(d.get("format"), int) and d["format"] > FORMAT:
            raise NewerFormat("memory format %d (this bridge writes %d)" % (d["format"], FORMAT))
        if d.get("format") not in READS:
            return None
        m = cls()
        for k in _FIELDS:
            if k in d and k != "format":
                setattr(m, k, d[k])
        m.format = FORMAT          # format 1 had none of the new fields: they start empty
        m._pushed = text
        return m

    @classmethod
    def from_engine(cls, api, seat):
        """The seat's memory as the game holds it, or None (a new game, or an engine without agent memory). Raises
        NewerFormat for a blob a newer bridge wrote."""
        try:
            data = api.data(action="get_agent_memory", player=seat)
        except ApiError as e:
            print("the game does not keep agent memory (%s); the plan will not survive a save or a restart" % e.code,
                  file=sys.stderr)
            return None
        return cls.from_blob((data or {}).get("data"))

    def bind_engine(self, api, seat):
        """From now on flush() stores this memory in the game, for the seat."""
        def sink(blob):
            try:
                api.data(action="set_agent_memory", player=seat, data=blob)
            except ApiError as e:
                print("could not store agent memory: %s" % e.code, file=sys.stderr)
        self._sink = sink
        return self

    def flush(self):
        """Stores the memory where it lives, if it changed since the last time."""
        if self._sink is None:
            return
        blob = self.to_blob()
        if blob != self._pushed:
            self._sink(blob)
            self._pushed = blob

    # ---- what the model writes -------------------------------------------------------------------------------------------
    def update(self, plan=None, notes=None, facts=None, chapter_note=None, turn=None):
        """Replace the plan and/or notes (only what the model sent); set or drop single facts (`facts` {key: text}, a
        null or empty text drops it); the agent's line for the current pay day (`chapter_note`). `turn`: the decision's,
        stamped on the facts set. Returns a warning string if something was cut or refused."""
        cut = []
        if facts:
            cut += self.set_facts(facts, turn)
        if chapter_note:
            self.chapter_notes[str(self.period_of(turn))] = chapter_note.strip()[:CHAPTER_NOTE_CAP]
            if len(chapter_note.strip()) > CHAPTER_NOTE_CAP:
                cut.append("chapter_note cut to %d characters" % CHAPTER_NOTE_CAP)
        if plan is not None:
            self.plan = plan[:PLAN_CAP]
            if len(plan) > PLAN_CAP:
                cut.append("plan cut to %d characters" % PLAN_CAP)
        if notes is not None:
            self.notes = notes[:NOTES_CAP]
            if len(notes) > NOTES_CAP:
                cut.append("notes cut to %d characters; compress them" % NOTES_CAP)
        return "; ".join(cut)

    def set_facts(self, facts, turn=None):
        """Sets or drops facts ({key: text}; null or "" drops). Returns the warnings (cut, refused)."""
        warns = []
        if not isinstance(facts, dict):
            return ["facts is an object {key: text}"]
        for raw, text in facts.items():
            key = "_".join(str(raw).strip().split())[:FACT_KEY_CAP]
            if not key:
                continue
            if text is None or not str(text).strip():
                self.facts.pop(key, None)
                continue
            text = str(text).strip()
            if key not in self.facts and len(self.facts) >= FACTS_MAX:
                warns.append("fact %r not kept: %d facts already (drop one with null)" % (key, FACTS_MAX))
                continue
            if len(text) > FACT_CAP:
                warns.append("fact %r cut to %d characters" % (key, FACT_CAP))
            self.facts[key] = {"text": text[:FACT_CAP], "turn": turn}
        return warns

    def period_of(self, turn):
        """The pay day a turn falls in, as the turn of the checkpoint that opened it (0 before the first)."""
        start = 0
        for cp in self.checkpoints:
            t = cp.get("turn")
            if t is not None and turn is not None and t <= turn:
                start = t
        return start

    def _fold(self, d):
        """A decision leaving the recent log goes into its pay day's chapter."""
        period = self.period_of(d.get("turn"))
        ch = self.chapters[-1] if self.chapters else None
        if ch is None or ch.get("period") != period:
            ch = {"period": period, "from": d.get("turn"), "to": d.get("turn"), "decisions": 0, "sent": 0, "refused": 0,
                  "reasons": {}, "plan": "", "note": ""}
            self.chapters.append(ch)
        ch["to"] = d.get("turn")
        ch["decisions"] += 1
        ch["sent"] += len(d.get("orders") or [])
        ch["refused"] += len(d.get("refused") or [])
        for r in d.get("reasons") or ["timer"]:
            ch["reasons"][r] = ch["reasons"].get(r, 0) + 1
        if d.get("plan1"):
            ch["plan"] = d["plan1"]
        note = self.chapter_notes.pop(str(period), None)
        if note:
            ch["note"] = note
        while len(self.chapters) > CHAPTERS_MAX:
            a, b = self.chapters[0], self.chapters[1]
            reasons = dict(a["reasons"])
            for r, n in b["reasons"].items():
                reasons[r] = reasons.get(r, 0) + n
            merged = {"period": a["period"], "from": a["from"], "to": b["to"], "decisions": a["decisions"] + b["decisions"],
                      "sent": a["sent"] + b["sent"], "refused": a["refused"] + b["refused"], "reasons": reasons,
                      "plan": b["plan"] or a["plan"], "note": "; ".join(x for x in (a["note"], b["note"]) if x)[:CHAPTER_NOTE_CAP]}
            self.chapters[:2] = [merged]

    def record_decision(self, turn, reasons, reasoning, sent, refused):
        self.count += 1
        first = (self.plan or "").strip().splitlines()
        self.decisions.append({"n": self.count, "turn": turn, "reasons": list(reasons), "reasoning": reasoning[:300],
                               "orders": [dict(o, status="queued") for o in sent], "refused": list(refused),
                               "plan1": first[0][:120] if first else ""})
        for d in self.decisions[:-RECENT]:
            self._fold(d)
        del self.decisions[:-RECENT]
        st = self.stats
        st["decisions"] = st.get("decisions", 0) + 1
        st["sent"] = st.get("sent", 0) + len(sent)
        st["refused"] = st.get("refused", 0) + len(refused)
        by_error = st.setdefault("refusals", {})
        for r in refused:
            code = r.get("error") or "?"
            by_error[code] = by_error.get(code, 0) + 1
        self.flush()

    def note_results(self, results):
        """Fill in what happened to earlier orders from the view's seat.results ([{id,status,error?}])."""
        by_id = {r["id"]: r for r in results}
        for d in self.decisions:
            for o in d["orders"]:
                r = by_id.get(o.get("id"))
                if r and o["status"] == "queued":
                    o["status"] = r["status"] + ((" " + r["error"]) if r.get("error") else "")

    def add_checkpoint(self, cp):
        """One pay day's numbers (experience.checkpoint_from_view). A second one for the same turn replaces the first."""
        if self.checkpoints and self.checkpoints[-1].get("turn") == cp.get("turn"):
            self.checkpoints[-1] = cp
        else:
            self.checkpoints.append(cp)
        if len(self.checkpoints) > CHECKPOINTS_CAP:
            old, recent = self.checkpoints[:-20], self.checkpoints[-20:]
            self.checkpoints = old[::2] + recent

    def note_live(self, creatures):
        """The creatures the seat has now ([{id, born?}]): names resolve only to these, and once there are many names
        those of creatures gone for good are dropped."""
        self._live = {creature_key(c["id"], c.get("born")) for c in creatures}
        if len(self.creature_names) > NAMES_PRUNE_AT:
            self.creature_names = {k: v for k, v in self.creature_names.items() if k in self._live}

    def name_for(self, thing_id, kind, born=None):
        """This creature's stable name, assigning one ("Kind #n") the first time it is seen."""
        key = creature_key(thing_id, born)
        name = self.creature_names.get(key)
        if name is not None:
            return name
        pretty = kind.replace("_", " ").title()
        n = self._kind_counters.get(kind, 0) + 1
        self._kind_counters[kind] = n
        name = "%s #%d" % (pretty, n)
        self.creature_names[key] = name
        return name

    def id_for(self, name):
        """The thing_id of the creature a name was given to, or None -- also None when that creature is gone (its thing
        slot may hold another creature by now). O(n) in the number of names; fine at this scale."""
        for key, val in self.creature_names.items():
            if val == name and (self._live is None or key in self._live):
                return int(key.split("@")[0])
        return None

    def rule_name(self, slot):
        """The agent's name for the combat assistant's rule in `slot` (or "rule N" for one set without a name)."""
        if 0 <= slot < len(self.rule_names) and self.rule_names[slot]:
            return self.rule_names[slot]
        return "rule %d" % slot

    def new_kinds(self, kinds):
        """The kinds in `kinds` the model has not been shown yet (and remember them)."""
        fresh = [k for k in kinds if k not in self.seen_kinds]
        self.seen_kinds.extend(fresh)
        return fresh

    # ---- what the model reads --------------------------------------------------------------------------------------------
    def notes_text(self):
        """The notes and the facts as one text (the game record's final notes)."""
        facts = ["%s = %s" % (k, f["text"]) for k, f in self.facts.items()]
        return "\n".join(x for x in ([self.notes] if self.notes else []) + facts)

    def render_facts(self, now=None, payday=1200):
        lines = ["YOUR FACTS (%d of %d; set or change one with facts {key: text} on submit_orders, null drops it; %d chars each):"
                 % (len(self.facts), FACTS_MAX, FACT_CAP)]
        if not self.facts:
            lines.append("  (none: keep things you must not forget here -- where the enemy is, what a creature is for)")
        for k, f in self.facts.items():
            t = f.get("turn")
            old = now is not None and t is not None and now - t > FACT_OLD_PAYDAYS * payday
            lines.append("  %s = %s%s" % (k, f["text"], "" if t is None else " (t%d%s)" % (t, ", old" if old else "")))
        return lines

    def render_chapters(self):
        if not self.chapters and not self.chapter_notes:
            return []
        lines = ["EARLIER IN THIS GAME (one chapter per pay day; chapter_note on submit_orders adds your line to the current one):"]
        for ch in self.chapters:
            top = sorted(ch["reasons"].items(), key=lambda kv: -kv[1])[:5]
            lines.append("  t%s-%s: %d decisions, %d orders, %d refused; %s%s%s" % (
                ch["from"], ch["to"], ch["decisions"], ch["sent"], ch["refused"],
                ", ".join("%s x%d" % kv for kv in top) or "-",
                ("; plan: " + ch["plan"]) if ch.get("plan") else "", ("; note: " + ch["note"]) if ch.get("note") else ""))
        for period, note in sorted(self.chapter_notes.items(), key=lambda kv: int(kv[0])):
            lines.append("  this pay day (from t%s), your note: %s" % (period, note))
        return lines

    def render(self, view=None):
        now, payday = None, 1200
        if view:
            import experience     # local: experience imports memory's callers, not the other way round
            now, payday = view.get("turn"), experience.payday_turns(view)
        lines = ["YOUR PLAN (rewrite it in submit_orders when it changes; max %d chars):" % PLAN_CAP, self.plan or "(none yet: make one)"]
        lines += self.render_facts(now, payday)
        if self.notes:
            lines += ["YOUR NOTES (free text, rewritten whole; facts are cheaper to keep; max %d chars):" % NOTES_CAP, self.notes]
        if view:
            import places
            lines += places.render(self, view)
        lines += self.render_chapters()
        if self.decisions:
            lines.append("RECENT DECISIONS (oldest first):")
            for d in self.decisions:
                outcome = ", ".join("%s %s" % (o.get("summary", o.get("verb")), o["status"]) for o in d["orders"]) or "no orders"
                if d["refused"]:
                    outcome += "; refused: " + ", ".join("%s %s" % (r.get("verb"), r.get("error")) for r in d["refused"])
                lines.append("  #%d turn %d (%s): %s -> %s" % (d["n"], d["turn"], ",".join(d["reasons"]) or "timer", d["reasoning"], outcome))
        return "\n".join(lines)


def new_branch_id():
    return uuid.uuid4().hex[:12]


def level_of(view):
    """The level a view is of, as the experience store keys it: {"key", "campaign", "number", "name"}."""
    lv = view.get("level") or {}
    campaign = lv.get("campaign") or "?"
    if campaign.lower().endswith(".cfg"):
        campaign = campaign[:-4]
    number = lv.get("number", 0)
    return {"key": "%s:%s" % (campaign, number), "campaign": campaign, "number": number, "name": lv.get("name", "")}


def attach(api, seat, view):
    """The seat's memory for the game in `view`, bound to the game: the one the game holds (a restart) or a new one (a new
    game). Returns (memory, resumed)."""
    try:
        mem = Memory.from_engine(api, seat)
    except NewerFormat as e:
        # A newer bridge's memory: play on a fresh one, and never write over the one the game holds.
        print("the game holds agent memory from a newer bridge (%s): playing on a fresh memory that is not saved" % e,
              file=sys.stderr)
        return Memory.new_game(view), False
    resumed = mem is not None
    if mem is None:
        mem = Memory.new_game(view)
    mem.bind_engine(api, seat)
    mem.flush()
    return mem, resumed


def reattach_after_load(api, seat, view, reached=None):
    """After a save was loaded under the agent: the memory saved with it (a new attempt at that game), or a new game's
    when the save holds none. Returns (memory, note), the note being what to tell the model once."""
    loaded = view.get("turn", 0)
    try:
        mem = Memory.from_engine(api, seat)
    except NewerFormat as e:
        return Memory.new_game(view), ("A saved game was loaded (turn %d) whose memory a newer bridge wrote (%s): it is left as it "
                                       "is, and you play on a fresh memory that is not saved." % (loaded, e))
    if mem is None:
        # A save made without an agent (or by an older build): as far as memory goes, a new game.
        mem = Memory.new_game(view)
        note = "A saved game was loaded (turn %d) that holds no memory of yours: start a fresh plan." % loaded
    else:
        mem.start_branch()
        note = ("The game was reloaded to turn %d%s. Your plan, notes and decisions are as they were when that save was "
                "made; whatever happened after it did not." % (loaded, "" if reached is None else " (you had reached turn %d)" % reached))
    mem.bind_engine(api, seat)
    mem.flush()
    return mem, note
