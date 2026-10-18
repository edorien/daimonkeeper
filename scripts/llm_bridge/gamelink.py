"""One game connection shared by a session's tool calls and a pump thread (docs/plans/llm-bridge-realtime.md, Phase 1).

Before this, nothing read the game between tool calls: while the model thought (tens of seconds), the game's pushes piled
up in the socket, the view went stale, and nothing could tell the agent that a raid had started under the decision it was
still thinking about. The pump runs between tool calls and keeps three things current:

  - the pushed events (DECISION_DUE, GAME_ENDED, GAME_LOADED, GAME_SAVED), read as soon as they arrive (the pump waits on
    the socket), kept in `events` until a wait takes them;
  - the view: a diff every `pump_turns` game turns (about half a second at normal speed; the game builds one in a quarter
    of a millisecond), sooner after an urgent push, so ViewState's news and the state the next decision shows are fresh;
  - the alerts: urgent decisions (severity <= URGENT_SEVERITY) that arrived after the agent's last decision, which every
    other tool reply mentions until a wait takes them (mcp_session.alerts_text) -- the one interrupt MCP allows.

Everything that touches the socket or the session's state holds `lock` (an RLock the session shares), so a tool call and
the pump never interleave a request and its reply. A wait releases it while it waits (`changed`, a Condition on the same
lock), so status and look run while a wait is open. The pump never touches the experience store.
"""
import select
import threading
import time

from api import ApiError, SESSION_EVENTS

# Decision severity, as the engine sends it (api_seat_decision.c reason_severity): 0 critical, 1 combat, 2 problem,
# 3 routine. Mirrored here for an engine that sends no severity, and for decisions no push made (start, timer).
SEV_CRITICAL, SEV_COMBAT, SEV_PROBLEM, SEV_ROUTINE = 0, 1, 2, 3
URGENT_SEVERITY = SEV_COMBAT       # at most this: an alert on other replies, and a flash decision
_CRITICAL = ("heart_attacked", "defeat")
_COMBAT = ("enemy_fight", "room_under_attack", "breach", "intruder", "alarm", "room_captured", "room_destroyed",
           "ground_lost", "room_lost")
_PROBLEM = ("creature_hungry", "treasure_room_full", "need_treasure_room", "creature_annoyed", "no_more_living_set",
            "work_room_unreachable", "storage_room_unreachable", "enemy_door", "creature_scavenged", "assist_budget",
            "assist_reserve", "assist_gold_guard", "sight_open", "prisoner_hungry", "torture_victim_hurt", "chat")
SEVERITY_NAMES = {SEV_CRITICAL: "critical", SEV_COMBAT: "combat", SEV_PROBLEM: "problem", SEV_ROUTINE: "routine"}

PUMP_TURNS = 10          # game turns between two view refreshes
PUMP_MIN_SECONDS = 0.05
PUMP_MAX_SECONDS = 1.0   # also the refresh while paused: nothing moves, but an advance may start


def reason_severity(reason):
    if reason in _CRITICAL:
        return SEV_CRITICAL
    if reason in _COMBAT:
        return SEV_COMBAT
    if reason in _PROBLEM:
        return SEV_PROBLEM
    return SEV_ROUTINE


def severity_of(reasons):
    """A decision's severity: its most severe reason's (routine for none)."""
    return min((reason_severity(r) for r in reasons or []), default=SEV_ROUTINE)


def event_severity(ev):
    """A DECISION_DUE push's severity: the engine's, else worked out from its reasons."""
    d = ev.get("data") or {}
    if isinstance(d.get("severity"), int):
        return d["severity"]
    return severity_of([r for r in str(d.get("reasons") or "").split(",") if r])


def event_where(ev):
    """A DECISION_DUE push's place (subtiles), or None."""
    d = ev.get("data") or {}
    x, y = d.get("where_x"), d.get("where_y")
    return [x, y] if isinstance(x, int) and isinstance(y, int) and x >= 0 and y >= 0 else None


