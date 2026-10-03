"""One agent's play session against a running keeperfx, driven step by step from the outside (mcp_server.py's tools) rather
than by an autonomous loop with a policy (bridge.py). Each method is one MCP tool call: connect once, then repeatedly
wait_for_decision (or status/look) and submit; the calling assistant decides in its own turn, in between.

Shares its logic with bridge.py: prompt.render_for_agent (the text shown to whichever model is deciding) and
orders.submit_batch (turning decided orders into submit_action calls) are the same code either way, so the two entry points
cannot silently drift apart.
"""
import prompt
from api import Api
from bridge import find_seat
from memory import Memory
from orders import submit_batch
from viewstate import ViewState


class SessionError(Exception):
    """A tool was called out of order (e.g. before connect, or twice)."""


class Session:
    def __init__(self):
        self.api = None
        self.seat = None
        self.state = None
        self.memory = None
        self.max_age_turns = 1500
        self.pending_reasons = None
        self._rendered_once = False
        self._decided_once = False
        self.connected = False

    def connect(self, host="127.0.0.1", port=5599, claim=None, memory_path=None, min_interval_turns=100,
                max_age_turns=1500, connect_timeout=60.0, takeover=False):
        if self.connected:
            raise SessionError("already connected; call disconnect first")
        api = Api(host, port)
        api.connect(connect_timeout)
        try:
            seat = find_seat(api, claim)
            api.data(action="subscribe_event", event="DECISION_DUE")
            api.data(action="set_decision_policy", min_interval_turns=min_interval_turns)
            if takeover:
                api.data(action="set_takeover", enabled=True)
        except Exception:
            api.close()
            raise
        self.api, self.seat, self.max_age_turns = api, seat, max_age_turns
        self.state = ViewState()
        self.memory = Memory(memory_path)
        self.pending_reasons = ["start"] if not self.memory.decisions else ["restart"]
        self._rendered_once = False
        self._decided_once = False
        self.connected = True
        self.state.update(self.api, self.seat)
        v = self.state.view
        return ("connected: player %d, turn %d, gold %s, victory %s. Call wait_for_decision next; it returns when a "
                "decision is due (each quarter of a pay day, or a major event) and gives the full state to decide from."
                % (self.seat, v["turn"], v["own"]["gold"], v["seat"]["victory_state"]))

    def _require_connected(self):
        if not self.connected:
            raise SessionError("not connected; call connect first")

    def wait_for_decision(self, timeout_seconds=50.0, treat_timeout_as_decision=False):
        """Blocks up to `timeout_seconds` for the game's DECISION_DUE push. If one arrives (or several arrived already),
        returns {"due": True, "reasons", "turn", "state"} with the full text to decide from. If none arrives in time:
        {"due": False, "turn"} by default, or (treat_timeout_as_decision) a decision with reasons ["timer"] -- for a
        caller that would rather always get something to act on than poll again.

        The very first call after connect (or the first after a reconnect that resumes an existing --memory file) always
        returns due=True at once, with reasons=["start"] or ["restart"] -- connecting is itself a reason to decide, the
        same as bridge.py's autonomous loop, which decides once before ever waiting for the game's cue. Without this, an
        agent that connects moments after some unrelated event already fired would see that event's reason on its first
        decision and never actually be told it just started."""
        self._require_connected()
        if not self._decided_once:
            self._decided_once = True
            self.state.update(self.api, self.seat)
            self.memory.note_results(self.state.view["seat"].get("results", []))
            return {"due": True, "reasons": self.pending_reasons, "turn": self.state.view["turn"], "state": self._render()}
        events = self.api.drain_events("DECISION_DUE")
        if not events and timeout_seconds > 0:
            ev = self.api.wait_event("DECISION_DUE", timeout_seconds)
            if ev is not None:
                events = [ev]
        reasons = None
        if events:
            reasons = []
            for ev in events:
                reasons += [x for x in ev["data"]["reasons"].split(",") if x and x not in reasons]
        self.state.update(self.api, self.seat)
        if reasons is None:
            if not treat_timeout_as_decision:
                return {"due": False, "turn": self.state.view["turn"]}
            reasons = ["timer"]
        self.pending_reasons = reasons
        self.memory.note_results(self.state.view["seat"].get("results", []))
        return {"due": True, "reasons": reasons, "turn": self.state.view["turn"], "state": self._render()}

    def status(self):
        """A cheap look without treating it as a decision (does not touch pending reasons or memory)."""
        self._require_connected()
        self.state.update(self.api, self.seat)
        v = self.state.view
        own = v["own"]
        return ("turn %d%s | gold %s | victory %s | creatures %d | queued verbs %d | ordered creatures %d | "
                "pending decision reasons: %d" % (
                    v["turn"], " (paused)" if v["paused"] else "", own["gold"], v["seat"]["victory_state"],
                    len(own["creatures"]), v["seat"].get("queued_verbs", 0), v["seat"].get("ordered_creatures", 0),
                    v["seat"].get("decision", {}).get("pending", 0)))

    def look(self, slab_rect):
        self._require_connected()
        if self.state.view is None:
            self.state.update(self.api, self.seat)
        return prompt.render_window(self.state.view, slab_rect)

    def _render(self):
        text = prompt.render_for_agent(self.state, self.memory, reasons=self.pending_reasons, first=not self._rendered_once)
        self._rendered_once = True
        return text

    def submit(self, orders, reasoning="", plan=None, notes=None):
        self._require_connected()
        if self.state.view is None:
            self.state.update(self.api, self.seat)
        decision_turn = self.state.view["turn"]
        sent, refused = submit_batch(self.api, self.seat, orders, decision_turn, self.max_age_turns, memory=self.memory)
        warn = self.memory.update(plan=plan, notes=notes)
        self.memory.record_decision(decision_turn, self.pending_reasons or [], reasoning, sent, refused)
        result = {"sent": sent, "refused": refused}
        if warn:
            result["warning"] = warn
        return result

    def check_orders(self, orders):
        """Validates a batch (the same checks submit would run, including cost) without spending it or the queue."""
        self._require_connected()
        if self.state.view is None:
            self.state.update(self.api, self.seat)
        decision_turn = self.state.view["turn"]
        would_succeed, would_fail = submit_batch(self.api, self.seat, orders, decision_turn, self.max_age_turns, dry_run=True, memory=self.memory)
        return {"would_succeed": would_succeed, "would_fail": would_fail}

    def set_speed(self, turns_per_second):
        """Global to the game, not per seat: how many simulated turns run per real second. 0 resets to the configured
        default. See external_seat.h's own docs/refactor/AI/LLM/02 §4a note on the trade-off this makes for a human
        sharing the session."""
        self._require_connected()
        data = self.api.data(action="set_game_speed", turns_per_second=turns_per_second)
        return "turns_per_second is now %s" % data["turns_per_second"]

    def log_tail(self, lines=100):
        """The game's own recent log (get_log_tail), for debugging a confusing session."""
        self._require_connected()
        data = self.api.data(action="get_log_tail", lines=lines)
        if data["lines"]:
            return "\n".join(data["lines"])
        if data.get("log_level") == "OFF":
            return "(logging is off in this game: LOG_LEVEL=OFF, so it writes no log)"
        return "(log empty or unavailable)"

    def instructions(self):
        return prompt.SYSTEM_PROMPT

    def disconnect(self):
        self._require_connected()
        self.api.close()
        self.connected = False
        return "disconnected"
