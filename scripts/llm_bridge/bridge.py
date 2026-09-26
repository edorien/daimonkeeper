#!/usr/bin/env python3
"""Reference bridge: plays one External seat of a running keeperfx in real time with a decision policy.

    python3 scripts/llm_bridge/bridge.py --port 5599 --policy anthropic      # a Claude model plays (needs ANTHROPIC_API_KEY)
    python3 scripts/llm_bridge/bridge.py --port 5599 --policy scripted       # deterministic, no model

The game must run with API_ENABLED=TRUE and have an External seat (Skirmish: Slots & AI -> "External agent (API)"), or pass
--claim PLAYER to convert a computer keeper in a running game. The loop (docs/refactor/AI/LLM/02 section 4a):
    view (a diff after the first) -> policy -> batched, expiring, queued orders -> sleep until the next planning beat
The game is never paused. Beats are quarters of a pay day (--beat quarter), a fixed number of game turns (--beat turns:N)
or wall-clock seconds (--beat seconds:S). Every decision is logged as one JSON line (--log FILE) with timings, token usage
and view sizes, which is the budget data the plan asked for.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import prompt  # noqa: E402
from api import Api, ApiError  # noqa: E402
from viewstate import ViewState  # noqa: E402


def find_seat(api, claim):
    if claim is not None:
        api.data(action="claim_seat", player=claim)
        return claim
    seats = api.data(action="get_seats")["seats"]
    if not seats:
        raise SystemExit("the game has no External seat; start one from Skirmish (Slots & AI) or pass --claim PLAYER")
    return seats[0]["player"]


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

    def sleep_until(self, api, seat, state, due_turn, deadline_wall):
        """Sleep (polling lightly) until the beat. Returns when due."""
        while True:
            if self.kind == "seconds":
                if time.time() >= deadline_wall:
                    return
                time.sleep(min(0.5, max(0.0, deadline_wall - time.time())))
                continue
            state.update(api, seat)
            self.observe(state.view["turn"])
            if state.view["turn"] >= due_turn or state.view["seat"]["victory_state"] != "undecided":
                return
            remaining = due_turn - state.view["turn"]
            time.sleep(min(2.0, max(0.05, remaining / self.turns_per_second() * 0.8)))


def run(args):
    if args.policy == "scripted":
        from policies import ScriptedPolicy
        policy = ScriptedPolicy()
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
    state = ViewState()
    beat = Beat(args.beat)
    log = open(args.log, "a") if args.log else None
    ctx = {}
    decisions, first_turn = 0, None
    started = time.time()
    summary = {"decisions": 0, "orders_sent": 0, "orders_refused": 0}
    try:
        state.update(api, seat)
        while True:
            v = state.view
            beat.observe(v["turn"])
            if first_turn is None:
                first_turn = v["turn"]
            if v["seat"]["victory_state"] != "undecided":
                print("seat is %s; stopping" % v["seat"]["victory_state"])
                break
            if args.max_decisions and decisions >= args.max_decisions:
                break
            if args.max_turns and v["turn"] - first_turn >= args.max_turns:
                break
            t0 = time.time()
            decision = policy.decide(state, ctx)
            think = time.time() - t0
            sent, refused = [], []
            decision_turn = v["turn"]
            for order in decision["orders"]:
                req = prompt.order_to_request(order, seat, decision_turn, args.max_age_turns)
                r = api.call(**req)
                if r.get("success"):
                    sent.append({"verb": order["verb"], "id": r["data"].get("id"), "behind": r["data"].get("queued_behind")})
                else:
                    refused.append({"verb": order.get("verb"), "error": r.get("error")})
            decisions += 1
            summary["decisions"], summary["orders_sent"], summary["orders_refused"] = decisions, summary["orders_sent"] + len(sent), summary["orders_refused"] + len(refused)
            rec = {"decision": decisions, "turn": decision_turn, "think_seconds": round(think, 3), "reasoning": decision["reasoning"],
                   "sent": sent, "refused": refused, "full_view_bytes": state.full_bytes, "diff_view_bytes": state.diff_bytes,
                   "model": dict(ctx.get("metrics", {}))}
            line = json.dumps(rec)
            if log:
                log.write(line + "\n"); log.flush()
            if not args.quiet:
                print("decision %d @turn %d: %s | sent %d refused %d | model %.1fs" % (
                    decisions, decision_turn, decision["reasoning"][:100], len(sent), len(refused), think))
            # Wait for the next beat. Game turns keep running while the model thought, so measure from the view.
            due = beat.next_due_turn(state.view)
            deadline = time.time() + beat.arg if beat.kind == "seconds" else 0
            state.update(api, seat)
            if due is not None and state.view["turn"] >= due:
                due = state.view["turn"] + 1      # the thinking took longer than a beat: go again next turn, never pile up
            beat.sleep_until(api, seat, state, due, deadline)
            state.update(api, seat)
    finally:
        if log:
            log.close()
        api.close()
    summary.update({"view_bytes_full": state.full_bytes, "view_bytes_diff": state.diff_bytes, "diff_views": state.diff_count, "avg_diff_bytes": round(state.diff_bytes / max(1, state.diff_count)), "resyncs": state.resyncs,
                    "wall_seconds": round(time.time() - started, 1), "model": ctx.get("metrics", {})})
    return summary, state


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=5599)
    p.add_argument("--claim", type=int, default=None, help="convert this player's computer keeper into the seat (default: use the seat the game made)")
    p.add_argument("--policy", choices=["scripted", "anthropic"], default="anthropic")
    p.add_argument("--model", default=None, help="model id (default $ANTHROPIC_MODEL or claude-sonnet-5)")
    p.add_argument("--beat", default="quarter", help="quarter | turns:N | seconds:S")
    p.add_argument("--max-age-turns", type=int, default=1500, help="an order not started within this many turns is dropped")
    p.add_argument("--max-decisions", type=int, default=0)
    p.add_argument("--max-turns", type=int, default=0)
    p.add_argument("--takeover", action="store_true", help="arm the built-in AI takeover if this bridge disconnects")
    p.add_argument("--connect-timeout", type=float, default=60.0)
    p.add_argument("--log", default=None)
    p.add_argument("--quiet", action="store_true")
    args = p.parse_args(argv)
    summary, _ = run(args)
    print(json.dumps(summary))
    return 0


if __name__ == "__main__":
    sys.exit(main())
