#!/usr/bin/env python3
"""Client half of the reference-bridge end-to-end test. Runs scripts/llm_bridge/bridge.py's loop in-process with the scripted
policy against a real game (see scripts/run_ftest_ai_bridge_reference.sh), then checks what the bridge did and that a diff-patched
view equals a fresh full view. Signals the game side with FLAG0 on player 0 (1 = ok, 2 = failed).
Usage: ai_bridge_reference_e2e.py [port]"""
import argparse
import json
import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "llm_bridge"))

import bridge  # noqa: E402
from api import Api  # noqa: E402

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5599
failures = []


def check(what, cond, detail=""):
    print(("ok   - " if cond else "FAIL - ") + what + ("" if cond else " " + str(detail)))
    if not cond:
        failures.append(what)


def strip(view):
    return canon({k: v for k, v in view.items() if k not in ("view_id", "mode", "base", "reason")})


def canon(x):
    """Order-insensitive form: the game lists rooms/legend entries in an arbitrary but equivalent order."""
    if isinstance(x, dict):
        return {k: canon(v) for k, v in x.items()}
    if isinstance(x, list):
        return sorted((canon(v) for v in x), key=lambda v: json.dumps(v, sort_keys=True))
    return x


def explain(patched, full):
    """Where two views differ, briefly."""
    out = []
    for k in full:
        a, b = patched.get(k), full[k]
        if a == b:
            continue
        if isinstance(a, dict) and isinstance(b, dict):
            for kk in b:
                if a.get(kk) != b[kk]:
                    if kk == "rows":
                        bad = [y for y in range(len(b[kk])) if a[kk][y] != b[kk][y]]
                        out.append("%s.rows differ in %d row(s), e.g. y=%s: patched %r full %r" % (k, len(bad), bad[:3], a[kk][bad[0]][:60], b[kk][bad[0]][:60]) if bad else "%s.rows differ in length" % k)
                    else:
                        out.append("%s.%s: patched %.120r full %.120r" % (k, kk, a.get(kk), b[kk]))
        else:
            out.append("%s: patched %.120r full %.120r" % (k, a, b))
    return out


def main():
    # The game side prepares the scene, then raises FLAG1 on player 0; the bridge must not start before that.
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

    log = tempfile.NamedTemporaryFile(prefix="bridge-", suffix=".jsonl", delete=False).name
    args = bridge.argparse.Namespace(host="127.0.0.1", port=PORT, claim=None, policy="scripted", model=None, beat="turns:150",
                                     max_age_turns=1500, max_decisions=4, max_turns=0, takeover=False, connect_timeout=90.0,
                                     log=log, quiet=False)
    summary, state = bridge.run(args)
    print(json.dumps(summary))

    check("four decisions were made", summary["decisions"] == 4, summary)
    check("orders were sent and none refused", summary["orders_sent"] >= 3 and summary["orders_refused"] == 0, summary)
    check("the views after the first were diffs", summary["view_bytes_diff"] > 0 and summary["resyncs"] == 0, summary)
    check("a diff view is far smaller than the full one (%d B average over %d diffs vs %d B)" % (
        summary["avg_diff_bytes"], summary["diff_views"], summary["view_bytes_full"]),
        summary["diff_views"] > 0 and summary["avg_diff_bytes"] * 5 < summary["view_bytes_full"], summary)
    lines = [json.loads(l) for l in open(log)]
    check("the log has one line per decision, in real time (the game was never paused)", len(lines) == 4 and lines[-1]["turn"] > lines[0]["turn"], lines)
    check("the first decision sent the three scripted orders", [o["verb"] for o in lines[0]["sent"]] == ["cast_power", "build_room", "mark_dig"], lines[0])

    # Patched view == fresh full view, at one instant: pause, take a diff on the bridge's baseline, then a full view.
    time.sleep(1.0)
    api = Api("127.0.0.1", PORT)
    api.connect(30)
    api.call(action="set_var", var="FLAG1", value=2, player=0)   # tells the game the bridge phase is over: pausing is now allowed
    seat = bridge.find_seat(api, None)
    api.data(action="set_pause", player=seat, paused=True)
    state.update(api, seat)
    patched = strip(state.view)
    raw_full = api.data(action="get_player_view", player=seat)
    full = strip(raw_full)
    state.view_id = raw_full["view_id"]          # a full fetch replaces the baseline; follow it, as any client must
    state.view = {k: v for k, v in raw_full.items() if k not in ("mode", "reason")}
    check("the diff-patched view equals a fresh full view", patched == full, "\n      ".join(explain(patched, full)))
    api.data(action="set_pause", player=seat, paused=False)

    # A hand-fed diff cycle on top: change something, take a diff, patch, compare again (the check above may be trivial when
    # nothing changed since the last update).
    time.sleep(1.0)
    api.data(action="set_pause", player=seat, paused=True)
    state.update(api, seat)
    if state.last_diff is not None:
        print("last diff sizes by key:", {k: len(json.dumps(v)) for k, v in state.last_diff.items()})
    check("this update was a diff", state.last_diff is not None, "resynced")
    patched = strip(state.view)
    raw_full = api.data(action="get_player_view", player=seat)
    full = strip(raw_full)
    check("after more game time, patch + diff still equals the full view", patched == full, [k for k in full if patched.get(k) != full[k]])
    check("the diff carried real changes (turn moved)", state.last_diff is not None and "turn" in state.last_diff)
    api.data(action="set_pause", player=seat, paused=False)

    api.call(action="set_var", var="FLAG0", value=2 if failures else 1, player=0)
    api.close()
    os.unlink(log)
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
            a = Api("127.0.0.1", PORT)
            a.connect(5)
            a.call(action="set_var", var="FLAG0", value=2, player=0)
        except Exception:  # noqa: BLE001
            pass
        sys.exit(1)
