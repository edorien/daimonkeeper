"""One agent's play session against a running dAImon Keeper game, driven step by step from the outside (mcp_server.py's tools) rather
than by an autonomous loop with a policy (bridge.py). Each method is one MCP tool call: connect once, then repeatedly
wait_for_decision (or status/look) and submit; the calling assistant decides in its own turn, in between.

Shares its logic with bridge.py: prompt.render_for_agent (the text shown to whichever model is deciding) and
orders.submit_batch (turning decided orders into submit_action calls) are the same code either way, so the two entry points
cannot silently drift apart.

The session's game connection is a gamelink.GameLink: a pump thread reads the game's pushes and refreshes the view between
tool calls (docs/plans/llm-bridge-realtime.md, Phase 1). Every public method holds the session's lock (one RLock, shared
with the pump), so tool calls may come from several threads (mcp_http_server.py runs each request on its own); a wait
releases it while it waits, so status and look answer during one. An urgent decision (severity <= 1) is a FLASH decision
(prompt.render_flash), announced on every other reply until a wait takes it (alerts_text), and handed back inline by
submit when it came while the previous decision was being answered (Phase 2).
"""
import functools
import os
import threading
import time

import combat_report
import experience
import memory as memory_mod
import prompt
import standing_orders
import triggers as triggers_mod
from api import Api
from bridge import find_seat
from gamelink import GameLink, URGENT_SEVERITY, SEV_ROUTINE, SEVERITY_NAMES, event_severity, event_where, severity_of
from orders import submit_batch
from viewstate import ViewState


# A wait longer than an MCP client is prepared to wait for its reply loses the decision it produces: the client gives up,
# calls again, and the late reply goes nowhere (seen live: a quarter decision during a long advance_turns never reached
# the agent). Waits are capped below the reference SDKs' 60 s request timeout, and a decision produced by a wait that had
# already blocked RISKY_WAIT_SECONDS (a client with a shorter timeout may have given up) is handed out again by the next
# wait_for_decision unless submit_orders answered it first.
MAX_WAIT_SECONDS = float(os.environ.get("DAIMONKEEPER_MAX_WAIT_SECONDS", "50"))
RISKY_WAIT_SECONDS = float(os.environ.get("DAIMONKEEPER_RISKY_WAIT_SECONDS", "30"))


# Reasons that make a decision the full picture whatever its severity: a new start, or a load replacing the game.
FULL_PICTURE_REASONS = ("start", "restart", "reloaded")


class SessionError(Exception):
    """A tool was called out of order (e.g. before connect, or twice)."""


def _locked(fn):
    """Runs a Session method holding the session's lock (shared with the pump)."""
    @functools.wraps(fn)
    def run(self, *args, **kwargs):
        with self._lock:
            return fn(self, *args, **kwargs)
    return run


