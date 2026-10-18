"""The latency chain of each decision (docs/plans/llm-bridge-realtime.md, Phase 0): how many game turns each link between
something happening and the seat acting on it took. Every link is measured, so a change to one of them (the minimum
interval, the bridge, the prompt, the order queue) shows up in the number it was meant to move.

  raised     the turn the engine first noted the earliest of the decision's reasons (DECISION_DUE's `raised`)
  due        the turn it sent DECISION_DUE               raised -> due:        the minimum interval holding it back
  delivered  the turn the decision reached the agent     due -> delivered:     nobody was listening for it
  answered   the turn the game accepted the reply's first order
                                                         delivered -> answered: reading and thinking
  started    the turn that order became the running gesture
                                                         answered -> started:  the queue in front of it
  finished   the turn its last step was written          started -> finished:  the gesture itself

Turns stand still while the game is paused, so a stepped game shows small turn counts and large `think_seconds`
(wall-clock, delivered -> the reply). Shared by mcp_session.py, bridge.py and agent_vs_agent.py through
experience.Recorder, which keeps the summary on the game's record (final.latency; combat_report.py reads it).
"""
import time

# (name, from, to): the links reported, in the order of the chain. "total" is cause -> the hand starts acting.
LINKS = (("hold", "raised", "due"), ("delivery", "due", "delivered"), ("think", "delivered", "answered"),
         ("queue", "answered", "started"), ("gesture", "started", "finished"), ("total", "raised", "started"))
KEPT = 64       # finished records kept for the medians (the game keeps 128 decisions for its own)
RECENT = 16     # of those, shown in the summary


def due_info(events):
    """(due, raised) from the DECISION_DUE events one decision coalesced: the earliest emit turn and the earliest turn
    one of its reasons was raised (the emit turn when the game sent no `raised`: an older engine). (None, None) for a
    decision no event made due (start, restart, reloaded, timer)."""
    due = raised = None
    for ev in events or []:
        if ev.get("event") != "DECISION_DUE":
            continue
        d = ev.get("data") or {}
        t = d.get("turn")
        if isinstance(t, int):
            due = t if due is None else min(due, t)
        for x in str(d.get("raised") or "").split(","):
            x = x.strip()
            if x.lstrip("-").isdigit():
                raised = int(x) if raised is None else min(raised, int(x))
    if raised is None:
        raised = due
    return due, raised


def _median(values):
    vals = sorted(v for v in values if v is not None)
    return vals[(len(vals) - 1) // 2] if vals else None


class LatencyChain:
    """One record per decision, from the reasons that made it due to the first order's gesture being written."""

    def __init__(self):
        self.count = 0
        self.current = None     # delivered, not yet answered
        self.open = []          # answered, waiting for the first order's result (seat.results)
        self.done = []          # complete, the last KEPT
        self._taken = 0         # how many of `done` take_finished() has handed out (counted over all ever finished)
        self._finished_total = 0

    def delivered(self, reasons, events, view_turn, wall=None):
        """A decision reached the agent. A redelivered one (the reply to it may have been lost) keeps its record."""
        due, raised = due_info(events)
        now = time.time() if wall is None else wall
        if self.current is not None and self.current.get("redelivery_pending"):
            rec = self.current
            rec.pop("redelivery_pending", None)
            rec["redelivered"] = rec.get("redelivered", 0) + 1
            return rec
        if self.current is not None:
            self._finish(dict(self.current, unanswered=True))  # the agent waited again without replying
        self.count += 1
        self.current = {"decision": self.count, "reasons": list(reasons or []), "raised": raised, "due": due,
                        "delivered": view_turn, "_wall": now}
        return self.current

    def redelivering(self):
        """The next delivered() is the same decision handed out again (mcp_session's risky-wait redelivery)."""
        if self.current is not None:
            self.current["redelivery_pending"] = True

    def answered(self, sent, wall=None):
        """The agent replied with these orders (submit_batch's `sent`). Returns the record, or None if no decision was
        open (orders sent outside a decision)."""
        rec, self.current = self.current, None
        if rec is None:
            return None
        rec.pop("redelivery_pending", None)
        rec["think_seconds"] = round((time.time() if wall is None else wall) - rec.pop("_wall"), 3)
        ids = [s["id"] for s in sent or [] if isinstance(s.get("id"), int)]
        rec["orders"] = len(ids)
        if ids:
            rec["first_id"] = ids[0]
            self.open.append(rec)
        else:
            self._finish(rec)
        return rec

    def results(self, results):
        """The seat's recent order results (view seat.results): completes each record whose first order is there."""
        if not self.open:
            return
        by_id = {r.get("id"): r for r in results or []}
        oldest = min(by_id) if by_id else None
        still = []
        for rec in self.open:
            r = by_id.get(rec["first_id"])
            if r is None:
                if (oldest is not None) and (rec["first_id"] < oldest) and (len(by_id) >= RECENT):
                    self._finish(dict(rec, lost=True))   # fell out of the game's ring of results unseen
                else:
                    still.append(rec)
                continue
            rec["answered"] = r.get("submitted")
            started = r.get("started")
            rec["started"] = started if isinstance(started, int) and started >= 0 else None
            if r.get("status") == "done":
                rec["finished"] = r.get("turn")
            else:
                rec["error"] = r.get("error")
            self._finish(rec)
        self.open = still[-KEPT:]

    def _finish(self, rec):
        rec = {k: v for k, v in rec.items() if not k.startswith("_") and k != "redelivery_pending"}
        for name, a, b in LINKS:
            if isinstance(rec.get(a), int) and isinstance(rec.get(b), int):
                rec[name] = rec[b] - rec[a]
        self.done.append(rec)
        self._finished_total += 1
        del self.done[:-KEPT]

    def take_finished(self):
        """The records finished since the last call (for a per-decision log line)."""
        fresh = self._finished_total - self._taken
        self._taken = self._finished_total
        return self.done[-fresh:] if fresh > 0 else []

    def summary(self):
        """Medians per link over the kept records, plus the most recent ones: what goes on the game's record."""
        if not self.done:
            return None
        links = {}
        for name, _, _ in LINKS:
            vals = [r[name] for r in self.done if name in r]
            if vals:
                links[name] = {"median": _median(vals), "max": max(vals), "n": len(vals)}
        think = [r["think_seconds"] for r in self.done if "think_seconds" in r]
        return {"decisions": len(self.done), "links": links,
                "median_think_seconds": _median(think), "recent": self.done[-RECENT:]}
