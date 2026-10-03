#!/usr/bin/env python3
"""Client half of the External-seat smoke test (docs/refactor/AI/LLM/05-testing-and-rollout.md, 1.2 step 5).

Talks to a running keeperfx over its in-game TCP JSON API (API_ENABLED=TRUE) the way an agent bridge
would: claim a seat, read its view, submit verbs, advance turns, and survive a stuck pause and a
disconnect. Standard library only. Exit status 0 = every check passed.
Usage: ai_bridge_smoke.py [port]   (default 5599)
"""
import json
import socket
import sys
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5599
failures = []


class Api:
    def __init__(self, port):
        self.port = port
        self.sock = None
        self.buf = b""
        self.ack = 0

    def connect(self, timeout=90.0):
        deadline = time.time() + timeout
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=5)
                self.buf = b""
                return
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.25)

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None

    def call(self, **req):
        self.ack += 1
        req["ack"] = self.ack
        self.sock.sendall((json.dumps(req) + "\n").encode())
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        resp = json.loads(line)
        assert resp.get("ack") == self.ack, "response does not match the request: %r" % resp
        return resp


def check(what, cond, detail=""):
    if cond:
        print("ok   - " + what)
    else:
        print("FAIL - %s %s" % (what, detail))
        failures.append(what)


def expect_error(api, what, code, **req):
    r = api.call(**req)
    check(what, (not r.get("success")) and r.get("error") == code, "got %r" % r)


def view(api, seat):
    r = api.call(action="get_player_view", player=seat)
    assert r.get("success"), r
    return r["data"]


def advance(api, seat, turns):
    r = api.call(action="advance_turns", player=seat, turns=turns)
    assert r.get("success"), r
    deadline = time.time() + 60
    while time.time() < deadline:
        v = view(api, seat)
        if v["paused"] and not v["advancing"]:
            return v
        time.sleep(0.02)
    raise TimeoutError("advance_turns did not finish")


def settle(api, seat, limit=200):
    """Advance one turn at a time until the seat's queue is empty (it can then take a new verb)."""
    for _ in range(limit):
        v = view(api, seat)
        if v["seat"]["queued_steps"] == 0:
            return v
        v = advance(api, seat, 1)
    raise TimeoutError("the seat's queue never drained")


def get_flag(api, name):
    r = api.call(action="read_var", var=name, player=0)
    return r["data"] if r.get("success") else None


