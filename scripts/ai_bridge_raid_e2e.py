#!/usr/bin/env python3
"""Client half of the raid scenario (docs/plans/llm-bridge-realtime.md, Phase 0). Runs scripts/llm_bridge/bridge.py's loop
in-process with the evacuate policy (no model: it lifts the imps near an intruder to the heart) against a real game (see
scripts/run_ftest_ai_bridge_raid.sh), then prints the latency chain from the raid to the hand acting, link by link.
Signals the game side with FLAG0 on player 0 (1 = ok, 2 = failed); the game side then reports the imps lost.

The chain is the yardstick for the later phases. The checks are only that the scenario happened and was measured: a raid
decision came, it was told within RAID_TOLD_TURNS of the raid, and its lift order ran.
--mode triggers plays the raid with no policy at all (Phase 4's acceptance test): an MCP Session in-process sets a rally
point on its own floor farthest from its heart, applies the evacuate_imps trigger preset, and answers every decision with
nothing -- the lift comes from the trigger, sent by the session's pump the moment the game pushes the intruder.
--mode slowthink / hold: an MCP Session plays it as a slow model would: each urgent decision is answered after
--think-seconds of wall clock (3 by default) with a lift of the imps near the cause to the safe pocket -- without and with
the game held for the answer (on_urgent pause, with LLM Pause asked for: Phase 6). The difference is what the hold buys.
--mode reflex: the engine's own hand rule instead (Phase 5): an evacuate_diggers rule of the combat assistant, to the same
pocket, and an agent doing nothing; the seat lifts the imps itself.
Usage: ai_bridge_raid_e2e.py [port] [--result FILE] [--mode policy|triggers|slowthink|hold|reflex] [--think-seconds S]"""
import argparse
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "llm_bridge"))

import bridge  # noqa: E402
import prompt  # noqa: E402
from api import Api  # noqa: E402

RAID_REASONS = ("intruder", "enemy_fight", "heart_attacked", "room_under_attack", "breach", "ground_lost", "room_captured")
RAID_TOLD_TURNS = 5
failures = []


def check(what, cond, detail=""):
    print(("ok   - " if cond else "FAIL - ") + what + ("" if cond else " " + str(detail)))
    if not cond:
        failures.append(what)


def flag(api, n):
    r = api.call(action="read_var", var="FLAG%d" % n, player=0)
    return r.get("data") if r.get("success") else None


