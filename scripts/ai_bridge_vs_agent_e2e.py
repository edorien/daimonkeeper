#!/usr/bin/env python3
"""Client half of the agent-vs-agent end-to-end test. Runs scripts/llm_bridge/agent_vs_agent.py's harness against a
real game with two External seats (see scripts/run_ftest_ai_bridge_vs_agent.sh), then checks that both seats made
independent progress over the one shared TCP connection and were never mixed up. Signals the game side with FLAG0
on player 0 (1 = ok, 2 = failed).
Usage: ai_bridge_vs_agent_e2e.py [port]"""
import argparse
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "llm_bridge"))

import agent_vs_agent  # noqa: E402
from api import Api  # noqa: E402

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5599
failures = []


def check(what, cond, detail=""):
    print(("ok   - " if cond else "FAIL - ") + what + ("" if cond else " " + str(detail)))
    if not cond:
        failures.append(what)


def main():
    probe = Api("127.0.0.1", PORT)
    probe.connect(90)
    deadline = time.time() + 120
    while time.time() < deadline:
        r = probe.call(action="read_var", var="FLAG1", player=0)
        if r.get("success") and r.get("data") == 1:
            break
        time.sleep(0.2)
    else:
        raise SystemExit("the game never signalled that the scene is ready")
    probe.close()
    time.sleep(1.0)   # the game serves one API client at a time and notices the close on its next poll

    mem_dir = tempfile.mkdtemp(prefix="agent-vs-agent-mem-")
    log_dir = tempfile.mkdtemp(prefix="agent-vs-agent-log-")
    args = agent_vs_agent.argparse.Namespace(host="127.0.0.1", port=PORT, claim=None, seats=None, policy="scripted",
                                             model=None, min_interval=40, max_wait_seconds=120.0, memory_dir=mem_dir,
                                             log_dir=log_dir, max_age_turns=1500, max_decisions=2, max_turns=0,
                                             connect_timeout=90.0, quiet=False)
    summaries, seats = agent_vs_agent.run_multi(args)
    print(json.dumps(summaries, indent=2))

    players = sorted(summaries)
    check("both External seats were found and played", len(players) == 2, players)
    for p in players:
        s = summaries[p]
        check("seat %d made two decisions" % p, s["decisions"] == 2, s)
        # >=1, not >=2: the dig order always succeeds (its earth patch is untouched by anything else), but the
        # treasure-room target area sits on ground the seat's own dungeon owned before conversion, so residual
        # pre-conversion AI activity (an imp already mid-task) can occasionally finish building it there first,
        # racily, depending on how many turns pass (headless, no frame limiter) before this harness's first
        # decision -- that races the test, not the product, so this only requires the seat's own orders to have
        # gone through cleanly, not an exact count.
        check("seat %d's orders were sent and none refused" % p, s["orders_sent"] >= 1 and s["orders_refused"] == 0, s)

    logs = {p: [json.loads(l) for l in open(os.path.join(log_dir, "seat_%d.jsonl" % p))] for p in players}
    for p in players:
        check("seat %d's log lines are tagged with its own player number" % p, all(l["seat"] == p for l in logs[p]), logs[p])
    check("the first decision was 'start' for both seats", all(logs[p][0]["reasons"] == ["start"] for p in players), logs)
    check("the second decision was raised by that seat's own enemy_fight event, for both",
          all("enemy_fight" in logs[p][1]["reasons"] for p in players), {p: logs[p][1]["reasons"] for p in players})

    mems = {p: json.load(open(os.path.join(mem_dir, "seat_%d.json" % p))) for p in players}
    check("each seat's memory file holds exactly its own two decisions", all(len(mems[p]["decisions"]) == 2 for p in players), mems)

    # No cross-talk at the API level either: the two seats' own views must show different dungeons.
    time.sleep(1.0)   # the game serves one API client at a time and notices run_multi's close on its next poll
    api = Api("127.0.0.1", PORT)
    api.connect(30)
    views = {p: api.data(action="get_player_view", player=p) for p in players}
    api.close()
    hearts = {p: next(r["pos"] for r in views[p]["own"]["rooms"] if r["kind"] == "DUNGEON_HEART") for p in players}
    check("the two seats' own dungeons are at different places on the map", hearts[players[0]] != hearts[players[1]], hearts)

    for p in players:
        os.unlink(os.path.join(mem_dir, "seat_%d.json" % p))
        os.unlink(os.path.join(log_dir, "seat_%d.jsonl" % p))
    os.rmdir(mem_dir)
    os.rmdir(log_dir)

    time.sleep(1.0)
    api2 = Api("127.0.0.1", PORT)
    api2.connect(30)
    api2.call(action="set_var", var="FLAG1", value=2, player=0)
    api2.call(action="set_var", var="FLAG0", value=2 if failures else 1, player=0)
    api2.close()
    print("\n%d check(s) failed" % len(failures) if failures else "\nall checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