class Session:
    def __init__(self):
        self._lock = threading.RLock()
        self.link = None             # gamelink.GameLink while connected: the pump and the pushed events
        self._decision_turn = None   # the view's turn when the agent was last shown a decision (its orders' view_turn)
        self._severity = SEV_ROUTINE  # of the decision being answered
        self._where = None
        self._shown_alerts = set()   # urgent pushes already announced on a reply
        self._unanswered_inline = False  # the unanswered decision was handed out by submit (next_decision)
        self.api = None
        self.seat = None
        self.state = None
        self.memory = None
        self.max_age_turns = 1500
        self.pending_reasons = None
        self._unanswered = None      # the reasons of a due decision not yet answered with submit_orders
        self._rendered_once = False
        self._decided_once = False
        self.connected = False
        self.game_over = None   # "won" / "lost" / "ended" once the level is over; every later tool call is refused
        self.last_save_turn = None   # the game turn of the latest GAME_SAVED this session saw
        self._note = None            # said once, before the next decision's state (e.g. that the game was reloaded)
        self.client_name = None      # the MCP client's own name (initialize clientInfo), for the game record
        self.store = None            # experience.Experience: game records and lessons across games
        self.recorder = None
        self.last_combat = None      # the game's latest get_combat_stats for the seat, kept on the record at the end

    @_locked
    def connect(self, host="127.0.0.1", port=5599, claim=None, min_interval_turns=100,
                max_age_turns=1500, connect_timeout=60.0, takeover=False, experience_path=None, agent=None,
                start_paused=True, on_urgent="auto", hold_seconds=60, slow_turns_per_second=4):
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
        self._unanswered = None
        self.connected = True
        self.game_over = None
        self.last_save_turn = None
        self._note = None
        self.last_combat = None
        self.state.update(self.api, self.seat)
        v = self.state.view
        # No human plays a keeper: the game is the agent's to pause. A level that started with the seat is already
        # paused for it; one it has just claimed is paused now, so it can look the level over before anything moves.
        if v.get("agent_only") and start_paused and not v.get("paused"):
            self.api.data(action="set_pause", player=self.seat, paused=True)
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
        self._shown_alerts = set()
        self._unanswered_inline = False
        self._decision_turn = None
        self._decision_seq = None
        # Time to think (api_urgent_hold.h): "auto" holds the game -- pauses it -- for each urgent decision where the agent may
        # pause anyway (no human keeper, or the Skirmish page's LLM Pause), and never where a human plays.
        mode = on_urgent
        if mode == "auto":
            mode = "pause" if (v.get("can_pause") if "can_pause" in v else v.get("agent_only")) else "none"
        self.on_urgent = "none"
        if mode in ("pause", "slow"):
            r = self.api.call(action="set_decision_policy", player=self.seat, min_interval_turns=min_interval_turns, on_urgent=mode,
                              hold_seconds=int(hold_seconds), slow_turns_per_second=int(slow_turns_per_second))
            if r.get("success"):
                self.on_urgent = mode
            elif on_urgent != "auto":
                raise SessionError("on_urgent %s refused: %s" % (mode, r.get("error")))
        self.link = GameLink(self.api, self.seat, self.state, lock=self._lock,
                             pump_turns=int(os.environ.get("DAIMONKEEPER_PUMP_TURNS", "10")))
        self.link.on_events = self._on_pushed
        if os.environ.get("DAIMONKEEPER_PUMP", "1") != "0":
            self.link.start()
        held = v.get("paused") and v.get("agent_pause")
        return ("connected: player %d, turn %d, gold %s, victory %s. Call wait_for_decision next; it returns when a "
                "decision is due (each quarter of a pay day, or a major event) and gives the full state to decide from.%s"
                % (self.seat, v["turn"], v["own"]["gold"], v["seat"]["victory_state"],
                   " The game is PAUSED for you (no human plays a keeper): look the level over, send your first orders, then "
                   "set_pause(paused=false) to start it." if held else "")
                + {"pause": " Each urgent decision (a fight, an intruder, a loss) pauses the game until you answer it (at most %d s)." % hold_seconds,
                   "slow": " Each urgent decision slows the game to %d turns a second until you answer it (at most %d s)." % (slow_turns_per_second, hold_seconds)
                   }.get(self.on_urgent, ""))

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
        self._refresh_combat()   # the level may be gone; then the last decision's statistics stand
        if why in ("won", "lost"):
            self.recorder.ended(self.memory, last_view or {}, why, combat=self.last_combat)
            result["record"], wanted = why, True
        else:
            result["record"], wanted = self.recorder.left(self.memory, last_view, self.last_save_turn,
                                                          combat=self.last_combat)
        if wanted and self.store is not None and self.store.enabled:
            text = experience.debrief_text(self.store, self.memory, result["record"], experience.final_from_view(last_view),
                                           self.recorder.opponents)
            self.debrief = {"result": result["record"], "text": text}
            result["debrief"] = text
        return result

    @_locked
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
            self._refresh_combat()
            self.recorder.latency.delivered(self.pending_reasons, [], self.state.view["turn"])
            self._severity, self._where = SEV_ROUTINE, None
            return {"due": True, "reasons": self.pending_reasons, "turn": self.state.view["turn"], "severity": SEV_ROUTINE,
                    "flash": False, "state": self._render()}
        # A decision whose reply may not have reached the agent (MAX_WAIT_SECONDS above) is handed out again, at once and
        # with the state as it is now. While the agent holds the pause nothing happens in the game, so nothing would come
        # either: answer at once.
        redeliver = self._unanswered
        self._unanswered = None
        inline, self._unanswered_inline = self._unanswered_inline, False
        held = (redeliver is not None) or self._holding_pause()
        capped = (not held) and (timeout_seconds > MAX_WAIT_SECONDS)
        started = time.time()
        events = self._collect_events(0 if held else min(timeout_seconds, MAX_WAIT_SECONDS))
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
        # How urgent: the most severe push's severity and place; a redelivered decision keeps its own.
        severity, where = (self._severity, self._where) if redeliver is not None else (SEV_ROUTINE, None)
        for ev in due:
            sev = event_severity(ev)
            if sev < severity or (sev == severity and where is None):
                severity, where = sev, event_where(ev) or where
        self.state.update(self.api, self.seat)
        vic = self.state.view["seat"]["victory_state"]
        if vic in ("won", "lost"):
            return self._over(vic, reasons or (["victory"] if vic == "won" else ["defeat"]))
        if redeliver is not None:
            reasons = list(redeliver) + [r for r in (reasons or []) if r not in redeliver]
        if reasons is None:
            if held:
                if (self.state.view["seat"].get("decision") or {}).get("hold"):
                    return {"due": False, "paused": True, "turn": self.state.view["turn"],
                            "note": "The game is paused for your answer to the last urgent decision: submit_orders (an empty "
                                    "list will do) and it runs on."}
                return {"due": False, "paused": True, "turn": self.state.view["turn"],
                        "note": "The game is paused and you hold the pause: nothing moves and no decision comes until "
                                "set_pause(paused=false) (or set_pause(paused=false, turns=N) to run N turns and pause again)."}
            if not treat_timeout_as_decision:
                out = {"due": False, "turn": self.state.view["turn"]}
                if capped:
                    out["note"] = ("waited %d s, the longest a wait may take (a client gives up on a reply after about a "
                                   "minute, and the decision would be lost): call wait_for_decision again" % MAX_WAIT_SECONDS)
                return out
            reasons = ["timer"]
        severity = min(severity, severity_of(reasons))
        self.pending_reasons = reasons
        if redeliver is None:
            seqs = [(ev.get("data") or {}).get("seq") for ev in due]
            seqs = [q for q in seqs if isinstance(q, int)]
            self._decision_seq = max(seqs) if seqs else ((self.state.view["seat"].get("decision") or {}).get("seq") or self._decision_seq)
        self._severity, self._where = severity, where
        if (redeliver is None) and (time.time() - started >= RISKY_WAIT_SECONDS):
            self._unanswered = list(reasons) # a long wait: its reply may not reach a client that gave up
        results = self.state.view["seat"].get("results", [])
        self.recorder.latency.results(results)
        triggers_mod.note_results(self.memory, results)   # what became of the orders the triggers sent
        if redeliver is not None:
            self.recorder.latency.redelivering()
        self.recorder.latency.delivered(reasons, due, self.state.view["turn"])
        self.memory.note_results(results)
        self.recorder.decision(self.memory, self.state.view)
        flash = (severity <= URGENT_SEVERITY) and not any(r in FULL_PICTURE_REASONS for r in reasons)
        if not flash:
            self._refresh_combat()   # a flash is answered first; the statistics can wait for the next full decision
        result = {"due": True, "reasons": reasons, "turn": self.state.view["turn"], "severity": severity, "flash": flash}
        if where:
            result["where"] = where
        result["state"] = self._render(flash=flash, severity=severity, where=where)
        if redeliver is not None:
            result["repeated"] = True
            result["note"] = (("You had not answered the urgent decision submit_orders handed you (next_decision): here it "
                               "is again, with the state as it is now.") if inline else
                              ("The last decision came at the end of a long wait, so its reply may never have reached you, "
                               "and you had not answered it (submit_orders; an empty list to wait): here it is again, with "
                               "the state as it is now."))
        return result

    def _holding_pause(self):
        """Whether the game is paused by this agent and not running an advance (a fresh view)."""
        self.state.update(self.api, self.seat)
        v = self.state.view
        return bool(v.get("paused") and v.get("agent_pause") and not v.get("advancing"))

    @_locked
    def set_pause(self, paused, turns=None):
        """Pauses or resumes the game -- only in a game no human plays a keeper in, or one whose setup allows an agent to
        pause anyway (the view's can_pause; the engine refuses HUMAN_PLAYING otherwise). With paused=False and `turns`, runs that many turns and pauses again."""
        self._require_connected()
        self.state.update(self.api, self.seat)
        if paused or turns:
            v = self.state.view
            if not (v.get("can_pause") if "can_pause" in v else v.get("agent_only")):
                raise SessionError("a human plays a keeper in this game, so you cannot pause it; set_game_speed slows it "
                                   "down for everyone instead")
        if turns:
            self.api.data(action="advance_turns", player=self.seat, turns=int(turns))
            return "running %d turn(s) from turn %d, then paused again" % (int(turns), self.state.view["turn"])
        self.api.data(action="set_pause", player=self.seat, paused=bool(paused))
        return ("paused at turn %d: nothing moves (orders you send wait) until set_pause(paused=false)" if paused
                else "running from turn %d") % self.state.view["turn"]

    def _collect_events(self, timeout_seconds):
        """The session events the pump has collected, or else the first to arrive within the timeout (the lock is released
        while waiting). A save alone is noted, not returned: it does not make a decision due."""
        deadline = time.time() + max(0.0, timeout_seconds)
        while True:
            events = self.link.take_events()
            for ev in events:
                if ev.get("event") == "GAME_SAVED":
                    self.last_save_turn = (ev.get("data") or {}).get("turn", self.last_save_turn)
            decisive = [ev for ev in events if ev.get("event") != "GAME_SAVED"]
            if decisive or time.time() >= deadline:
                return decisive
            self.link.wait_for_events(deadline - time.time())

    def _on_loaded(self):
        """A save was loaded under the agent: start over from the game's own state and the memory saved with it."""
        reached = (self.state.view or {}).get("turn")
        self.state = ViewState()
        self.link.state = self.state
        self.state.update(self.api, self.seat)
        mem, self._note = memory_mod.reattach_after_load(self.api, self.seat, self.state.view, reached)
        self.recorder.reloaded(self.memory, reached, mem, self.state.view)
        self._experience_shown = False
        self.memory = mem
        self._rendered_once = False

    @_locked
    def status(self, full=False):
        """A cheap look without treating it as a decision (does not touch pending reasons or memory). `full`: the whole
        state as a decision shows it (for a flash decision that needs more), without starting the news over."""
        self._require_connected()
        self.state.update(self.api, self.seat)
        v = self.state.view
        if full:
            return prompt.render_state(self.state, memory=self.memory)
        own = v["own"]
        return ("turn %d%s | gold %s | victory %s | creatures %d | queued verbs %d | ordered creatures %d | "
                "pending decision reasons: %d" % (
                    v["turn"], " (paused)" if v["paused"] else "", own["gold"], v["seat"]["victory_state"],
                    len(own["creatures"]), v["seat"].get("queued_verbs", 0), v["seat"].get("ordered_creatures", 0),
                    v["seat"].get("decision", {}).get("pending", 0)))

    @_locked
    def look(self, slab_rect):
        self._require_connected()
        # Always a fresh view: the cached one is the last decision's, so a room built or a slab dug since then would not
        # show (a bridge just built still read as lava).
        self.state.update(self.api, self.seat)
        return prompt.render_window(self.state.view, slab_rect)

    def _render(self, flash=False, severity=SEV_ROUTINE, where=None):
        self._decision_turn = self.state.view["turn"]
        self._shown_alerts.clear()   # what is pending now is in this decision, or announced afresh
        if flash:
            text = prompt.render_flash(self.state, self.memory, self.pending_reasons or [], severity, where=where, note=self._note)
            self._note = None
            self.memory.flush()
            return text
        # Lessons from earlier games in full on an attempt's first decision (start, restart, reloaded), else one line.
        exp = experience.render_experience(self.store, self.memory, self.recorder.opponents, full=not self._experience_shown)
        self._experience_shown = True
        text = prompt.render_for_agent(self.state, self.memory, reasons=self.pending_reasons, first=not self._rendered_once,
                                       note=self._note, experience=exp)
        self._note = None
        self._rendered_once = True
        self.memory.flush()     # names given and kinds shown while rendering
        return text

    @_locked
    def submit(self, orders, reasoning="", plan=None, notes=None):
        self._require_connected()
        if self.state.view is None:
            self.state.update(self.api, self.seat)
        # The orders answer the view the decision showed: they expire counting from it, however fresh the pump keeps
        # the view since.
        decision_turn = self._decision_turn if self._decision_turn is not None else self.state.view["turn"]
        if self._severity <= URGENT_SEVERITY:
            # The answer to a flash decision is urgent by default: ahead of the routine orders still waiting.
            orders = [o if "priority" in o or o.get("verb") == "cancel" else dict(o, priority="now") for o in orders]
        sent, refused = submit_batch(self.api, self.seat, orders, decision_turn, self.max_age_turns, memory=self.memory,
                                     decision_seq=self._decision_seq)
        # The decision is answered, orders or not: a game held for it runs on (api_urgent_hold.h).
        try:
            ack = {"action": "ack_decision", "player": self.seat}
            if self._decision_seq:
                ack["seq"] = self._decision_seq
            self.api.call(**ack)
        except Exception:  # noqa: BLE001 - an older game without it: nothing is held there
            pass
        warn = self.memory.update(plan=plan, notes=notes)
        self.memory.record_decision(decision_turn, self.pending_reasons or [], reasoning, sent, refused)
        self.recorder.decided(self.memory, decision_turn)
        self.recorder.latency.answered(sent)
        self._unanswered = None
        self._unanswered_inline = False
        result = {"sent": sent, "refused": refused}
        if warn:
            result["warning"] = warn
        since = self._since_view(decision_turn)
        if since:
            result["since_your_view"] = since
        # An urgent decision that came while this one was being answered: handed out now, so it is answered next.
        if self.link is not None and not self.game_over:
            self.link.poll()
            if self.link.urgent_pending():
                nxt = self.wait_for_decision(timeout_seconds=0)
                if nxt.get("due"):
                    result["next_decision"] = nxt
                    if not nxt.get("game_over"):
                        self._unanswered = list(nxt["reasons"])   # a wait before answering it hands it out again
                        self._unanswered_inline = True
        return result

    # ---- contingency triggers (triggers.py): fired from the pump, between tool calls ------------------------------------
    def _on_pushed(self, events):
        """The pump read these pushes (the lock is held): every trigger an urgent push sets off fires now."""
        if not self.connected or self.game_over or self.memory is None or not self.memory.triggers or self.state.view is None:
            return
        view = self.state.view
        changed = False
        for ev in events:
            if ev.get("event") != "DECISION_DUE":
                continue
            d = ev.get("data") or {}
            reasons = [r for r in str(d.get("reasons") or "").split(",") if r]
            where = event_where(ev) or prompt.flash_where(view)
            fired = triggers_mod.fire(self.memory, view, reasons, where, self.memory.rally or {}, self._drop_spot,
                                      self._send_trigger_orders)
            for entry in fired:
                entry["due"] = d.get("turn")
                changed = True
        if changed:
            try:
                self.memory.flush()
            except Exception:  # noqa: BLE001 - the counters are kept; they reach the game with the next flush
                pass

    def _drop_spot(self, pos):
        """The game's nearest spot the hand may drop on near `pos` (find_drop_spot), else None; an engine without the
        action answers with the spot itself (the drop's own check then decides)."""
        try:
            r = self.api.call(action="find_drop_spot", player=self.seat, pos=list(pos), radius=15)
        except Exception:  # noqa: BLE001
            return None
        if r.get("success"):
            return r["data"]["pos"]
        return list(pos) if r.get("error") == "UNKNOWN_ACTION" else None

    def _send_trigger_orders(self, orders):
        # decision_seq 0: a trigger's orders answer no decision (they never end a hold the agent's answer is awaited for).
        return submit_batch(self.api, self.seat, orders, self.state.view["turn"], triggers_mod.NOW_MAX_AGE, memory=self.memory,
                            decision_seq=0)

    @_locked
    def set_triggers(self, triggers=None, dry_run=False):
        """Replaces the contingency triggers (the whole list; [] clears them). Checked against the rally points; a trigger
        sent again unchanged keeps its counters. Errors come back as data naming the trigger at fault."""
        self._require_connected()
        try:
            new = triggers_mod.validate(triggers if triggers is not None else [], self.memory.rally or {})
        except triggers_mod.TriggerError as e:
            return e.as_dict()
        if dry_run:
            return {"would_succeed": True, "triggers": [t["name"] for t in new]}
        self.memory.triggers = triggers_mod.keep_counters(new, self.memory.triggers)
        self.memory.flush()
        return {"triggers": [t["name"] for t in self.memory.triggers], "armed": sum(1 for t in self.memory.triggers if t["armed"])}

    def check_triggers(self, triggers=None):
        return self.set_triggers(triggers, dry_run=True)

    @_locked
    def get_triggers(self):
        """The triggers as written (ready to edit and send back), their counters, the rally points and the latest firings."""
        self._require_connected(allow_over=True)
        keys = ("name", "when", "do", "cooldown", "once", "expires_turn")
        return {"triggers": [dict({k: t.get(k) for k in keys}, armed=t.get("armed", True), fired=t.get("fired", 0),
                                  last_turn=t.get("last_turn"), disarmed=t.get("disarmed")) for t in self.memory.triggers or []],
                "rally": self.memory.rally or {}, "fired": list(self.memory.trigger_log or [])[-8:],
                "presets": {k: v["description"] for k, v in triggers_mod.PRESETS.items()}}

    @_locked
    def apply_trigger_preset(self, name=None, overrides=None, dry_run=False):
        """Adds one of the stock triggers (triggers.PRESETS) to the list, or replaces the one of that name. Without a
        name: the presets and what each needs."""
        self._require_connected()
        if not name:
            return {"presets": {k: {"description": v["description"], "needs_rally": v["needs_rally"]} for k, v in triggers_mod.PRESETS.items()}}
        try:
            t = triggers_mod.preset(name, overrides)
        except triggers_mod.TriggerError as e:
            return e.as_dict()
        missing = [r for r in triggers_mod.PRESETS[name]["needs_rally"] if r not in (self.memory.rally or {})]
        if missing:
            return {"error": "NEEDS_RALLY", "detail": "set_rally_points first: %s" % ", ".join(missing)}
        keys = ("name", "when", "do", "cooldown", "once", "expires_turn")
        current = [{k: x.get(k) for k in keys if x.get(k) is not None} for x in self.memory.triggers or [] if x["name"] != t["name"]]
        return self.set_triggers(current + [t], dry_run=dry_run)

    @_locked
    def set_rally_points(self, points=None):
        """Names spots (subtiles [x, y]) the triggers and the flash view refer to; null removes one. Returns them all."""
        self._require_connected()
        rally = dict(self.memory.rally or {})
        m = self.state.view["map"] if self.state.view else {"width": 255, "height": 255}
        for k, v in (points or {}).items():
            if v is None:
                rally.pop(k, None)
                continue
            if not (isinstance(v, list) and len(v) == 2 and all(isinstance(c, int) for c in v)
                    and 0 <= v[0] < 3 * m["width"] and 0 <= v[1] < 3 * m["height"]):
                return {"error": "BAD_POSITION", "name": k, "detail": "a subtile [x, y] on the map"}
            rally[str(k)[:24]] = list(v)
        self.memory.rally = rally
        self.memory.flush()
        return {"rally": rally}

    # ---- combat assistant banks --------------------------------------------------------------------------------------
    @_locked
    def use_combat_bank(self, bank):
        """Makes one of the combat assistant's banks the live set: one packet, in the urgent lane."""
        self._require_connected()
        r = self.api.call(action="use_assist_bank", player=self.seat, bank=int(bank))
        if not r.get("success"):
            return {"error": r.get("error")}
        names = (self.memory.bank_names or {}).get(str(int(bank)), {})
        standing_orders.name_rules(self.memory, names.get("names", []), self.state.view, verb_id=r["data"].get("id"))
        self.memory.flush()
        return {"bank": int(bank), "label": names.get("label"), "live_in_turns": r["data"].get("steps", 1) + 1 + r["data"].get("queued_behind", 0)}

    def _since_view(self, decision_turn):
        """What changed between the view a decision showed and now, in a line: so the agent knows its batch landed on a
        world that moved on."""
        v = self.state.view
        if v is None or decision_turn is None:
            return ""
        news = self.state.last_diff or {}
        evs = ((news.get("own") or {}).get("events") or {}).get("added") or []
        changed = sum(len(r.get("cells", "")) // 2 for r in (news.get("map") or {}).get("changes", []))
        intruders = (v["own"].get("intruders") or {}).get("count", 0)
        parts = ["%d turn(s) passed" % max(0, v["turn"] - decision_turn)]
        if evs:
            parts.append("new events: " + ", ".join(sorted({e.get("kind", "?") for e in evs})))
        if changed:
            parts.append("%d map slab(s) changed" % changed)
        if intruders:
            parts.append("%d intruder(s) on your ground" % intruders)
        return "since your view: " + "; ".join(parts)

    @_locked
    def alerts_text(self):
        """The urgent decisions that came since the agent's last decision and no wait has taken yet, each announced once:
        appended to every other tool's reply (mcp_server), the one interrupt MCP allows."""
        if not self.connected or self.link is None or self.game_over:
            return ""
        fresh = []
        for ev in self.link.urgent_pending():
            d = ev.get("data") or {}
            key = (d.get("seq"), d.get("turn"), d.get("reasons"))
            if key not in self._shown_alerts:
                self._shown_alerts.add(key)
                fresh.append(ev)
        if not fresh:
            return ""
        now = (self.state.view or {}).get("turn", 0)
        lines = []
        for ev in fresh:
            d = ev["data"]
            sev, where = event_severity(ev), event_where(ev)
            lines.append("ALERT turn %s (%s turns ago), severity %d (%s): %s%s" % (
                d.get("turn"), (now - d["turn"]) if isinstance(d.get("turn"), int) else "?", sev, SEVERITY_NAMES.get(sev, "?"),
                str(d.get("reasons", "")).replace(",", ", "),
                " at subtile (%d,%d) = slab (%d,%d)" % (where[0], where[1], where[0] // 3, where[1] // 3) if where else ""))
        intr = prompt.render_intruders((self.state.view or {}).get("own", {}))
        if intr:
            lines.append(intr)
        lines.append("An urgent decision is waiting: wrap up, then wait_for_decision returns it at once (or answer this one with "
                     "submit_orders, whose reply brings it as next_decision).")
        return "\n".join(lines)

    @_locked
    def check_orders(self, orders):
        """Validates a batch (the same checks submit would run, including cost) without spending it or the queue."""
        self._require_connected()
        if self.state.view is None:
            self.state.update(self.api, self.seat)
        decision_turn = self._decision_turn if self._decision_turn is not None else self.state.view["turn"]
        would_succeed, would_fail = submit_batch(self.api, self.seat, orders, decision_turn, self.max_age_turns, dry_run=True, memory=self.memory)
        return {"would_succeed": would_succeed, "would_fail": would_fail}

    @_locked
    def set_speed(self, turns_per_second):
        """Global to the game, not per seat: how many simulated turns run per real second. 0 resets to the configured
        default. See external_seat.h's own docs/refactor/AI/LLM/02 §4a note on the trade-off this makes for a human
        sharing the session."""
        self._require_connected()
        data = self.api.data(action="set_game_speed", turns_per_second=turns_per_second)
        return "turns_per_second is now %s" % data["turns_per_second"]

    # ---- the combat assistant: standing orders for fights (docs/newFeatures/llm/) -----------------------------------
    @_locked
    def set_combat_rules(self, rules=None, dry_run=False, bank=None, label=None, **settings):
        """Replaces the standing orders (`rules`, in priority order, each with an optional "name"; left out = keep them)
        and/or the settings (enabled, reserve, interval, min_gap, max_spend, charge_time). Only what changed is sent;
        it goes live a few turns later, all together. `bank` (1-3, with an optional `label`): store the set in that bank
        instead, to make it live later in one packet (use_combat_bank). Errors come back as data naming the rule at fault."""
        self._require_connected()
        try:
            req, names = standing_orders.prepare(rules, settings)
        except standing_orders.RulesError as e:
            return {"error": e.code, "rule": e.rule, "name": e.name}
        cap = standing_orders.default_cap(req, self.state.view)
        if bank:
            req["bank"] = int(bank)
        r = self.api.call(action="set_assist", player=self.seat, dry_run=bool(dry_run), **req)
        if not r.get("success"):
            out = {"error": r.get("error")}
            if r.get("rule") is not None:
                out["rule"] = r["rule"]
                if r["rule"] < len(names):
                    out["name"] = names[r["rule"]]
            return out
        data = r["data"]
        if bank and not dry_run:
            # A bank's names wait with it; they become the live rules' names when the bank is made live.
            if rules is not None or str(int(bank)) not in (self.memory.bank_names or {}):
                self.memory.bank_names = dict(self.memory.bank_names or {})
                self.memory.bank_names[str(int(bank))] = {"label": label, "names": list(names)}
            elif label:
                self.memory.bank_names[str(int(bank))]["label"] = label
            self.memory.flush()
        elif not dry_run and rules is not None:
            # The names belong to the rules, which go live only when the queued change runs (standing_orders.name_rules).
            standing_orders.name_rules(self.memory, names, self.state.view, verb_id=data.get("id"), unchanged=bool(data.get("unchanged")))
            self.memory.flush()
        out = {"rules_changed": data.get("rules_changed", 0), "packets": data.get("steps", 0)}
        if cap is not None:
            out["max_spend"] = cap
            out["note"] = ("nothing capped what these rules may spend, so the set may spend at most %d gold a pay day "
                           "(max_spend: give another cap, or 0 for none)" % cap)
        if data.get("unchanged"):
            out["unchanged"] = True
        elif dry_run:
            out["would_succeed"] = True
        else:
            out["live_in_turns"] = data.get("steps", 0) + 1 + 2 * data.get("queued_behind", 0)
        return out

    def check_combat_rules(self, rules=None, **settings):
        """set_combat_rules as a dry run: checked exactly as the game would, nothing sent."""
        return self.set_combat_rules(rules, dry_run=True, **settings)

    @_locked
    def get_combat_rules(self):
        """The standing orders as they are live now, by name, ready to edit and send back, and what they did."""
        self._require_connected(allow_over=True)
        self.state.update(self.api, self.seat)
        out = standing_orders.rules_in_words(self.state.view, self.memory)
        out["summary"] = "\n".join(standing_orders.render(self.state.view, self.memory, dict(self.memory._assist_seen)))
        self.memory.flush()   # names adopted (or dropped) on the way
        return out

    @_locked
    def apply_combat_preset(self, name=None, overrides=None, dry_run=False):
        """Applies one of the game's presets (fxdata/combat_assist_presets.toml), with `overrides` (settings, "drop":
        [rule names], "rules": {name: fields}). Without a name: the presets and what each does."""
        self._require_connected()
        presets = self.api.data(action="get_assist_presets").get("presets") or {}
        if not name:
            return {"presets": {n: {"description": p.get("description", ""),
                                    "rules": ["%s: %s" % (r.get("name", "?"), standing_orders.describe(r)) for r in p.get("rules", [])]}
                                for n, p in presets.items()}}
        if name not in presets:
            return {"error": "UNKNOWN_PRESET", "presets": sorted(presets)}
        req = standing_orders.apply_overrides(presets[name], overrides)
        rules = req.pop("rules")
        return self.set_combat_rules(rules, dry_run=dry_run, **req)

    def _refresh_combat(self):
        """The game's combat statistics for the seat (docs/newFeatures/llm/03 P0), kept for the game's record. A game
        too old to have them, or a level already gone, leaves the last ones standing."""
        try:
            self.last_combat = self.api.data(action="get_combat_stats", player=self.seat)
        except Exception:
            pass

    @_locked
    def combat_stats(self):
        """How fast fights are being answered: the game's own measurements for this seat, as text."""
        self._require_connected(allow_over=True)
        self._refresh_combat()
        if self.last_combat is None:
            return "(the game has no combat statistics for this seat)"
        text = combat_report.render_text(self.last_combat)
        chain = combat_report.render_latency_text(self.recorder.latency.summary())
        return text + ("\n" + chain if chain else "")

    @_locked
    def log_tail(self, lines=100):
        """The game's own recent log (get_log_tail), for debugging a confusing session."""
        self._require_connected(allow_over=True)
        data = self.api.data(action="get_log_tail", lines=lines)
        if data["lines"]:
            return "\n".join(data["lines"])
        if data.get("log_level") == "OFF":
            return "(logging is off in this game: LOG_LEVEL=OFF, so it writes no log)"
        return "(log empty or unavailable)"

    @_locked
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

    @_locked
    def experience_text(self):
        """The lessons that apply to this game, in full (for a client whose context has dropped them)."""
        self._require_connected(allow_over=True)
        return experience.render_experience(self.store, self.memory, self.recorder.opponents, full=True) or \
            "There is no experience store in this session (experience_path was \"\")."

    def instructions(self):
        return prompt.SYSTEM_PROMPT

    @_locked
    def recipes(self):
        """The active sacrifice recipes, for a client whose context no longer holds the list shown when the temple was
        built."""
        self._require_connected()
        self.state.update(self.api, self.seat)
        return prompt.render_recipes(self.state.view, full=True)

    def disconnect(self):
        with self._lock:
            self._require_connected(allow_over=True)
            link, self.link = self.link, None
        if link is not None:
            link.stop()   # outside the lock: the pump may be waiting for it
        with self._lock:
            # An attempt still being played stays open in the record: a reconnect carries on with it.
            if self.store is not None:
                self.store.close()
                self.store = None
            self.api.close()
            self.connected = False
            return "disconnected"
