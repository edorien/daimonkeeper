"""What the agent remembers between decisions, so it can keep one coherent plan for a whole game.

Four layers: a `plan` and `notes` the model rewrites itself (long-lived: strategy, what it learned about the opponent and the
map, things it must not forget), a rolling log of recent decisions with what became of each order, bookkeeping (which creature
kinds it has already been told about), and a stable name per creature (kind #n, assigned once and kept for that creature's
whole life) so the model can say "Orc #3" consistently instead of juggling numeric thing_ids turn to turn -- orders may give
`name` in place of `thing_id` (prompt.order_to_request resolves it). Everything survives a restart when a file is given.
Sizes are capped so the prompt stays bounded however long the game runs; the model is told the caps and is expected to
compress its own notes.
"""
import json
import os

PLAN_CAP = 1800
NOTES_CAP = 3500
RECENT = 12


class Memory:
    def __init__(self, path=None):
        self.path = path
        self.plan = ""
        self.notes = ""
        self.decisions = []          # newest last: {"n","turn","reasons","reasoning","orders":[{"id","verb","summary","status"}]}
        self.seen_kinds = []         # creature kinds whose profile the model has already been shown
        self.count = 0
        self.creature_names = {}     # {str(thing_id): name}, e.g. {"27": "Orc #3"}
        self._kind_counters = {}     # {kind: next index to assign}
        if path and os.path.exists(path):
            with open(path) as f:
                d = json.load(f)
            self.__dict__.update({k: d[k] for k in ("plan", "notes", "decisions", "seen_kinds", "count",
                                                     "creature_names", "_kind_counters") if k in d})

    def save(self):
        if not self.path:
            return
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump({"plan": self.plan, "notes": self.notes, "decisions": self.decisions, "seen_kinds": self.seen_kinds,
                      "count": self.count, "creature_names": self.creature_names, "_kind_counters": self._kind_counters}, f)
        os.replace(tmp, self.path)

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
        self.save()

    def note_results(self, results):
        """Fill in what happened to earlier orders from the view's seat.results ([{id,status,error?}])."""
        by_id = {r["id"]: r for r in results}
        changed = False
        for d in self.decisions:
            for o in d["orders"]:
                r = by_id.get(o.get("id"))
                if r and o["status"] == "queued":
                    o["status"] = r["status"] + ((" " + r["error"]) if r.get("error") else "")
                    changed = True
        if changed:
            self.save()

    def name_for(self, thing_id, kind):
        """This creature's stable name, assigning one ("Kind #n") the first time it is seen. Persists immediately
        (a name must survive even if the process is killed before the next scheduled save)."""
        key = str(thing_id)
        name = self.creature_names.get(key)
        if name is not None:
            return name
        pretty = kind.replace("_", " ").title()
        n = self._kind_counters.get(kind, 0) + 1
        self._kind_counters[kind] = n
        name = "%s #%d" % (pretty, n)
        self.creature_names[key] = name
        self.save()
        return name

    def id_for(self, name):
        """The thing_id a name was assigned to, or None. O(n) in the number of creatures ever named; fine at this scale."""
        for key, val in self.creature_names.items():
            if val == name:
                return int(key)
        return None

    def new_kinds(self, kinds):
        """The kinds in `kinds` the model has not been shown yet (and remember them)."""
        fresh = [k for k in kinds if k not in self.seen_kinds]
        self.seen_kinds.extend(fresh)
        if fresh:
            self.save()
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