class GameLink:
    def __init__(self, api, seat, state, lock=None, pump_turns=PUMP_TURNS, refresh=True):
        self.api = api
        self.seat = seat
        self.state = state                  # the session's ViewState; replace it with set_state() (a load)
        self.lock = lock if lock is not None else threading.RLock()
        self.changed = threading.Condition(self.lock)
        self.pump_turns = pump_turns
        # Whether the pump refreshes the view too: not for a caller that reads it without the lock (bridge.py's policy
        # thinks on the main thread); its pump only reads the pushes, which still keeps the socket drained.
        self.refresh = refresh
        self.events = []                    # pushed session events not yet taken, oldest first, each with "_wall"
        # Called (holding the lock) with each batch of pushes as they are read, before any wait takes them: the session's
        # triggers (triggers.py) fire from here, so they do not wait for the agent.
        self.on_events = None
        self.error = None                   # what stopped the pump (the connection went away)
        self.polls = 0
        self._stop = threading.Event()
        self._thread = None
        self._next_view = 0.0

    # ---- the pump -----------------------------------------------------------------------------------------------------
    def start(self):
        if self._thread is None:
            self._thread = threading.Thread(target=self._run, name="daimonkeeper-pump", daemon=True)
            self._thread.start()

    def stop(self, timeout=5.0):
        """Stops the pump. Call it without holding `lock` (the pump may be waiting for it)."""
        self._stop.set()
        t, self._thread = self._thread, None
        if t is not None and t is not threading.current_thread():
            t.join(timeout)

    def _interval(self):
        v = self.state.view or {}
        if v.get("paused") and not v.get("advancing"):
            return PUMP_MAX_SECONDS
        tps = v.get("turns_per_second") or 20
        return min(PUMP_MAX_SECONDS, max(PUMP_MIN_SECONDS, self.pump_turns / float(tps)))

    def _run(self):
        while not self._stop.is_set():
            api = self.api
            sock = getattr(api, "sock", None)
            if sock is None:
                return
            wait = max(0.0, self._next_view - time.time())
            try:
                if not (api.events or b"\n" in api.buf):
                    select.select([sock], [], [], min(wait, 0.25))   # short: stop() is noticed soon
            except (OSError, ValueError):
                pass   # closed under us: poll() finds out
            if self._stop.is_set():
                return
            if not self.lock.acquire(timeout=0.2):
                continue   # a tool call holds it; the stop flag is checked again first
            try:
                self.poll()
            except Exception as e:  # noqa: BLE001 - the connection is gone or the reply was not understood
                self.error = e
                self.changed.notify_all()
                return
            finally:
                self.lock.release()

    def poll(self):
        """One pump step (the caller holds `lock`): take what the game pushed, and refresh the view when it is due (or an
        urgent push asks for it). Tool calls may call it too, to be sure they see the latest."""
        evs = self.api.drain_events(SESSION_EVENTS)
        now = time.time()
        urgent = False
        for ev in evs:
            ev["_wall"] = now
            if ev.get("event") == "DECISION_DUE" and event_severity(ev) <= URGENT_SEVERITY:
                urgent = True
        self.events += evs
        if self.refresh and (urgent or now >= self._next_view or (evs and self.on_events is not None)):
            if self.state.view is not None:
                try:
                    self.state.update(self.api, self.seat)
                except ApiError:
                    pass   # the seat is gone (the level ended): the events say so; a connection error still stops the pump
            self._next_view = time.time() + self._interval()
        self.polls += 1
        if evs and self.on_events is not None:
            self.on_events(evs)
        if evs:
            self.changed.notify_all()

    # ---- what tool calls use ------------------------------------------------------------------------------------------
    def take_events(self):
        """The pushed events collected so far (and any the socket holds now), oldest first; they are the caller's."""
        with self.lock:
            self.api_events_now()
            evs, self.events = self.events, []
            return evs

    def api_events_now(self):
        evs = self.api.drain_events(SESSION_EVENTS)
        now = time.time()
        for ev in evs:
            ev["_wall"] = now
        self.events += evs
        if evs and self.on_events is not None:
            if self.refresh and self.state.view is not None:
                try:
                    self.state.update(self.api, self.seat)
                except ApiError:
                    pass
            self.on_events(evs)

    def wait_for_events(self, timeout):
        """Waits (releasing the lock) until an event is collected, the pump fails, or `timeout` seconds pass."""
        deadline = time.time() + max(0.0, timeout)
        with self.changed:
            self.api_events_now()
            while not self.events and self.error is None:
                left = deadline - time.time()
                if left <= 0:
                    break
                if self._thread is None:
                    # No pump (stopped, or never started): wait on the socket ourselves.
                    ev = self.api.wait_event(SESSION_EVENTS, left)
                    if ev is not None:
                        ev["_wall"] = time.time()
                        self.events.append(ev)
                    continue
                self.changed.wait(left)
            if self.error is not None and not self.events:
                raise self.error
            return bool(self.events)

    def urgent_pending(self):
        """The urgent DECISION_DUE pushes collected and not yet taken by a wait."""
        with self.lock:
            return [ev for ev in self.events if ev.get("event") == "DECISION_DUE" and event_severity(ev) <= URGENT_SEVERITY]
