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
"""
import json
import sys
import time
import uuid

from api import ApiError

PLAN_CAP = 1800
NOTES_CAP = 3500
RECENT = 12
FORMAT = 1
BLOB_CAP = 60000          # the engine refuses a text over 65536 bytes (agent_memory.h AGENT_MEMORY_MAX)
NAMES_PRUNE_AT = 400      # past this many names, those of creatures no longer in the dungeon are dropped
CHECKPOINTS_CAP = 120     # past this, every other old checkpoint is dropped (a coarser history, never a gap at the end)

_FIELDS = ("format", "game_id", "level", "started_at", "start_turn", "branch", "parent_branch", "plan", "notes", "decisions", "seen_kinds",
           "count", "creature_names", "_kind_counters", "checkpoints", "stats")


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
        if not isinstance(d, dict) or d.get("format") != FORMAT or not d.get("game_id"):
            return None
        m = cls()
        for k in _FIELDS:
            if k in d:
                setattr(m, k, d[k])
        m._pushed = text
        return m

    @classmethod
    def from_engine(cls, api, seat):
        """The seat's memory as the game holds it, or None (a new game, or an engine without agent memory)."""
        try:
            data = api.data(action="get_agent_memory", player=seat)
        except ApiError as e:
            print("keeperfx does not keep agent memory (%s); the plan will not survive a save or a restart" % e.code,
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
    def update(self, plan=None, notes=None):
        """Replace the plan and/or notes (only what the model sent). Returns a warning string if something was cut."""
        cut = []
        if plan is not None:
            self.plan = plan[:PLAN_CAP]
            if len(plan) > PLAN_CAP:
                cut.append("plan cut to %d characters" % PLAN_CAP)
        if notes is not None:
            self.notes = notes[:NOTES_CAP]
            if len(notes) > NOTES_CAP:
                cut.append("notes cut to %d characters; compress them" % NOTES_CAP)
        return "; ".join(cut)

    def record_decision(self, turn, reasons, reasoning, sent, refused):
        self.count += 1
        self.decisions.append({"n": self.count, "turn": turn, "reasons": list(reasons), "reasoning": reasoning[:300],
                               "orders": [dict(o, status="queued") for o in sent], "refused": list(refused)})
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

    def new_kinds(self, kinds):
        """The kinds in `kinds` the model has not been shown yet (and remember them)."""
        fresh = [k for k in kinds if k not in self.seen_kinds]
        self.seen_kinds.extend(fresh)
        return fresh

    # ---- what the model reads --------------------------------------------------------------------------------------------
    def render(self):
        lines = ["YOUR PLAN (rewrite it in submit_orders when it changes; max %d chars):" % PLAN_CAP, self.plan or "(none yet: make one)",
                 "YOUR NOTES (facts to remember; rewrite and keep them short; max %d chars):" % NOTES_CAP, self.notes or "(empty)"]
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
    mem = Memory.from_engine(api, seat)
    resumed = mem is not None
    if mem is None:
        mem = Memory.new_game(view)
    mem.bind_engine(api, seat)
    mem.flush()
    return mem, resumed


def reattach_after_load(api, seat, view, reached=None):
    """After a save was loaded under the agent: the memory saved with it (a new attempt at that game), or a new game's
    when the save holds none. Returns (memory, note), the note being what to tell the model once."""
    mem = Memory.from_engine(api, seat)
    loaded = view.get("turn", 0)
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