def safe_spot(view):
    """The seat's own claimed floor farthest from its heart (the knights head for the heart), as a subtile."""
    m, seat = view["map"], view["seat"]["player"]
    path = next((e["c"] for e in m.get("legend", []) if e["kind"] == "PRETTY_PATH"), None)
    heart = next((r for r in view["own"]["rooms"] if r["kind"] == "DUNGEON_HEART"), None)
    hx, hy = (heart["pos"][0] // 3, heart["pos"][1] // 3) if heart else (m["width"] // 2, m["height"] // 2)
    best = None
    for y, row in enumerate(m["rows"]):
        for x in range(m["width"]):
            if row[2 * x] == path and row[2 * x + 1] == str(seat):
                d = max(abs(x - hx), abs(y - hy))
                if best is None or d > best[0]:
                    best = (d, x, y)
    return [3 * best[1] + 1, 3 * best[2] + 1] if best else None


def run_slow_model(port, hold, think_seconds):
    """The raid answered by a 'model' that takes think_seconds per urgent decision, the game held for it or not. Returns
    (the raid decision's latency record, decisions)."""
    import mcp_session
    import triggers as triggers_mod
    sess = mcp_session.Session()
    sess.connect(host="127.0.0.1", port=port, experience_path="", connect_timeout=90.0, agent="raid-slow",
                 on_urgent="pause" if hold else "none")
    check("the game is held for urgent decisions" if hold else "the game is not held", sess.on_urgent == ("pause" if hold else "none"),
          sess.on_urgent)
    sess.wait_for_decision(timeout_seconds=0.5)                 # start
    sess.submit([], reasoning="start")
    spot = safe_spot(sess.state.view)
    rally = {"safe": spot}
    first, decisions = sess.state.view["turn"], 1
    while sess.state.view["turn"] - first < 900:
        d = sess.wait_for_decision(timeout_seconds=5)
        if not d.get("due"):
            continue
        decisions += 1
        orders = []
        if d.get("flash"):
            time.sleep(think_seconds)                           # the model thinks; the game runs on, or waits
            with sess._lock:
                o, _ = triggers_mod.resolve({"verb": "pick_up_and_drop", "thing_ids": "$imps", "pos": "$drop_near:rally:safe"},
                                            d.get("where") or prompt.flash_where(sess.state.view), sess.state.view, rally, sess._drop_spot)
            orders = [o] if o else []
        sess.submit(orders, reasoning="lift the imps" if orders else "nothing")
    time.sleep(1.0)
    sess.wait_for_decision(timeout_seconds=0.5, treat_timeout_as_decision=True)
    sess.submit([], reasoning="end")
    recs = list(sess.recorder.latency.done)
    sess.disconnect()
    rec = next((r for r in recs if any(x in RAID_REASONS for x in r.get("reasons", [])) and r.get("orders")), None)
    return rec, decisions


def run_reflex(port):
    """The raid with an evacuate_diggers hand rule and an agent that does nothing. Returns (chain record, decisions)."""
    import mcp_session
    sess = mcp_session.Session()
    sess.connect(host="127.0.0.1", port=port, experience_path="", connect_timeout=90.0, agent="raid-reflex", on_urgent="none")
    sess.wait_for_decision(timeout_seconds=0.5)
    spot = safe_spot(sess.state.view)
    r = sess.set_combat_rules([{"name": "evac", "hand": "evacuate_diggers", "to": spot, "count": 8, "radius": 6}], enabled=True)
    check("the hand rule is accepted", "error" not in r, r)
    sess.submit([], reasoning="start")
    first, decisions = sess.state.view["turn"], 1
    while sess.state.view["turn"] - first < 900:
        d = sess.wait_for_decision(timeout_seconds=5)
        if d.get("due"):
            decisions += 1
            sess.submit([], reasoning="nothing: the hand rule does it")
    time.sleep(1.0)
    sess.status()
    v = sess.state.view
    moves = (v["own"].get("assist") or {}).get("reflexes") or []
    results = {x["id"]: x for x in v["seat"].get("results", [])}
    print("REFLEXES " + json.dumps(moves))
    sess.disconnect()
    first_move = moves[0] if moves else None
    if first_move is None:
        return None, decisions
    res = results.get(first_move["verb_id"]) or {}
    rec = {"reasons": ["intruder"], "due": first_move["turn"], "raised": first_move["turn"], "hold": 0, "delivery": 0,
           "think": 0, "answered": res.get("submitted"), "orders": len(first_move.get("things", [])),
           "started": res.get("started") if isinstance(res.get("started"), int) and res.get("started") >= 0 else None,
           "error": res.get("error"), "think_seconds": 0.0, "fired": first_move["turn"]}
    rec["finished"] = res.get("turn") if (res.get("status") == "done" or str(res.get("error") or "").startswith("PARTIAL")) else None
    if isinstance(rec["started"], int) and isinstance(rec["answered"], int):
        rec["queue"] = rec["started"] - rec["answered"]
    if isinstance(rec["finished"], int) and isinstance(rec["started"], int):
        rec["gesture"] = rec["finished"] - rec["started"]
    return rec, decisions


def run_triggers(port):
    """The raid with the evacuate_imps trigger and an agent that does nothing. Returns (chain record, decisions, session view)."""
    import mcp_session
    sess = mcp_session.Session()
    sess.connect(host="127.0.0.1", port=port, experience_path="", connect_timeout=90.0, agent="raid-triggers")
    sess.wait_for_decision(timeout_seconds=0.5)                 # start
    view = sess.state.view
    spot = safe_spot(view)
    check("a safe rally point on the seat's own floor", spot is not None, spot)
    sess.set_rally_points({"safe": spot})
    r = sess.apply_trigger_preset("evacuate_imps")
    check("the evacuate_imps preset is armed", r.get("triggers") == ["evacuate_imps"], r)
    first, decisions = view["turn"], 1
    while sess.state.view["turn"] - first < 900:
        d = sess.wait_for_decision(timeout_seconds=5)
        if d.get("due"):
            decisions += 1
            sess.submit([], reasoning="nothing: the triggers do it")
    # Let the last orders finish, then read what became of them.
    time.sleep(1.0)
    sess.wait_for_decision(timeout_seconds=0.5, treat_timeout_as_decision=True)
    results = {r["id"]: r for r in sess.state.view["seat"].get("results", [])}
    log = list(sess.memory.trigger_log or [])
    print("TRIGGER_LOG " + json.dumps(log))
    final_view = sess.state.view
    sess.disconnect()
    fired = next((e for e in log if e["name"] == "evacuate_imps" and e.get("sent")), None)
    if fired is None:
        return None, decisions, final_view
    first_id = fired["sent"][0].get("id")
    res = results.get(first_id) or {}
    rec = {"reasons": fired["reasons"], "due": fired.get("due"), "fired": fired["turn"], "answered": res.get("submitted"),
           "started": res.get("started") if isinstance(res.get("started"), int) and res.get("started") >= 0 else None,
           "finished": res.get("turn") if (res.get("status") == "done" or str(res.get("error") or "").startswith("PARTIAL")) else None,
           "error": res.get("error"),
           "orders": len(fired["sent"]), "think_seconds": 0.0}
    if isinstance(rec["due"], int):
        rec["raised"], rec["hold"] = rec["due"], 0
        rec["delivery"] = rec["fired"] - rec["due"]          # push -> the pump fired the trigger
    if isinstance(rec["answered"], int):
        rec["think"] = rec["answered"] - rec["fired"]
    if isinstance(rec["started"], int) and isinstance(rec["answered"], int):
        rec["queue"] = rec["started"] - rec["answered"]
    if isinstance(rec["finished"], int) and isinstance(rec["started"], int):
        rec["gesture"] = rec["finished"] - rec["started"]
    return rec, decisions, final_view


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int, nargs="?", default=5599)
    ap.add_argument("--result", default=None)
    ap.add_argument("--mode", choices=["policy", "triggers", "slowthink", "hold", "reflex"], default="policy")
    ap.add_argument("--think-seconds", type=float, default=3.0)
    opts = ap.parse_args()
    probe = Api("127.0.0.1", opts.port)
    probe.connect(90)
    deadline = time.time() + 120
    while time.time() < deadline and flag(probe, 1) != 1:
        time.sleep(0.2)
    if flag(probe, 1) != 1:
        raise SystemExit("the game never signalled that the scene is ready")
    if opts.mode == "hold":
        probe.call(action="set_var", var="FLAG3", value=1, player=0)   # the game allows the agent to pause (LLM Pause)
        deadline = time.time() + 30
        while time.time() < deadline and flag(probe, 3) != 2:
            time.sleep(0.1)
    probe.close()
    time.sleep(1.0)   # the game serves one API client at a time and notices the close on its next poll

    if opts.mode == "triggers":
        trig_rec, n_decisions, final_view = run_triggers(opts.port)
        chains = [trig_rec] if trig_rec else []
        decisions = [None] * n_decisions
        state = None
    elif opts.mode == "reflex":
        rec, n_decisions = run_reflex(opts.port)
        chains = [rec] if rec else []
        decisions = [None] * n_decisions
        state = None
    elif opts.mode in ("slowthink", "hold"):
        rec, n_decisions = run_slow_model(opts.port, opts.mode == "hold", opts.think_seconds)
        chains = [rec] if rec else []
        decisions = [None] * n_decisions
        state = None
    else:
        log = tempfile.NamedTemporaryFile(prefix="raid-", suffix=".jsonl", delete=False).name
        args = bridge.argparse.Namespace(host="127.0.0.1", port=opts.port, claim=None, policy="evacuate", model=None, beat="decision",
                                         min_interval=100, max_wait_seconds=20.0, experience=None, no_experience=True,
                                         max_age_turns=1500, max_decisions=0, max_turns=900, takeover=False, connect_timeout=90.0,
                                         log=log, quiet=False, assist_preset=None)
        summary, state = bridge.run(args)
        print(json.dumps(summary))
        lines = [json.loads(l) for l in open(log)]
        os.unlink(log)
        chains = [c for l in lines for c in (l.get("latency") or [])]
        decisions = [l for l in lines if "decision" in l]

    time.sleep(1.0)
    api = Api("127.0.0.1", opts.port)
    api.connect(30)
    raid_turn = flag(api, 2)
    seat = bridge.find_seat(api, None)
    combat = api.data(action="get_combat_stats", player=seat)
    check("the raid happened (FLAG2 is its turn)", isinstance(raid_turn, int) and raid_turn > 0, raid_turn)
    raid = next((c for c in chains if any(r in RAID_REASONS for r in c.get("reasons", []))
                 and isinstance(c.get("due"), int) and c["due"] >= (raid_turn or 0)), None)
    check({"triggers": "the trigger fired on the raid's push", "reflex": "the hand rule fired on the raid"}.get(opts.mode, "a decision was raised by the raid"), raid is not None,
          [(c.get("reasons"), c.get("due")) for c in chains])
    result = {"raid_turn": raid_turn, "decisions": len(decisions), "mode": opts.mode}
    if raid is not None:
        told = raid["due"] - raid_turn
        check("it was told within %d turns of the raid (%d)" % (RAID_TOLD_TURNS, told), told <= RAID_TOLD_TURNS, raid)
        check({"triggers": "the trigger", "reflex": "the hand rule"}.get(opts.mode, "the policy") + " answered it with a lift", raid.get("orders", 0) >= 1, raid)
        # PARTIAL: some of the things could not be picked (one already fighting, or dead): the lift still ran.
        ran = (raid.get("finished") is not None and not raid.get("error")) or str(raid.get("error") or "").startswith("PARTIAL")
        check("and the lift ran", ran, raid)
        chain = {"raid_to_raised": (raid["raised"] - raid_turn) if isinstance(raid.get("raised"), int) else None}
        for k in ("hold", "delivery", "think", "queue", "gesture", "think_seconds", "reasons"):
            chain[k] = raid.get(k)
        chain["raid_to_hand"] = (raid["started"] - raid_turn) if isinstance(raid.get("started"), int) else None
        chain["raid_to_done"] = (raid["finished"] - raid_turn) if isinstance(raid.get("finished"), int) else None
        result["chain"] = chain
        print("RAID_CHAIN " + json.dumps(chain))
    # What an urgent decision costs to read: the flash text against the full state, on this game's view.
    if state is not None:
        state.update(api, seat)
        full_text = prompt.render_state(state, reasons=["intruder"])
        flash_text = prompt.render_flash(state, None, ["intruder"], 1)
        result["text_bytes"] = {"full": len(full_text), "flash": len(flash_text)}
        print("RAID_TEXT " + json.dumps(result["text_bytes"]))
    fights = (combat.get("fights") or {}).get("recent") or []
    result["fights"] = [{k: f.get(k) for k in ("start", "decision_after", "orders_after", "first_cast_after", "deaths", "digger_deaths")} for f in fights]
    result["deaths"] = {k: (combat.get("deaths") or {}).get(k) for k in ("combat", "combat_diggers", "median_window_low", "median_window_critical")}
    print("RAID_COMBAT " + json.dumps({"fights": result["fights"], "deaths": result["deaths"]}))
    if opts.result:
        with open(opts.result, "w") as f:
            json.dump(result, f, indent=1)
    api.call(action="set_var", var="FLAG0", value=2 if failures else 1, player=0)
    api.close()
    print("\n%d check(s) failed" % len(failures) if failures else "\nall checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:  # noqa: BLE001
        print("FAIL - unexpected error: %r" % (e,))
        import traceback
        traceback.print_exc()
        try:
            a = Api("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 5599)
            a.connect(5)
            a.call(action="set_var", var="FLAG0", value=2, player=0)
        except Exception:  # noqa: BLE001
            pass
        sys.exit(1)
