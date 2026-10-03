#!/usr/bin/env python3
"""Agent-vs-agent harness: plays N External seats of one running keeperfx through a single TCP connection.

    python3 scripts/llm_bridge/agent_vs_agent.py --port 5599 --seats 0,1 --policy scripted
    python3 scripts/llm_bridge/agent_vs_agent.py --port 5599 --claim 0,1 --policy scripted   # convert two computer keepers first

The game's TCP API accepts only one client at a time (api.c: listen(srv, 1); a second connection while one is
active is rejected outright), so N independent bridge.py processes cannot connect concurrently. This instead
multiplexes N seats over the one connection this process holds, servicing whichever seat's decision the game
raises next -- each External seat already gets its own independent DECISION_DUE tracking engine-side
(api_seat_decision.c keeps one state machine per player), so no engine change was needed, only a scheduler here
that dispatches by the event's "player" field instead of assuming there is exactly one seat.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import experience  # noqa: E402
import memory as memory_mod  # noqa: E402
from bridge import open_experience  # noqa: E402
from api import Api  # noqa: E402
from orders import submit_batch  # noqa: E402
from viewstate import ViewState  # noqa: E402


class SeatState:
    def __init__(self, player, policy, log_path):
        self.player = player
        self.policy = policy   # each seat gets its own instance: ScriptedPolicy/AnthropicPolicy keep per-game state
        self.view = ViewState()
        self.memory = None       # attached once the seat's first view is in (memory.attach)
        self.recorder = None
        self.note = None
        self.log = open(log_path, "a") if log_path else None
        self.decisions = 0
        self.first_turn = None
        self.last_service_time = time.time()
        self.done = False
        self.summary = {"decisions": 0, "orders_sent": 0, "orders_refused": 0}
        self.ctx = {"reasons": []}   # kept across decisions, like bridge.py's ctx: metrics accumulate


def find_seats(api, claim, seats_arg):
    if claim:
        players = [int(x) for x in claim.split(",") if x != ""]
        for p in players:
            api.data(action="claim_seat", player=p)
        return players
    existing = [s["player"] for s in api.data(action="get_seats")["seats"]]
    if seats_arg:
        wanted = [int(x) for x in seats_arg.split(",") if x != ""]
        missing = [p for p in wanted if p not in existing]
        if missing:
            raise SystemExit("not an External seat: %s (existing: %s)" % (missing, existing))
        return wanted
    if len(existing) < 2:
        raise SystemExit("need at least two External seats for agent-vs-agent; the game has %d (%s)" % (len(existing), existing))
    return existing


def make_policy(name, model):
    if name == "scripted":
        from policies import ScriptedPolicy
        return ScriptedPolicy()
    from anthropic_policy import AnthropicPolicy
    policy = AnthropicPolicy(model=model)
    if not policy.api_key:
        raise SystemExit("set ANTHROPIC_API_KEY (or use --policy scripted)")
    return policy


def service_decision(api, seat, args, reasons):
    v = seat.view.view
    if seat.first_turn is None:
        seat.first_turn = v["turn"]
    seat.memory.note_results(v["seat"].get("results", []))
    seat.recorder.decision(seat.memory, v, seat.ctx)
    ctx = seat.ctx
    ctx["reasons"] = reasons
    ctx["memory"] = seat.memory
    ctx["note"], seat.note = seat.note, None
    t0 = time.time()
    decision = seat.policy.decide(seat.view, ctx)
    think = time.time() - t0
    decision_turn = v["turn"]
    sent, refused = submit_batch(api, seat.player, decision["orders"], decision_turn, args.max_age_turns, memory=seat.memory)
    seat.decisions += 1
    seat.summary["decisions"] = seat.decisions
    seat.summary["orders_sent"] += len(sent)
    seat.summary["orders_refused"] += len(refused)
    seat.memory.update(plan=decision.get("plan"), notes=decision.get("notes"))
    seat.memory.record_decision(decision_turn, reasons, decision["reasoning"], sent, refused)
    seat.recorder.decided(seat.memory, decision_turn)
    rec = {"seat": seat.player, "decision": seat.decisions, "turn": decision_turn, "reasons": reasons,
           "think_seconds": round(think, 3), "sent": sent, "refused": refused, "model": dict(ctx.get("metrics", {}))}
    if seat.log:
        seat.log.write(json.dumps(rec) + "\n")
        seat.log.flush()
    if not args.quiet:
        print("seat %d decision %d @turn %d: %s | sent %d refused %d | %.1fs" % (
            seat.player, seat.decisions, decision_turn, decision["reasoning"][:80], len(sent), len(refused), think))
    seat.last_service_time = time.time()


def run_multi(args):
    """Runs the whole harness to completion (all seats done or timed out) and returns (summaries, seats), summaries
    keyed by player number with the same fields as bridge.run()'s summary."""
    api = Api(args.host, args.port)
    api.connect(args.connect_timeout)
    players = find_seats(api, args.claim, args.seats)
    api.data(action="subscribe_event", event="DECISION_DUE")
    api.data(action="subscribe_event", event="GAME_LOADED")
    api.data(action="set_decision_policy", min_interval_turns=args.min_interval)

    seats = {}
    store = open_experience(args)     # one file, every seat's records (each seat is its own game in it)
    for p in players:
        log = os.path.join(args.log_dir, "seat_%d.jsonl" % p) if args.log_dir else None
        seats[p] = SeatState(p, make_policy(args.policy, args.model), log)
        seats[p].view.update(api, p)
        # The game keeps each seat's memory: a seat that already has one is a game being resumed.
        seats[p].memory, resumed = memory_mod.attach(api, p, seats[p].view.view)
        seats[p].recorder = experience.Recorder(store, p, "%s seat %d" % (args.model or args.policy, p))
        seats[p].recorder.start(seats[p].memory, seats[p].view.view, resumed)
        service_decision(api, seats[p], args, ["restart"] if resumed else ["start"])

    started = time.time()
    try:
        while True:
            for s in seats.values():
                if s.done:
                    continue
                s.view.update(api, s.player)
                if s.view.view["seat"]["victory_state"] != "undecided":
                    print("seat %d is %s; stopping" % (s.player, s.view.view["seat"]["victory_state"]))
                    if s.recorder.ended(s.memory, s.view.view, s.view.view["seat"]["victory_state"]):
                        s.summary["debrief"] = experience.run_debrief(store, s.memory, s.recorder, s.view.view["seat"]["victory_state"],
                                                                      s.view.view, s.policy, s.ctx)
                    s.done = True
                elif args.max_decisions and s.decisions >= args.max_decisions:
                    s.done = True
                elif args.max_turns and s.first_turn is not None and s.view.view["turn"] - s.first_turn >= args.max_turns:
                    s.done = True
            active = [s for s in seats.values() if not s.done]
            if not active:
                break

            events = api.drain_events(("DECISION_DUE", "GAME_LOADED"))
            if not events:
                wait = min(args.max_wait_seconds - (time.time() - s.last_service_time) for s in active)
                ev = api.wait_event(("DECISION_DUE", "GAME_LOADED"), max(0.05, wait))
                events = [ev] if ev else []

            by_player = {}
            loads = [i for i, ev in enumerate(events) if ev.get("event") == "GAME_LOADED"]
            if loads:
                # A save was loaded: every seat starts over from it, with the memory saved with it.
                events = events[loads[-1] + 1:]
                for s in active:
                    reached = s.view.view["turn"]
                    s.view = ViewState()
                    s.view.update(api, s.player)
                    old = s.memory
                    s.memory, s.note = memory_mod.reattach_after_load(api, s.player, s.view.view, reached)
                    s.recorder.reloaded(old, reached, s.memory, s.view.view)
                    by_player[s.player] = ["reloaded"]
            for ev in events:
                p = ev["data"]["player"]
                if p in seats and not seats[p].done:
                    by_player.setdefault(p, []).extend(x for x in ev["data"]["reasons"].split(",") if x)
            now = time.time()
            for s in active:
                if s.player not in by_player and now - s.last_service_time >= args.max_wait_seconds:
                    by_player[s.player] = ["timer"]
            for p, reasons in by_player.items():
                seats[p].view.update(api, p)
                service_decision(api, seats[p], args, reasons or ["timer"])
    finally:
        for s in seats.values():
            if s.log:
                s.log.close()
        store.close()
        api.close()

    summaries = {}
    for p, s in seats.items():
        s.summary.update({"view_bytes_full": s.view.full_bytes, "view_bytes_diff": s.view.diff_bytes,
                           "diff_views": s.view.diff_count, "resyncs": s.view.resyncs,
                           "wall_seconds": round(time.time() - started, 1)})
        summaries[p] = s.summary
    return summaries, seats


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=5599)
    p.add_argument("--claim", default=None, help="comma-separated player numbers to convert into External seats first (default: use existing seats)")
    p.add_argument("--seats", default=None, help="comma-separated player numbers among existing External seats to use (default: all of them)")
    p.add_argument("--policy", choices=["scripted", "anthropic"], default="scripted")
    p.add_argument("--model", default=None, help="model id (anthropic policy only; default $ANTHROPIC_MODEL or claude-sonnet-5)")
    p.add_argument("--min-interval", type=int, default=100, help="least game turns between two DECISION_DUE events, any seat")
    p.add_argument("--max-wait-seconds", type=float, default=240.0, help="think anyway after this long without an event, per seat")
    p.add_argument("--experience", default=None, help="sqlite file of game records and lessons (default: %s)" % experience.default_path())
    p.add_argument("--no-experience", action="store_true", help="keep no game record and read no lessons (a clean run)")
    p.add_argument("--log-dir", default=None, help="directory for one seat_<player>.jsonl decision log per seat")
    p.add_argument("--max-age-turns", type=int, default=1500)
    p.add_argument("--max-decisions", type=int, default=0, help="per seat")
    p.add_argument("--max-turns", type=int, default=0, help="per seat, from that seat's first decision")
    p.add_argument("--connect-timeout", type=float, default=60.0)
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args(argv)
    summaries, _ = run_multi(args)
    print(json.dumps(summaries, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