def main():
    api = Api(PORT)
    api.connect()

    # Wait for the game to be in a level and the game side to be ready (FLAG1 on player 0).
    deadline = time.time() + 120
    while time.time() < deadline:
        r = api.call(action="read_var", var="FLAG1", player=0)
        if r.get("success") and r.get("data") == 1:
            break
        time.sleep(0.25)
    else:
        raise TimeoutError("game side never signalled ready")

    SEAT = 1

    # Before the seat exists, nothing seat-shaped is allowed.
    expect_error(api, "no player given is refused", "MISSING_PLAYER", action="get_player_view")
    expect_error(api, "a player that is not a seat is refused", "NOT_A_VALID_SEAT", action="get_player_view", player=0)
    expect_error(api, "the local human's slot cannot be claimed", "CANNOT_CLAIM_SEAT", action="claim_seat", player=0)

    r = api.call(action="claim_seat", player=SEAT)
    check("claim_seat takes the rival keeper", r.get("success") and r["data"]["user"] == 1, repr(r))

    v = view(api, SEAT)
    check("the game is paused and the agent holds the pause", v["paused"] and v["agent_pause"])
    check("the seat reports itself", v["seat"]["player"] == SEAT and v["seat"]["user"] == 1)
    check("victory state is undecided", v["seat"]["victory_state"] == "undecided")
    check("gold is reported", v["own"]["gold"] >= 5000, "gold=%r" % v["own"].get("gold"))
    orcs = [c for c in v["own"]["creatures"] if c["kind"] == "ORC"]
    check("the seat's creature is listed", len(orcs) == 1, repr(v["own"]["creatures"]))
    check("trap and door stock are listed", v["own"]["stock"]["traps"].get("BOULDER") == 2 and v["own"]["stock"]["doors"].get("WOOD") == 2)
    check("Call to Arms is an available power", "POWER_CALL_TO_ARMS" in v["own"]["powers"])
    check("rooms and the heart are visible", len(v["own"]["rooms"]) >= 1, repr(v["own"]["rooms"]))
    m = v["map"]
    check("the map has one row per slab row and two characters per slab", len(m["rows"]) == m["height"] and all(len(r) == 2 * m["width"] for r in m["rows"]))
    hcx, hcy = orcs[0]["pos"][0] // 3, orcs[0]["pos"][1] // 3
    check("the seat's own territory is revealed in the map", m["rows"][hcy][2 * hcx:2 * hcx + 2] != "..")
    check("unrevealed slabs are '..' (the map is not fully known)", any(".." in [r[i:i + 2] for i in range(0, len(r), 2)] for r in m["rows"]))
    check("the map legend names the slab kinds seen", len(m["legend"]) >= 2 and all("kind" in e for e in m["legend"]))
    check("the visible section is present and lists nothing the seat cannot see yet",
          set(v["visible"]) == {"creatures", "rooms", "traps", "doors"} and all(c["owner"] != 1 for c in v["visible"]["creatures"]))
    print("     (view is %d bytes)" % len(json.dumps(v)))
    orc = orcs[0]
    hx, hy = orc["pos"][0] + 3, orc["pos"][1]  # the heart is 3 subtiles east of where the creature was made

    # Validation happens at submit time, with stable codes.
    expect_error(api, "an unknown verb is refused", "UNKNOWN_VERB", action="submit_action", player=SEAT, verb="dance")
    expect_error(api, "a missing verb is refused", "MISSING_VERB", action="submit_action", player=SEAT)
    expect_error(api, "an unknown trap is refused", "UNKNOWN_KIND", action="submit_action", player=SEAT, verb="place_trap", kind="NOPE", pos=[hx, hy + 6])
    expect_error(api, "a position off the map is refused", "POSITION_OFF_MAP", action="submit_action", player=SEAT, verb="place_trap", kind="BOULDER", pos=[9999, 9999])
    expect_error(api, "a slap this early in the level is refused", "SLAP_NOT_READY", action="submit_action", player=SEAT, verb="slap", thing_id=orc["id"])

    # A queued gesture waits for the game to run: nothing happens while paused.
    r = api.call(action="submit_action", player=SEAT, verb="place_trap", kind="BOULDER", pos=[hx, hy + 6])
    check("place_trap is accepted as two steps", r.get("success") and r["data"]["steps"] == 2, repr(r))
    expect_error(api, "a second gesture is refused while the first is queued", "ACTION_ALREADY_QUEUED",
                 action="submit_action", player=SEAT, verb="place_trap", kind="BOULDER", pos=[hx, hy + 6])
    time.sleep(0.3)
    check("nothing executes while the game is paused", view(api, SEAT)["own"]["stock"]["traps"].get("BOULDER") == 2)
    v = advance(api, SEAT, 5)
    check("the trap was placed (stock 2 -> 1)", v["own"]["stock"]["traps"].get("BOULDER") == 1, repr(v["own"]["stock"]))
    check("the trap is listed among the seat's own traps", any(t["kind"] == "BOULDER" and t["pos"][0] // 3 == hx // 3 and t["pos"][1] // 3 == (hy + 6) // 3 for t in v["own"]["traps"]), repr(v["own"]["traps"]))

    r = api.call(action="submit_action", player=SEAT, verb="place_door", kind="WOOD", pos=[hx + 12, hy])
    check("place_door is accepted", r.get("success"), repr(r))
    v = advance(api, SEAT, 5)
    check("the door was placed (stock 2 -> 1)", v["own"]["stock"]["doors"].get("WOOD") == 1, repr(v["own"]["stock"]))
    check("the door is listed among the seat's own doors", any(d["kind"] == "WOOD" for d in v["own"]["doors"]), repr(v["own"]["doors"]))

    # Slaps are ignored for 10 turns after the last creature drop, counted from level start.
    v = advance(api, SEAT, 25)
    r = api.call(action="submit_action", player=SEAT, verb="slap", thing_id=orc["id"])
    check("a slap is accepted once the level is old enough", r.get("success"), repr(r))
    advance(api, SEAT, 3)
    expect_error(api, "a second slap is refused while the first runs (so the first ran)", "SLAP_NOT_READY",
                 action="submit_action", player=SEAT, verb="slap", thing_id=orc["id"])
    advance(api, SEAT, 40)

    expect_error(api, "drop with an empty hand is refused", "HAND_EMPTY", action="submit_action", player=SEAT, verb="drop", pos=[hx - 3, hy + 3])
    r = api.call(action="submit_action", player=SEAT, verb="pick_up", thing_id=orc["id"])
    check("pick_up is accepted", r.get("success"), repr(r))
    # The pick-up completes over a few turns; the creature's state says when the hand holds it.
    turns_to_pick = 0
    while turns_to_pick < 30:
        v = advance(api, SEAT, 1)
        turns_to_pick += 1
        held = [c for c in v["own"]["creatures"] if c["id"] == orc["id"]]
        if held and held[0]["state"] == "InPowerHand":
            break
    check("the creature ends up in the hand", held and held[0]["state"] == "InPowerHand", "after %d turn(s): %r" % (turns_to_pick, held))
    print("     (pick-up took %d turn(s))" % turns_to_pick)
    r = api.call(action="submit_action", player=SEAT, verb="drop", pos=[hx - 3, hy + 3])
    check("drop is accepted once the hand holds a creature", r.get("success"), repr(r))
    v = advance(api, SEAT, 3)
    moved = [c for c in v["own"]["creatures"] if c["id"] == orc["id"]]
    check("the creature was put down at the target", moved and moved[0]["pos"] == [hx - 3, hy + 3], repr(moved))

    gold_before = v["own"]["gold"]
    r = api.call(action="submit_action", player=SEAT, verb="cast_power", power="POWER_CALL_TO_ARMS", pos=[hx, hy - 6])
    check("cast_power is accepted as two steps", r.get("success") and r["data"]["steps"] == 2, repr(r))
    v = advance(api, SEAT, 4)
    check("the cast ran (the queue drained)", api.call(action="submit_action", player=SEAT, verb="pick_up", thing_id=orc["id"]).get("success"))
    advance(api, SEAT, 3)

    # The area verbs (M4). Coordinates are slabs, relative to the heart.
    hsx, hsy = hx // 3, hy // 3
    room_rect = [hsx + 3, hsy + 4, hsx + 6, hsy + 6]      # 4 x 3 slabs of the seat's own floor
    v = view(api, SEAT)
    check("the dig marks are empty and the engine limit is reported", v["own"]["dig_marks"] == [] and v["own"]["dig_marks_limit"] == 300)
    expect_error(api, "build_room without a rect is refused", "MISSING_RECT", action="submit_action", player=SEAT, verb="build_room", kind="TREASURE")
    expect_error(api, "build_room of an unknown room is refused", "UNKNOWN_KIND", action="submit_action", player=SEAT, verb="build_room", kind="NOPE", slab_rect=room_rect)
    expect_error(api, "an oversized build_room is refused", "AREA_TOO_LARGE", action="submit_action", player=SEAT, verb="build_room", kind="TREASURE", slab_rect=[0, 0, 40, 40])
    expect_error(api, "a mark_dig over the engine's mark limit is refused", "AREA_TOO_LARGE", action="submit_action", player=SEAT, verb="mark_dig", slab_rect=[0, 0, 84, 3])
    expect_error(api, "selling where the seat owns nothing is refused", "NOTHING_TO_SELL", action="submit_action", player=SEAT, verb="sell", slab_rect=[hsx - 9, hsy - 1, hsx - 8, hsy - 1])
    expect_error(api, "an overcharge beyond the cap is refused", "BAD_OVERCHARGE", action="submit_action", player=SEAT, verb="cast_power", power="POWER_CALL_TO_ARMS", pos=[hx, hy - 6], overcharge_turns=99)

    def treasure_slabs(view_):
        return sum(r["slabs"] for r in view_["own"]["rooms"] if r["kind"] == "TREASURE")

    # dry_run: validated exactly like a real submit, but nothing is queued or built either way.
    r = api.call(action="submit_action", player=SEAT, verb="build_room", kind="TREASURE", slab_rect=room_rect, dry_run=True)
    check("a valid order dry-runs to would_succeed with the real step count", r.get("success") and r["data"] == {"would_succeed": True, "steps": 7}, repr(r))
    check("nothing was queued by the dry run", view(api, SEAT)["seat"]["queued_steps"] == 0)
    check("nothing was built by the dry run", treasure_slabs(view(api, SEAT)) == 0)
    expect_error(api, "an invalid order dry-runs to the same error a real submit would give", "UNKNOWN_KIND",
                 action="submit_action", player=SEAT, verb="build_room", kind="NOPE", slab_rect=room_rect, dry_run=True)

    r = api.call(action="submit_action", player=SEAT, verb="build_room", kind="TREASURE", slab_rect=room_rect)
    check("build_room is accepted as seven steps", r.get("success") and r["data"]["steps"] == 7, repr(r))
    for _ in range(40):
        v = advance(api, SEAT, 3)
        if treasure_slabs(v) == 12:
            break
    check("the room was built over the following turns (12 slabs)", treasure_slabs(v) == 12, "slabs=%d" % treasure_slabs(v))

    r = api.call(action="submit_action", player=SEAT, verb="mark_dig", slab_rect=[hsx - 9, hsy - 1, hsx - 6, hsy + 1])
    check("mark_dig is accepted as ten steps", r.get("success") and r["data"]["steps"] == 10, repr(r))
    for _ in range(20):
        v = advance(api, SEAT, 2)
        if len(v["own"]["dig_marks"]) >= 12:
            break
    marks = {tuple(m) for m in v["own"]["dig_marks"]}
    want = {(x, y) for x in range(hsx - 9, hsx - 5) for y in range(hsy - 1, hsy + 2)}
    check("all twelve slabs of the rectangle are marked for digging", want <= marks, "marks=%r" % sorted(marks))

    check("the trap placed earlier is listed", len(v["own"]["traps"]) == 1, repr(v["own"]["traps"]))
    r = api.call(action="submit_action", player=SEAT, verb="sell", slab_rect=[hsx - 1, hsy + 2, hsx + 1, hsy + 2])
    check("sell accepts an area and clicks only the slabs the seat owns", r.get("success") and r["data"]["steps"] >= 5, repr(r))
    for _ in range(10):
        v = advance(api, SEAT, 3)
        if not v["own"]["traps"]:
            break
    check("the trap was sold", v["own"]["traps"] == [], repr(v["own"]["traps"]))
    v = settle(api, SEAT)
    check("once the sell's clicks are all written the seat reports itself idle (queued_steps is 0)", v["seat"]["queued_steps"] == 0)

    # cancel: drop a build_room once its drag has begun, and check the second patch stays bare.
    before = treasure_slabs(v)
    r = api.call(action="submit_action", player=SEAT, verb="build_room", kind="TREASURE", slab_rect=[hsx + 9, hsy + 4, hsx + 10, hsy + 5])
    check("a second build_room is accepted", r.get("success"), repr(r))
    v = advance(api, SEAT, 5)                              # state, drag mode, idle, press, hold: the button is down
    check("the view reports the steps still queued", v["seat"]["queued_steps"] >= 1, repr(v["seat"]))
    r = api.call(action="submit_action", player=SEAT, verb="cancel")
    check("cancel reports the steps it dropped", r.get("success") and r["data"]["steps"] >= 1, repr(r))
    v = advance(api, SEAT, 40)
    check("the queue is empty after the cancel's way out has been written", v["seat"]["queued_steps"] == 0, repr(v["seat"]))
    check("the cancelled build_room built nothing", treasure_slabs(v) == before, "before=%d after=%d" % (before, treasure_slabs(v)))
    r = api.call(action="submit_action", player=SEAT, verb="mark_dig", slab_rect=[hsx - 9, hsy - 1, hsx - 9, hsy - 1])
    check("the seat accepts a new gesture after a cancel", r.get("success"), repr(r))
    advance(api, SEAT, 8)

    # The watchdog: an agent that goes quiet while holding the pause does not wedge the game.
    r = api.call(action="set_pause", player=SEAT, paused=True, watchdog_ms=1500)
    check("set_pause is accepted with a short watchdog", r.get("success"), repr(r))
    time.sleep(3.2)
    v = view(api, SEAT)
    check("the watchdog resumed the game", (not v["paused"]) and (not v["agent_pause"]), repr({k: v[k] for k in ("paused", "agent_pause")}))

    # Disconnect: the agent going away while it holds the pause also resumes the game.
    api.call(action="set_pause", player=SEAT, paused=True, watchdog_ms=600000)
    check("the pause is held", view(api, SEAT)["paused"])
    api.close()
    time.sleep(1.0)
    api.connect(timeout=15)
    v = view(api, SEAT)
    check("losing the client resumed the game", (not v["paused"]) and (not v["agent_pause"]), repr({k: v[k] for k in ("paused", "agent_pause")}))

    # Seat discovery and hand-back (M5): get_seats, an idempotent claim, an explicit release, and the opt-in takeover.
    r = api.call(action="get_seats")
    check("get_seats lists the seat", r.get("success") and [(e["player"], e["user"]) for e in r["data"]["seats"]] == [(SEAT, 1)], repr(r))
    r = api.call(action="claim_seat", player=SEAT)
    check("claim_seat on a seat that is already ours answers with the same user", r.get("success") and r["data"]["user"] == 1, repr(r))
    r = api.call(action="release_seat", player=SEAT)
    check("release_seat hands the seat back to the built-in AI", r.get("success"), repr(r))
    expect_error(api, "a released seat is no longer a seat", "NOT_A_VALID_SEAT", action="get_player_view", player=SEAT)
    expect_error(api, "releasing it twice is refused", "NOT_A_VALID_SEAT", action="release_seat", player=SEAT)
    r = api.call(action="get_seats")
    check("get_seats is empty after the release", r.get("success") and r["data"]["seats"] == [], repr(r))
    r = api.call(action="claim_seat", player=SEAT)
    check("a released slot can be claimed again", r.get("success") and r["data"]["user"] == 1, repr(r))
    expect_error(api, "set_takeover needs a value", "MISSING_ENABLED", action="set_takeover")
    r = api.call(action="set_takeover", enabled=True)
    check("set_takeover is accepted", r.get("success"), repr(r))
    api.close()
    time.sleep(1.0)
    api.connect(timeout=15)
    r = api.call(action="get_seats")
    check("with the takeover armed, losing the client hands the seat to the built-in AI", r.get("success") and r["data"]["seats"] == [], repr(r))
    r = api.call(action="claim_seat", player=SEAT)
    check("the slot can be claimed again after a takeover", r.get("success"), repr(r))
    api.close()
    time.sleep(1.0)
    api.connect(timeout=15)
    r = api.call(action="get_seats")
    check("without the takeover armed, losing the client leaves the seat alone", r.get("success") and [e["player"] for e in r["data"]["seats"]] == [SEAT], repr(r))

    # Real-time cadence (M6): a batch with queue=true, results in the view, staleness bounds, pay day fields.
    api.call(action="set_pause", player=SEAT, paused=True, watchdog_ms=600000)
    v = settle(api, SEAT)
    check("the view has the pay day fields", set(v["seat"]["payday"]) >= {"progress", "gap", "quarter"} and v["seat"]["payday"]["gap"] > 0, repr(v["seat"].get("payday")))
    t0 = v["turn"]
    r = api.call(action="submit_action", player=SEAT, verb="mark_dig", slab_rect=[hsx - 9, hsy - 1, hsx - 9, hsy - 1], view_turn=t0)
    check("a plain submit reports its id and age", r.get("success") and r["data"]["id"] > 0 and r["data"]["age_turns"] == 0, repr(r))
    first = r["data"]["id"]
    expect_error(api, "a second verb without queue=true is refused while one runs", "ACTION_ALREADY_QUEUED",
                 action="submit_action", player=SEAT, verb="mark_dig", slab_rect=[hsx - 9, hsy, hsx - 9, hsy])
    r = api.call(action="submit_action", player=SEAT, verb="mark_dig", slab_rect=[hsx - 9, hsy, hsx - 9, hsy], queue=True, view_turn=t0, max_age_turns=100000)
    check("queue=true accepts a second verb behind the first", r.get("success") and r["data"]["queued_behind"] == 1, repr(r))
    second = r["data"]["id"]
    r = api.call(action="submit_action", player=SEAT, verb="mark_dig", slab_rect=[hsx - 9, hsy + 1, hsx - 9, hsy + 1], queue=True, view_turn=t0, max_age_turns=1)
    check("a third verb, allowed only one turn of age, is accepted now", r.get("success"), repr(r))
    third = r["data"]["id"]
    check("the view counts the waiting verbs", view(api, SEAT)["seat"]["queued_verbs"] == 2)
    expect_error(api, "an order already older than its bound is STALE_VIEW", "STALE_VIEW",
                 action="submit_action", player=SEAT, verb="mark_dig", slab_rect=[hsx - 9, hsy + 2, hsx - 9, hsy + 2],
                 queue=True, view_turn=t0 - 50, max_age_turns=10)
    for _ in range(60):
        v = advance(api, SEAT, 3)
        if v["seat"]["queued_verbs"] == 0 and v["seat"]["queued_steps"] == 0:
            break
    v = advance(api, SEAT, 2)
    res = {e["id"]: e for e in v["seat"]["results"]}
    check("the first two verbs are reported done", res.get(first, {}).get("status") == "done" and res.get(second, {}).get("status") == "done", repr(v["seat"]["results"]))
    check("the third, past its age bound by the time it started, is reported rejected EXPIRED",
          res.get(third, {}).get("status") == "rejected" and res.get(third, {}).get("error") == "EXPIRED", repr(v["seat"]["results"]))
    r = api.call(action="submit_action", player=SEAT, verb="cancel")
    check("cancel on an idle seat succeeds", r.get("success"), repr(r))

    # Diff views: a full view carries a view_id; since=<that id> answers with only the changes; a wrong id resyncs.
    api.call(action="set_pause", player=SEAT, paused=True, watchdog_ms=600000)
    v1 = api.call(action="get_player_view", player=SEAT)["data"]
    check("a plain view is full and numbered", v1.get("mode") == "full" and v1.get("view_id", 0) > 0, repr({k: v1.get(k) for k in ("mode", "view_id")}))
    # Run 3 turns, polling with `since` only (any plain view would replace the baseline the diff is measured against).
    api.call(action="advance_turns", player=SEAT, turns=3)
    last, top, first_diff = v1["view_id"], {"paused": False, "advancing": True}, None
    for _ in range(500):
        d = api.call(action="get_player_view", player=SEAT, since=last)["data"]
        if first_diff is None:
            first_diff = d
        last = d["view_id"]
        top.update({k: d[k] for k in ("turn", "paused", "advancing") if k in d})
        if top["paused"] and not top["advancing"]:
            break
        time.sleep(0.02)
    d = first_diff
    check("since=<last view_id> answers with a diff", d.get("mode") == "diff" and d.get("base") == v1["view_id"], repr({k: d.get(k) for k in ("mode", "base", "turn")}))
    check("the game advanced the three turns", top.get("turn") == v1["turn"] + 3, repr(top))
    check("the diff is much smaller than the full view", len(json.dumps(d)) * 5 < len(json.dumps(v1)), "%d vs %d" % (len(json.dumps(d)), len(json.dumps(v1))))
    v1 = {"view_id": last}
    d2 = api.call(action="get_player_view", player=SEAT, since=v1["view_id"] - 1)["data"]
    check("a stale view_id gets the full view with a reason", d2.get("mode") == "full" and d2.get("reason") == "BASE_MISMATCH", repr({k: d2.get(k) for k in ("mode", "reason")}))
    d3 = api.call(action="get_player_view", player=SEAT, since=9999999)["data"]
    check("an unknown view_id also resyncs", d3.get("mode") == "full", repr(d3.get("mode")))

    # set_alliance: a one-way declaration against the local human keeper (player 0); the engine's own mutual-alliance
    # rule is exercised at the driver level (ftest ai_seat_alliance, which has two External seats to check both sides).
    expect_error(api, "set_alliance needs a target", "MISSING_TARGET_PLAYER", action="submit_action", player=SEAT, verb="set_alliance", enabled=True)
    expect_error(api, "set_alliance needs enabled", "MISSING_ENABLED", action="submit_action", player=SEAT, verb="set_alliance", ally_player=0)
    expect_error(api, "allying with yourself is refused", "INVALID_PLAYER", action="submit_action", player=SEAT, verb="set_alliance", ally_player=SEAT, enabled=True)
    r = api.call(action="submit_action", player=SEAT, verb="set_alliance", ally_player=0, enabled=True)
    check("declaring an alliance with the human is accepted", r.get("success"), repr(r))
    v = advance(api, SEAT, 2)  # the toggle is one queued step; it only takes effect once the game runs it
    check("the view shows it declared but (until the human reciprocates) not mutual", 0 in v["own"]["alliance"]["declared"] and 0 not in v["own"]["alliance"]["mutual"], repr(v["own"]["alliance"]))
    expect_error(api, "declaring it again is refused", "ALREADY_SET", action="submit_action", player=SEAT, verb="set_alliance", ally_player=0, enabled=True)
    r = api.call(action="submit_action", player=SEAT, verb="set_alliance", ally_player="PLAYER0", enabled=False)
    check("a player given by name is accepted, and withdrawing is accepted", r.get("success"), repr(r))
    v = advance(api, SEAT, 2)
    check("the view no longer shows it declared", 0 not in v["own"]["alliance"]["declared"])

    # set_game_speed: global to the game, not per seat; the view's turns_per_second tracks it.
    expect_error(api, "set_game_speed needs a value", "MISSING_TURNS_PER_SECOND", action="set_game_speed")
    expect_error(api, "an out-of-range speed is refused", "BAD_TURNS_PER_SECOND", action="set_game_speed", turns_per_second=999)
    expect_error(api, "a negative speed is refused", "BAD_TURNS_PER_SECOND", action="set_game_speed", turns_per_second=-1)
    default_speed = view(api, SEAT)["turns_per_second"]
    r = api.call(action="set_game_speed", turns_per_second=5)
    check("a slower speed is accepted and echoed back", r.get("success") and r["data"]["turns_per_second"] == 5, repr(r))
    check("the view reflects it", view(api, SEAT)["turns_per_second"] == 5)
    r = api.call(action="set_game_speed", turns_per_second=0)
    check("0 resets to the configured default", r.get("success") and r["data"]["turns_per_second"] == default_speed, repr(r))
    check("the view reflects the reset", view(api, SEAT)["turns_per_second"] == default_speed)

    # get_log_tail: the running game's own log, for debugging without a human tailing the file by hand.
    r = api.call(action="get_log_tail", lines=5)
    check("get_log_tail returns at most the lines asked for", r.get("success") and 0 < len(r["data"]["lines"]) <= 5, repr(r))
    r2 = api.call(action="get_log_tail")
    check("a default (no lines given) is used", r2.get("success") and len(r2["data"]["lines"]) >= len(r["data"]["lines"]), repr(r2))
    check("the lines look like real log output, not something made up", all((":" in l) for l in r2["data"]["lines"]), r2["data"]["lines"][:3])

    # Report to the game side.
    status = 2 if failures else 1
    api.call(action="set_var", var="FLAG0", value=status, player=0)
    api.close()
    print("\n%d check(s) failed" % len(failures) if failures else "\nall checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:  # noqa: BLE001 - a smoke test reports any error as failure
        print("FAIL - unexpected error: %r" % (e,))
        try:
            a = Api(PORT)
            a.connect(timeout=5)
            a.call(action="set_var", var="FLAG0", value=2, player=0)
        except Exception:  # noqa: BLE001
            pass
        sys.exit(1)
