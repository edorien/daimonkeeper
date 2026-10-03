"""One agent's play session against a running dAImon Keeper game, driven step by step from the outside (mcp_server.py's tools) rather
than by an autonomous loop with a policy (bridge.py). Each method is one MCP tool call: connect once, then repeatedly
wait_for_decision (or status/look) and submit; the calling assistant decides in its own turn, in between.

Shares its logic with bridge.py: prompt.render_for_agent (the text shown to whichever model is deciding) and
orders.submit_batch (turning decided orders into submit_action calls) are the same code either way, so the two entry points
cannot silently drift apart.
"""
import time

import experience
import memory as memory_mod
import prompt
from api import Api, SESSION_EVENTS
from bridge import find_seat
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
        self.game_over = None   # "won" / "lost" / "ended" once the level is over; every later tool call is refused
        self.last_save_turn = None   # the game turn of the latest GAME_SAVED this session saw
        self._note = None            # said once, before the next decision's state (e.g. that the game was reloaded)
        self.client_name = None      # the MCP client's own name (initialize clientInfo), for the game record
        self.store = None            # experience.Experience: game records and lessons across games
        self.recorder = None

    def connect(self, host="127.0.0.1", port=5599, claim=None, min_interval_turns=100,
                max_age_turns=1500, connect_timeout=60.0, takeover=False, experience_path=None, agent=None):
        if self.connected:
            raise SessionError("already connected; call disconnect first")
        api = Api(host, port)
        api.connect(connect_timeout)
        try:
            seat = find_seat(api, claim)
            api.data(action="subscribe_event", event="DECISION_DUE")
            # Sent when the game leaves the level (the seat goes with it): the last word, even if no decision said so.
            api.data(action="subscribe_event", event="GAME_ENDED")
            # A load replaces the game under the agent (and its memory with the one saved); a save tells it the game
            # may be continued later.
            api.data(action="subscribe_event", event="GAME_LOADED")
            api.data(action="subscribe_event", event="GAME_SAVED")
            api.data(action="set_decision_policy", min_interval_turns=min_interval_turns)
            if takeover:
                api.data(action="set_takeover", enabled=True)
        except Exception:
            api.close()
            raise
        self.api, self.seat, self.max_age_turns = api, seat, max_age_turns
        self.state = ViewState()
        self._rendered_once = False
        self._decided_once = False
        self.connected = True
        self.game_over = None
        self.last_save_turn = None
        self._note = None
        self.state.update(self.api, self.seat)
        v = self.state.view
        # The game keeps the agent's memory (and saves it): a seat that already has one is a game being resumed.
        self.memory, resumed = memory_mod.attach(self.api, self.seat, v)
        self.pending_reasons = ["restart"] if resumed else ["start"]
        # What outlives the game: its record, and lessons from earlier ones (None: experience.sqlite next to the bridge; "": none).
        if self.store is not None:
            self.store.close()
        self.store = experience.open_store(experience_path)
        self.recorder = experience.Recorder(self.store, self.seat, agent or self.client_name)
        self.recorder.opponents = experience.opponents_from_view(v, self.seat)
        self.recorder.start(self.memory, v, resumed)
        self._experience_shown = False
        self.debrief = None          # {"result", "text"} once a finished game is waiting for record_debrief
        if agent:
            # The seat takes the agent's name, so the other players (and agents that meet it again) know who they play.
            try:
                self.api.data(action="set_player_name", player=self.seat, name=agent[:19])
            except Exception:  # noqa: BLE001 - an older game without the action: play on unnamed
                pass
        return ("connected: player %d, turn %d, gold %s, victory %s. Call wait_for_decision next; it returns when a "
                "decision is due (each quarter of a pay day, or a major event) and gives the full state to decide from."
                % (self.seat, v["turn"], v["own"]["gold"], v["seat"]["victory_state"]))

    def _require_connected(self, allow_over=False):
        if not self.connected:
            raise SessionError("not connected; call connect first")
        if self.game_over and not allow_over:
            raise SessionError("the level is over (%s); disconnect, then connect again for the next level" % self.game_over)

    def _over(self, why, reasons):
        """Marks the level over and returns the last decision result for it: the final state, and game_over=True."""
        self.game_over = why
        result = {"due": True, "reasons": reasons, "game_over": True, "victory_state": why, "turn": None, "state": None}
        last_view = self.state.view
        try:
            self.state.update(self.api, self.seat)
            result["turn"] = self.state.view["turn"]
            result["state"] = self._render()
            last_view = self.state.view
        except Exception:   # the level (and with it the seat) may already be gone
            result["state"] = "The level has ended (%s). No more orders can be given." % why
        # The game's record: won and lost close it; leaving the level undecided is a quit, or a pause if just saved.
        if why in ("won", "lost"):
            self.recorder.ended(self.memory, last_view or {}, why)
            result["record"], wanted = why, True
        else:
            result["record"], wanted = self.recorder.left(self.memory, last_view, self.last_save_turn)
        if wanted and self.store is not None and self.store.enabled:
            text = experience.debrief_text(self.store, self.memory, result["record"], experience.final_from_view(last_view),
                                           self.recorder.opponents)
            self.debrief = {"result": result["record"], "text": text}
            result["debrief"] = text
        return result

    def wait_for_decision(self, timeout_seconds=50.0, treat_timeout_as_decision=False):
        """Blocks up to `timeout_seconds` for the game's DECISION_DUE push. If one arrives (or several arrived already),
        returns {"due": True, "reasons", "turn", "state"} with the full text to decide from. If none arrives in time:
        {"due": False, "turn"} by default, or (treat_timeout_as_decision) a decision with reasons ["timer"] -- for a
        caller that would rather always get something to act on than poll again.

        The very first call after connect always returns due=True at once, with reasons=["start"] (a new game) or
        ["restart"] (the game already held this seat's memory) -- connecting is itself a reason to decide, the same as
        bridge.py's autonomous loop, which decides once before ever waiting for the game's cue. Without this, an agent
        that connects moments after some unrelated event already fired would see that event's reason on its first
        decision and never actually be told it just started.

        A load (GAME_LOADED) is a decision with reasons ["reloaded"]: the game, and the memory saved with it, are replaced
        by the save's, and the state says so. A save (GAME_SAVED) only notes its turn."""
        self._require_connected()
        if not self._decided_once:
            self._decided_once = True
            self.state.update(self.api, self.seat)
            self.memory.note_results(self.state.view["seat"].get("results", []))
            self.recorder.decision(self.memory, self.state.view)
            return {"due": True, "reasons": self.pending_reasons, "turn": self.state.view["turn"], "state": self._render()}
        events = self._collect_events(timeout_seconds)
        if any(ev.get("event") == "GAME_ENDED" for ev in events):
            last = (self.state.view or {}).get("seat", {}).get("victory_state", "undecided")
            return self._over(last if last in ("won", "lost") else "ended", ["game_ended"])
        reasons = None
        loads = [i for i, ev in enumerate(events) if ev.get("event") == "GAME_LOADED"]
        if loads:
            # What was due before the load belonged to a game that is gone.
            events = events[loads[-1] + 1:]
            reasons = ["reloaded"]
            self._on_loaded()
        due = [ev for ev in events if ev.get("event") == "DECISION_DUE"]
        if due:
            reasons = reasons or []
            for ev in due:
                reasons += [x for x in ev["data"]["reasons"].split(",") if x and x not in reasons]
        self.state.update(self.api, self.seat)
        vic = self.state.view["seat"]["victory_state"]
        if vic in ("won", "lost"):
            return self._over(vic, reasons or (["victory"] if vic == "won" else ["defeat"]))
        if reasons is None:
            if not treat_timeout_as_decision:
                return {"due": False, "turn": self.state.view["turn"]}
            reasons = ["timer"]
        self.pending_reasons = reasons
        self.memory.note_results(self.state.view["seat"].get("results", []))
        self.recorder.decision(self.memory, self.state.view)
        return {"due": True, "reasons": reasons, "turn": self.state.view["turn"], "state": self._render()}

    def _collect_events(self, timeout_seconds):
        """The session events that are here, or else the first to arrive within the timeout. A save alone is noted, not
        returned: it does not make a decision due."""
        deadline = time.time() + max(0.0, timeout_seconds)
        events = self.api.drain_events(SESSION_EVENTS)
        while True:
            for ev in events:
                if ev.get("event") == "GAME_SAVED":
                    self.last_save_turn = (ev.get("data") or {}).get("turn", self.last_save_turn)
            decisive = [ev for ev in events if ev.get("event") != "GAME_SAVED"]
            if decisive or time.time() >= deadline:
                return decisive
            ev = self.api.wait_event(SESSION_EVENTS, max(0.0, deadline - time.time()))
            if ev is None:
                return []
            events = [ev] + self.api.drain_events(SESSION_EVENTS)

    def _on_loaded(self):
        """A save was loaded under the agent: start over from the game's own state and the memory saved with it."""
        reached = (self.state.view or {}).get("turn")
        self.state = ViewState()
        self.state.update(self.api, self.seat)
        mem, self._note = memory_mod.reattach_after_load(self.api, self.seat, self.state.view, reached)
        self.recorder.reloaded(self.memory, reached, mem, self.state.view)
        self._experience_shown = False
        self.memory = mem
        self._rendered_once = False

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
        # Lessons from earlier games in full on an attempt's first decision (start, restart, reloaded), else one line.
        exp = experience.render_experience(self.store, self.memory, self.recorder.opponents, full=not self._experience_shown)
        self._experience_shown = True
        text = prompt.render_for_agent(self.state, self.memory, reasons=self.pending_reasons, first=not self._rendered_once,
                                       note=self._note, experience=exp)
        self._note = None
        self._rendered_once = True
        self.memory.flush()     # names given and kinds shown while rendering
        return text

    def submit(self, orders, reasoning="", plan=None, notes=None):
        self._require_connected()
        if self.state.view is None:
            self.state.update(self.api, self.seat)
        decision_turn = self.state.view["turn"]
        sent, refused = submit_batch(self.api, self.seat, orders, decision_turn, self.max_age_turns, memory=self.memory)
        warn = self.memory.update(plan=plan, notes=notes)
        self.memory.record_decision(decision_turn, self.pending_reasons or [], reasoning, sent, refused)
        self.recorder.decided(self.memory, decision_turn)
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
        self._require_connected(allow_over=True)
        data = self.api.data(action="get_log_tail", lines=lines)
        if data["lines"]:
            return "\n".join(data["lines"])
        if data.get("log_level") == "OFF":
            return "(logging is off in this game: LOG_LEVEL=OFF, so it writes no log)"
        return "(log empty or unavailable)"

    def record_debrief(self, summary=None, playbook=None, campaign_lessons=None, level_lessons=None, opponent_lessons=None):
        """After a game ends: the summary goes on its record and each lesson text replaces that scope's lessons."""
        self._require_connected(allow_over=True)
        if not self.game_over:
            raise SessionError("record_debrief is for when the game has ended (wait_for_decision says game_over)")
        if self.debrief is None:
            raise SessionError("no debrief is due for this game (%s)" % self.game_over)
        out = experience.apply_debrief(self.store, self.memory, summary, playbook, campaign_lessons, level_lessons,
                                       opponent_lessons, self.recorder.opponents)
        self.debrief = None
        return out

    def experience_text(self):
        """The lessons that apply to this game, in full (for a client whose context has dropped them)."""
        self._require_connected(allow_over=True)
        return experience.render_experience(self.store, self.memory, self.recorder.opponents, full=True) or \
            "There is no experience store in this session (experience_path was \"\")."

    def instructions(self):
        return prompt.SYSTEM_PROMPT

    def recipes(self):
        """The active sacrifice recipes, for a client whose context no longer holds the list shown when the temple was
        built."""
        self._require_connected()
        self.state.update(self.api, self.seat)
        return prompt.render_recipes(self.state.view, full=True)

    def disconnect(self):
        self._require_connected(allow_over=True)
        # An attempt still being played stays open in the record: a reconnect carries on with it.
        if self.store is not None:
            self.store.close()
            self.store = None
        self.api.close()
        self.connected = False
        return "disconnected"
