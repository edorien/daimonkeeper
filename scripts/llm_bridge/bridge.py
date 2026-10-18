#!/usr/bin/env python3
"""Reference bridge: plays one External seat of a running dAImon Keeper game in real time with a decision policy.

    python3 scripts/llm_bridge/bridge.py --port 5599 --policy anthropic      # a Claude model plays (needs ANTHROPIC_API_KEY)
    python3 scripts/llm_bridge/bridge.py --port 5599 --policy scripted       # deterministic, no model

The game must run with API_ENABLED=TRUE and have an External seat (Skirmish: Slots & AI -> "External agent (API)"), or pass
--claim PLAYER to convert a computer keeper in a running game. The loop (docs/refactor/AI/LLM/02 section 4a):
    view (a diff after the first) -> policy -> batched, expiring, queued orders -> sleep until the next planning beat
The game is not paused by this loop; a level (or a loaded save) with no human keeper starts paused for the agent
(agent_only), and the loop resumes it once the decision it prompts is sent. Beats are quarters of a pay day (--beat quarter), a fixed number of game turns (--beat turns:N)
or wall-clock seconds (--beat seconds:S). Every decision is logged as one JSON line (--log FILE) with timings, token usage
and view sizes, which is the budget data the plan asked for.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import experience  # noqa: E402
import memory as memory_mod  # noqa: E402
import standing_orders  # noqa: E402
from api import Api, ApiError  # noqa: E402
from gamelink import GameLink, SEV_ROUTINE, event_severity, event_where, severity_of  # noqa: E402
from orders import submit_batch  # noqa: E402
from viewstate import ViewState  # noqa: E402


def find_seat(api, claim):
    if claim is not None:
        api.data(action="claim_seat", player=claim)
        return claim
    seats = api.data(action="get_seats")["seats"]
    if not seats:
        raise SystemExit("the game has no External seat; start one from Skirmish (Slots & AI) or pass --claim PLAYER")
    return seats[0]["player"]


def resume_agent_pause(api, seat, view):
    """A level with no human keeper starts paused for the agent (the view's agent_only and agent_pause): an autonomous
    loop, which never pauses on its own, lets it run once its first decision is made. Returns whether it resumed."""
    if view.get("agent_only") and view.get("paused") and view.get("agent_pause"):
        api.data(action="set_pause", player=seat, paused=False)
        return True
    return False


class Beat:
    """Decides when the next decision is due, in game turns; also tracks turns per wall second so it can sleep sensibly."""

    def __init__(self, spec):
        kind, _, arg = spec.partition(":")
        self.kind, self.arg = kind, float(arg) if arg else 0.0
        self.samples = []   # (wall time, game turn)

    def observe(self, turn):
        self.samples.append((time.time(), turn))
        self.samples = self.samples[-8:]

    def turns_per_second(self):
        if len(self.samples) < 2 or self.samples[-1][0] == self.samples[0][0]:
            return 20.0
        return max(1.0, (self.samples[-1][1] - self.samples[0][1]) / (self.samples[-1][0] - self.samples[0][0]))

    def next_due_turn(self, view):
        turn = view["turn"]
        if self.kind == "turns":
            return turn + int(self.arg)
        if self.kind == "quarter":
            return turn + max(1, view["seat"].get("payday", {}).get("turns_to_next_quarter", 600))
        return None   # seconds: handled by wall clock

    def sleep_until(self, api, seat, state, due_turn, deadline_wall, pending=None):
        """Sleep (polling lightly) until the beat. Returns when due. `pending`: the pushed events collected so far (a
        GameLink's), when a pump takes them off the socket."""
        while True:
            if self.kind == "seconds":
                if time.time() >= deadline_wall:
                    return
                time.sleep(min(0.5, max(0.0, deadline_wall - time.time())))
                continue
            state.update(api, seat)
            self.observe(state.view["turn"])
            if any(ev.get("event") == "GAME_LOADED" for ev in list(api.events) + list(pending() if pending else [])):
                return      # a save was loaded: the turn went back, and the caller starts over from it
            if state.view["turn"] >= due_turn or state.view["seat"]["victory_state"] != "undecided":
                return
            remaining = due_turn - state.view["turn"]
            time.sleep(min(2.0, max(0.05, remaining / self.turns_per_second() * 0.8)))


def apply_assist_preset(api, seat, memory, name, view=None):
    """Starts the game's combat assistant from one of its presets (standing_orders.py; the automated runs use this, an
    MCP agent calls apply_combat_preset itself). Prints what it did; a game without the assistant is told, not fatal."""
    try:
        presets = api.data(action="get_assist_presets").get("presets") or {}
        if name not in presets:
            print("no combat assistant preset %r (have: %s)" % (name, ", ".join(sorted(presets)) or "none"), file=sys.stderr)
            return
        req = standing_orders.apply_overrides(presets[name], {})
        req, names = standing_orders.prepare(req.pop("rules"), req)
        data = api.data(action="set_assist", player=seat, **req)
        standing_orders.name_rules(memory, names, view, verb_id=data.get("id"), unchanged=bool(data.get("unchanged")))
        memory.flush()
        print("combat assistant: preset %s, %d rule(s), %s packet(s)" % (name, len(names), data.get("steps")))
    except ApiError as e:
        print("combat assistant preset %s not applied: %s" % (name, e.code), file=sys.stderr)


def run(args):
    if args.policy == "scripted":
        from policies import ScriptedPolicy
        policy = ScriptedPolicy()
    elif args.policy == "evacuate":
        from policies import EvacuatePolicy
        policy = EvacuatePolicy()
    else:
        from anthropic_policy import AnthropicPolicy
        policy = AnthropicPolicy(model=args.model)
        if not policy.api_key:
            raise SystemExit("set ANTHROPIC_API_KEY (or use --policy scripted)")

    api = Api(args.host, args.port)
    api.connect(args.connect_timeout)
    seat = find_seat(api, args.claim)
    if args.takeover:
        api.data(action="set_takeover", enabled=True)
    by_decision = args.beat == "decision"
    if by_decision:
        api.data(action="subscribe_event", event="DECISION_DUE")
        api.data(action="subscribe_event", event="GAME_ENDED")
        api.data(action="set_decision_policy", min_interval_turns=args.min_interval)
        if getattr(args, "on_urgent", "none") != "none":
            # Hold the game for each urgent decision's answer (api_urgent_hold.h); refused where a human plays.
            api.data(action="set_decision_policy", min_interval_turns=args.min_interval, on_urgent=args.on_urgent)
    # Either beat: a load replaces the game (and the agent's memory with the one saved), a save notes its turn.
    api.data(action="subscribe_event", event="GAME_LOADED")
    api.data(action="subscribe_event", event="GAME_SAVED")
    state = ViewState()
    beat = Beat("quarter" if by_decision else args.beat)
    log = open(args.log, "a") if args.log else None
    decisions, first_turn = 0, None
    started = time.time()
    summary = {"decisions": 0, "orders_sent": 0, "orders_refused": 0}
    ctx = {}
    store = open_experience(args)
    recorder = experience.Recorder(store, seat, getattr(args, "model", None) or args.policy)
    ctx["recorder"] = recorder
    last_save_turn = None
    link = None
    try:
        state.update(api, seat)
        # The pump reads the game's pushes while the policy thinks (the socket never fills up; each push is timestamped
        # when it came). The main thread holds the link's lock except while the policy thinks and while it waits.
        link = GameLink(api, seat, state, refresh=False)
        link.lock.acquire()
        link.start()
        # The game keeps the agent's memory (and saves it): a seat that already has one is a game being resumed.
        memory, resumed = memory_mod.attach(api, seat, state.view)
        ctx.update({"memory": memory, "reasons": ["restart"] if resumed else ["start"]})
        recorder.start(memory, state.view, resumed)
        if getattr(args, "assist_preset", None):
            apply_assist_preset(api, seat, memory, args.assist_preset, state.view)
        while True:
            v = state.view
            beat.observe(v["turn"])
            if first_turn is None:
                first_turn = v["turn"]
            if v["seat"]["victory_state"] != "undecided":
                print("seat is %s; stopping" % v["seat"]["victory_state"])
                if recorder.ended(memory, v, v["seat"]["victory_state"]):
                    summary["debrief"] = experience.run_debrief(store, memory, recorder, v["seat"]["victory_state"], v, policy, ctx)
                break
            if args.max_decisions and decisions >= args.max_decisions:
                break
            if args.max_turns and v["turn"] - first_turn >= args.max_turns:
                break
            memory.note_results(v["seat"].get("results", []))
            standing_orders.sync_names(v, memory)   # a rule change's names, once its rules are live
            recorder.decision(memory, v, ctx)
            recorder.latency.results(v["seat"].get("results", []))
            due_events = ctx.pop("due_events", [])
            seqs = [(ev.get("data") or {}).get("seq") for ev in due_events]
            decision_seq = max([q for q in seqs if isinstance(q, int)], default=None)
            recorder.latency.delivered(ctx.get("reasons", []), due_events, v["turn"])
            t0 = time.time()
            link.lock.release()
            try:
                decision = policy.decide(state, ctx)
            finally:
                link.lock.acquire()
            think = time.time() - t0
            state.mark_rendered()   # a policy that renders no text still starts the next decision's news from here
            decision_turn = v["turn"]
            sent, refused = submit_batch(api, seat, decision["orders"], decision_turn, args.max_age_turns, memory=memory,
                                         decision_seq=decision_seq)
            if getattr(args, "on_urgent", "none") != "none":
                api.call(**dict({"action": "ack_decision", "player": seat}, **({"seq": decision_seq} if decision_seq else {})))
            recorder.latency.answered(sent)
            decisions += 1
            summary["decisions"], summary["orders_sent"], summary["orders_refused"] = decisions, summary["orders_sent"] + len(sent), summary["orders_refused"] + len(refused)
            warn = memory.update(plan=decision.get("plan"), notes=decision.get("notes"))
            if warn:
                ctx.setdefault("warnings", []).append(warn)
            memory.record_decision(decision_turn, ctx.get("reasons", []), decision["reasoning"], sent, refused)
            recorder.decided(memory, decision_turn)
            # A level, or a loaded save, with no human keeper starts paused for the agent: decided, so play.
            if resume_agent_pause(api, seat, state.view) and not args.quiet:
                print("the game was paused for the agent (level start or load): resumed after the decision")
            rec = {"decision": decisions, "turn": decision_turn, "reasons": ctx.get("reasons", []), "think_seconds": round(think, 3), "reasoning": decision["reasoning"],
                   "plan_updated": decision.get("plan") is not None, "notes_updated": decision.get("notes") is not None,
                   "memory_chars": len(memory.plan) + len(memory.notes), "warnings": ctx.pop("warnings", []),
                   "sent": sent, "refused": refused, "full_view_bytes": state.full_bytes, "diff_view_bytes": state.diff_bytes,
                   "model": dict(ctx.get("metrics", {})),
                   "latency": recorder.latency.take_finished()}   # decisions whose first order has now run (latency.py)
            line = json.dumps(rec)
            if log:
                log.write(line + "\n"); log.flush()
            if not args.quiet:
                print("decision %d @turn %d: %s | sent %d refused %d | model %.1fs" % (
                    decisions, decision_turn, decision["reasoning"][:100], len(sent), len(refused), think))
            # Wait for the next decision. Game turns keep running while the model thought, so measure from the view.
            if by_decision:
                # Something that became due while the model was thinking counts: go again at once, with every reason.
                events = link.take_events()
                deadline = time.time() + args.max_wait_seconds
                while not [ev for ev in events if ev.get("event") != "GAME_SAVED"] and time.time() < deadline:
                    link.wait_for_events(max(0.0, deadline - time.time()))
                    events += link.take_events()
                for ev in events:
                    if ev.get("event") == "GAME_SAVED":
                        last_save_turn = (ev.get("data") or {}).get("turn", last_save_turn)
                if any(ev.get("event") == "GAME_ENDED" for ev in events):
                    result, wanted = recorder.left(memory, state.view, last_save_turn)
                    print("the game left the level (%s); stopping" % result)
                    if wanted:
                        summary["debrief"] = experience.run_debrief(store, memory, recorder, result, state.view, policy, ctx)
                    break
                reasons = []
                loads = [i for i, ev in enumerate(events) if ev.get("event") == "GAME_LOADED"]
                if loads:
                    events = events[loads[-1] + 1:]
                    memory, state = _reload(api, seat, state, ctx)
                    reasons.append("reloaded")
                for ev in events:
                    if ev.get("event") == "DECISION_DUE":
                        reasons += [x for x in ev["data"]["reasons"].split(",") if x and x not in reasons]
                ctx["reasons"] = reasons or ["timer"]
                ctx["due_events"] = [ev for ev in events if ev.get("event") == "DECISION_DUE"]
                # How urgent (a policy shows an urgent one as a flash decision): the most severe push, and its place.
                ctx["severity"], ctx["where"] = severity_of(ctx["reasons"]), None
                for ev in ctx["due_events"]:
                    sev = event_severity(ev)
                    if sev < ctx["severity"] or (sev == ctx["severity"] and ctx["where"] is None):
                        ctx["severity"], ctx["where"] = sev, event_where(ev) or ctx["where"]
                state.update(api, seat)
                continue
            ctx["reasons"] = ["beat"]
            ctx["severity"], ctx["where"] = SEV_ROUTINE, None
            due = beat.next_due_turn(state.view)
            deadline = time.time() + beat.arg if beat.kind == "seconds" else 0
            state.update(api, seat)
            if due is not None and state.view["turn"] >= due:
                due = state.view["turn"] + 1      # the thinking took longer than a beat: go again next turn, never pile up
            beat.sleep_until(api, seat, state, due, deadline, pending=lambda: link.events)
            if any(ev.get("event") == "GAME_LOADED" for ev in link.take_events()):
                memory, state = _reload(api, seat, state, ctx)
                ctx["reasons"] = ["reloaded"]
            state.update(api, seat)
        # The decisions whose first order ran after the last decision: their latency chains, on a closing log line.
        try:
            state.update(api, seat)
            recorder.latency.results(state.view["seat"].get("results", []))
        except (ApiError, OSError, ConnectionError):
            pass
        tail = recorder.latency.take_finished()
        if log and tail:
            log.write(json.dumps({"latency": tail}) + "\n")
    finally:
        if link is not None:
            link.stop()
            link.lock.release()
        if log:
            log.close()
        store.close()
        api.close()
    summary.update({"view_bytes_full": state.full_bytes, "view_bytes_diff": state.diff_bytes, "diff_views": state.diff_count, "avg_diff_bytes": round(state.diff_bytes / max(1, state.diff_count)), "resyncs": state.resyncs,
                    "wall_seconds": round(time.time() - started, 1), "model": ctx.get("metrics", {})})
    return summary, state


def open_experience(args):
    """--experience PATH, experience.sqlite next to the bridge without it, or none with --no-experience."""
    if getattr(args, "no_experience", False):
        return experience.Experience(None)
    return experience.open_store(getattr(args, "experience", None))


def _reload(api, seat, state, ctx):
    """A save was loaded under the bridge: a fresh view, and the memory saved with the game. Returns (memory, state)."""
    reached = state.view["turn"] if state.view else None
    fresh = ViewState()
    fresh.update(api, seat)
    old = ctx.get("memory")
    memory, note = memory_mod.reattach_after_load(api, seat, fresh.view, reached)
    ctx["memory"], ctx["note"] = memory, note
    ctx["recorder"].reloaded(old, reached, memory, fresh.view)
    print(note)
    return memory, fresh


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=5599)
    p.add_argument("--claim", type=int, default=None, help="convert this player's computer keeper into the seat (default: use the seat the game made)")
    p.add_argument("--policy", choices=["scripted", "evacuate", "anthropic"], default="anthropic")
    p.add_argument("--model", default=None, help="model id (default $ANTHROPIC_MODEL or claude-sonnet-5)")
    p.add_argument("--beat", default="decision", help="decision (the game says when) | quarter | turns:N | seconds:S")
    p.add_argument("--min-interval", type=int, default=100, help="decision beat: least game turns between two decisions")
    p.add_argument("--max-wait-seconds", type=float, default=240.0, help="decision beat: think anyway after this long without a decision event")
    p.add_argument("--experience", default=None, help="sqlite file of game records and lessons across games (default: %s)" % experience.default_path())
    p.add_argument("--no-experience", action="store_true", help="keep no game record and read no lessons (a clean run)")
    p.add_argument("--max-age-turns", type=int, default=1500, help="an order not started within this many turns is dropped")
    p.add_argument("--max-decisions", type=int, default=0)
    p.add_argument("--max-turns", type=int, default=0)
    p.add_argument("--takeover", action="store_true", help="arm the built-in AI takeover if this bridge disconnects")
    p.add_argument("--connect-timeout", type=float, default=60.0)
    p.add_argument("--log", default=None)
    p.add_argument("--assist-preset", default=None, help="start the game's combat assistant from this preset (guardian, striker, miser)")
    p.add_argument("--on-urgent", choices=["none", "pause", "slow"], default="none",
                   help="hold the game for each urgent decision's answer (only where an agent may pause: no human keeper, or LLM Pause)")
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args(argv)
    summary, _ = run(args)
    print(json.dumps(summary))
    return 0


if __name__ == "__main__":
    sys.exit(main())
