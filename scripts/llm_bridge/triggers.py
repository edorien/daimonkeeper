"""Contingency triggers (docs/plans/llm-bridge-realtime.md, Phase 4): orders the agent writes ahead, on a calm decision,
that the bridge sends the moment the game pushes the urgent decision they are for -- no model in the loop, so the reaction
is the pump's (gamelink.py) plus a couple of turns, against the hundreds a reply takes.

A trigger:

    {"name": "evacuate_east", "when": {"reasons": ["intruder", "enemy_fight"], "region": [44, 50, 52, 58],
                                       "facts": [["imps_near", ">", 0]]},
     "do": [{"verb": "pick_up_and_drop", "thing_ids": "$imps", "pos": "$drop_near:rally:safe"}],
     "cooldown": 300, "once": false, "expires_turn": 40000}

- `when.reasons`: any of these in a DECISION_DUE push (default: every urgent reason). `region` (slabs, inclusive) or
  `near_rally` (a rally name; within 6 slabs of it): where the push's cause must be. `facts`: up to 4 [fact, op, value],
  all must hold, counted around the cause (NEAR subtiles): enemies_near, imps_near, fighters_near, intruders, gold.
- `do`: up to MAX_ORDERS ordinary submit_orders orders, with placeholders resolved when it fires, against the pump's fresh
  view: pos "$where" (the cause), "$rally:NAME", "$drop_near:where" / "$drop_near:rally:NAME" (the nearest spot the hand
  may drop on, from the game: find_drop_spot); thing_ids "$imps[:N]" (own imps near the cause, nearest first),
  "$fighters[:N]" (own fighters near it), "$wounded[:N]" (own fighters near it under WOUNDED_PCT health), "$reserve[:N]"
  (own healthy fighters away from it, nearest first: reinforcements); thing_id "$enemy" (the strongest enemy near it) or
  "$wounded"; to_room "$heart". An order whose placeholder finds nothing is skipped; a trigger with nothing left to send
  does not fire. Its orders go in the urgent lane (priority "now") with a short expiry (NOW_MAX_AGE).
- `cooldown` (turns, default DEFAULT_COOLDOWN) between two firings, `once`, `expires_turn`.

Triggers, their counters, the firing log and the rally points live in the agent's memory, which the game saves: a load
brings back exactly the triggers of that save. A trigger whose orders were all refused twice in a row is disarmed, and
the next decision says why.
"""
import copy

from gamelink import _COMBAT, _CRITICAL

MAX_TRIGGERS = 12
MAX_ORDERS = 4
MAX_FACTS = 4
DEFAULT_COOLDOWN = 300
NOW_MAX_AGE = 200          # a late urgent order is worse than none
NEAR = 18                  # subtiles (6 slabs) around the cause
RALLY_NEAR = 18
WOUNDED_PCT = 35
HAND = 8                   # one hand trip (MaxThingsInHand in the stock rules)
LOG_KEPT = 16
FAIL_LIMIT = 2
DEFAULT_REASONS = list(_CRITICAL + _COMBAT)
FACTS = ("enemies_near", "imps_near", "fighters_near", "intruders", "gold")
OPS = {"<": lambda a, b: a < b, "<=": lambda a, b: a <= b, ">": lambda a, b: a > b, ">=": lambda a, b: a >= b,
       "==": lambda a, b: a == b, "!=": lambda a, b: a != b}
VERBS = ("pick_up_and_drop", "drop", "pick_up", "cast_power", "move_creature", "release_creature", "attack", "slap",
         "power_off", "set_door_lock", "place_door", "place_trap", "sell", "build_room", "mark_dig", "cancel")
_LIST_HOLDERS = ("$imps", "$fighters", "$wounded", "$reserve")


class TriggerError(Exception):
    def __init__(self, code, index=None, name=None, detail=None):
        super().__init__(code)
        self.code, self.index, self.name, self.detail = code, index, name, detail

    def as_dict(self):
        out = {"error": self.code}
        if self.index is not None:
            out["trigger"] = self.index
        if self.name:
            out["name"] = self.name
        if self.detail:
            out["detail"] = self.detail
        return out


# ---- writing them ----------------------------------------------------------------------------------------------------
def _check_placeholder(value, field, rally, i, name):
    if not (isinstance(value, str) and value.startswith("$")):
        return
    base, _, arg = value.partition(":")
    if field == "pos":
        if value == "$where":
            return
        if base == "$rally" or (base == "$drop_near" and arg.startswith("rally:")):
            spot = arg if base == "$rally" else arg[len("rally:"):]
            if spot not in rally:
                raise TriggerError("UNKNOWN_RALLY", i, name, "%s: no rally point %r (set_rally_points)" % (value, spot))
            return
        if value == "$drop_near:where":
            return
    elif field == "thing_ids":
        if base in _LIST_HOLDERS and (arg == "" or (arg.isdigit() and 1 <= int(arg) <= 16)):
            return
    elif field == "thing_id":
        if value in ("$enemy", "$wounded"):
            return
    elif field == "to_room":
        if value == "$heart":
            return
    raise TriggerError("BAD_PLACEHOLDER", i, name, "%s is not a placeholder for %s" % (value, field))


def validate(triggers, rally):
    """The triggers as the agent wrote them, checked and filled with defaults (counters reset). Raises TriggerError."""
    if not isinstance(triggers, list):
        raise TriggerError("NOT_A_LIST")
    if len(triggers) > MAX_TRIGGERS:
        raise TriggerError("TOO_MANY_TRIGGERS", detail="at most %d" % MAX_TRIGGERS)
    out, names = [], set()
    for i, t in enumerate(triggers):
        if not isinstance(t, dict):
            raise TriggerError("NOT_A_TRIGGER", i)
        name = str(t.get("name") or "").strip()
        if not name or len(name) > 32:
            raise TriggerError("MISSING_NAME", i)
        if name in names:
            raise TriggerError("DUPLICATE_NAME", i, name)
        names.add(name)
        when = t.get("when") or {}
        if not isinstance(when, dict):
            raise TriggerError("BAD_WHEN", i, name)
        reasons = when.get("reasons", DEFAULT_REASONS)
        if isinstance(reasons, str):
            reasons = [reasons]
        if not reasons or not all(isinstance(r, str) and r for r in reasons):
            raise TriggerError("BAD_REASONS", i, name)
        w = {"reasons": list(reasons)}
        if when.get("region") is not None:
            reg = when["region"]
            if not (isinstance(reg, list) and len(reg) == 4 and all(isinstance(v, int) for v in reg)):
                raise TriggerError("BAD_REGION", i, name, "region is a slab rectangle [x0, y0, x1, y1]")
            w["region"] = [min(reg[0], reg[2]), min(reg[1], reg[3]), max(reg[0], reg[2]), max(reg[1], reg[3])]
        if when.get("near_rally") is not None:
            if when["near_rally"] not in rally:
                raise TriggerError("UNKNOWN_RALLY", i, name, "near_rally %r" % when["near_rally"])
            w["near_rally"] = when["near_rally"]
        facts = when.get("facts") or []
        if len(facts) > MAX_FACTS:
            raise TriggerError("TOO_MANY_FACTS", i, name)
        for f in facts:
            if not (isinstance(f, list) and len(f) == 3 and f[0] in FACTS and f[1] in OPS and isinstance(f[2], int)):
                raise TriggerError("BAD_FACT", i, name, "%r: [fact, op, value], fact one of %s" % (f, ", ".join(FACTS)))
        w["facts"] = [list(f) for f in facts]
        do = t.get("do") or []
        if not isinstance(do, list) or not (1 <= len(do) <= MAX_ORDERS):
            raise TriggerError("BAD_ORDERS", i, name, "do: 1 to %d orders" % MAX_ORDERS)
        orders = []
        for o in do:
            if not isinstance(o, dict) or o.get("verb") not in VERBS:
                raise TriggerError("BAD_ORDER", i, name, "%r: a verb of %s" % (o.get("verb") if isinstance(o, dict) else o, ", ".join(VERBS)))
            for field in ("pos", "thing_ids", "thing_id", "to_room"):
                if field in o:
                    _check_placeholder(o[field], field, rally, i, name)
            orders.append(dict(o))
        cooldown = t.get("cooldown", DEFAULT_COOLDOWN)
        if not isinstance(cooldown, int) or cooldown < 0:
            raise TriggerError("BAD_COOLDOWN", i, name)
        exp = t.get("expires_turn")
        if exp is not None and not isinstance(exp, int):
            raise TriggerError("BAD_EXPIRES", i, name)
        out.append({"name": name, "when": w, "do": orders, "cooldown": cooldown, "once": bool(t.get("once")),
                    "expires_turn": exp, "armed": True, "fired": 0, "last_turn": None, "fails": 0, "disarmed": None})
    return out


def keep_counters(new, old):
    """A trigger sent again under the same name, unchanged, keeps its counters (a rewrite of the list is not a reset)."""
    by_name = {t["name"]: t for t in old or []}
    for t in new:
        o = by_name.get(t["name"])
        if o and o["when"] == t["when"] and o["do"] == t["do"]:
            for k in ("fired", "last_turn", "fails", "armed", "disarmed"):
                t[k] = o.get(k, t[k])
    return new


# ---- firing them -----------------------------------------------------------------------------------------------------
def _dist(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def _near(lst, where, r=NEAR):
    return sorted((c for c in lst if c.get("pos") and _dist(c["pos"], where) <= r), key=lambda c: _dist(c["pos"], where))


def facts_at(view, where):
    own, vis = view["own"], view["visible"]
    enemies = [c for c in vis.get("creatures", []) if not c.get("ally")]
    mine = own.get("creatures", [])
    return {"enemies_near": len(_near(enemies, where)) if where else len(enemies),
            "imps_near": len([c for c in (_near(mine, where) if where else mine) if c.get("digger")]),
            "fighters_near": len([c for c in (_near(mine, where) if where else mine) if not c.get("digger")]),
            "intruders": (own.get("intruders") or {}).get("count", 0),
            "gold": own.get("gold", 0)}


def matches(trig, reasons, where, view, rally):
    """Whether a push with these reasons, its cause at `where` (subtiles or None), sets the trigger off now."""
    w = trig["when"]
    if not trig.get("armed", True):
        return False
    if trig.get("expires_turn") is not None and view["turn"] > trig["expires_turn"]:
        return False
    if trig.get("last_turn") is not None and view["turn"] - trig["last_turn"] < trig.get("cooldown", DEFAULT_COOLDOWN):
        return False
    if not any(r in w["reasons"] for r in reasons):
        return False
    if "region" in w:
        if where is None:
            return False
        x0, y0, x1, y1 = w["region"]
        if not (x0 <= where[0] // 3 <= x1 and y0 <= where[1] // 3 <= y1):
            return False
    if "near_rally" in w:
        spot = rally.get(w["near_rally"])
        if where is None or spot is None or _dist(where, spot) > RALLY_NEAR:
            return False
    f = facts_at(view, where)
    return all(OPS[op](f[name], value) for name, op, value in w.get("facts", []))


def resolve(order, where, view, rally, drop_spot):
    """One order with its placeholders filled in, or (None, why) when one finds nothing. `drop_spot(pos)` asks the game for
    the nearest spot the hand may drop on (or returns None)."""
    own, vis = view["own"], view["visible"]
    o = dict(order)
    mine = own.get("creatures", [])
    fighters = [c for c in mine if not c.get("digger")]
    for field in ("pos", "thing_ids", "thing_id", "to_room"):
        v = o.get(field)
        if not (isinstance(v, str) and v.startswith("$")):
            continue
        base, _, arg = v.partition(":")
        if field == "pos":
            if v == "$where":
                spot = where
            elif base == "$rally":
                spot = rally.get(arg)
            else:   # $drop_near:where / $drop_near:rally:NAME
                target = where if arg == "where" else rally.get(arg[len("rally:"):])
                spot = drop_spot(target) if target else None
            if not spot:
                return None, "%s: no spot" % v
            o[field] = list(spot)
        elif field == "thing_ids":
            n = int(arg) if arg else HAND
            if where is None and base != "$reserve":
                return None, "%s: the push said nowhere" % v
            if base == "$imps":
                pick = [c for c in _near(mine, where) if c.get("digger")]
            elif base == "$fighters":
                pick = [c for c in _near(mine, where) if not c.get("digger")]
            elif base == "$wounded":
                pick = [c for c in _near(mine, where) if not c.get("digger") and c.get("health_pct", 100) < WOUNDED_PCT]
            else:   # $reserve: healthy fighters away from it, nearest first
                away = [c for c in fighters if c.get("pos") and (where is None or _dist(c["pos"], where) > NEAR)
                        and c.get("health_pct", 100) >= 60 and "Combat" not in str(c.get("state"))]
                pick = sorted(away, key=lambda c: _dist(c["pos"], where)) if where else away
            ids = [c["id"] for c in pick[:min(n, HAND)]]
            if not ids:
                return None, "%s: nobody" % v
            o[field] = ids
        elif field == "thing_id":
            if v == "$enemy":
                enemies = [c for c in vis.get("creatures", []) if not c.get("ally")]
                pick = sorted(_near(enemies, where), key=lambda c: -c.get("health", 0)) if where else []
            else:
                pick = sorted((c for c in _near(mine, where) if not c.get("digger") and c.get("health_pct", 100) < WOUNDED_PCT),
                              key=lambda c: c.get("health_pct", 100)) if where else []
            if not pick:
                return None, "%s: nobody" % v
            o[field] = pick[0]["id"]
        elif field == "to_room":
            heart = next((r for r in own.get("rooms", []) if r["kind"] == "DUNGEON_HEART"), None)
            if heart is None:
                return None, "$heart: no heart"
            o[field] = heart["id"]
    o["priority"] = "now"
    o.setdefault("max_age_turns", NOW_MAX_AGE)
    return o, None


def fire(memory, view, reasons, where, rally, drop_spot, send):
    """Every armed trigger the push sets off: its orders resolved and sent (`send(orders) -> (sent, refused)`). Returns the
    log entries of the triggers that fired."""
    fired = []
    for trig in memory.triggers or []:
        if not matches(trig, reasons, where, view, rally):
            continue
        orders, skipped = [], []
        for o in trig["do"]:
            r, why = resolve(o, where, view, rally, drop_spot)
            (orders.append(r) if r is not None else skipped.append(why))
        if not orders:
            continue   # nothing to do here: not a firing, no cooldown
        sent, refused = send(orders)
        trig["fired"] += 1
        trig["last_turn"] = view["turn"]
        trig["fails"] = (trig.get("fails", 0) + 1) if (refused and not sent) else 0
        if trig["fails"] >= FAIL_LIMIT:
            trig["armed"] = False
            trig["disarmed"] = "its orders were refused %d times in a row (last: %s)" % (
                trig["fails"], ", ".join("%s %s" % (r.get("verb"), r.get("error")) for r in refused))
        if trig.get("once"):
            trig["armed"] = False
            trig["disarmed"] = trig["disarmed"] or "once: it has fired"
        entry = {"turn": view["turn"], "name": trig["name"], "reasons": list(reasons), "where": where,
                 "sent": [{"verb": s.get("verb"), "id": s.get("id"), "summary": s.get("summary")} for s in sent],
                 "refused": [{"verb": r.get("verb"), "error": r.get("error")} for r in refused], "skipped": skipped}
        memory.trigger_log = (memory.trigger_log or []) + [entry]
        del memory.trigger_log[:-LOG_KEPT]
        fired.append(entry)
    return fired


def note_results(memory, results):
    """An order a firing sent that the game then dropped when its turn came (EXPIRED, a check failing) counts against
    its trigger like a refusal at sending."""
    by_id = {r.get("id"): r for r in results or []}
    for entry in memory.trigger_log or []:
        for s in entry["sent"]:
            r = by_id.get(s.get("id"))
            if r is not None and "status" not in s:
                s["status"] = r.get("status")
                if r.get("status") == "rejected":
                    s["error"] = r.get("error")


# ---- showing them ----------------------------------------------------------------------------------------------------
def render(memory, since_turn=None):
    """The TRIGGERS section: each trigger in a line (armed or why not, fired how often), then what fired since `since_turn`."""
    trigs = memory.triggers or []
    if not trigs and not memory.rally:
        return []
    lines = ["TRIGGERS (fired by the bridge the moment their push comes; set_triggers):" if trigs else "TRIGGERS: none"]
    for t in trigs:
        w = t["when"]
        where = (" in slabs %s" % w["region"]) if "region" in w else ((" near rally %s" % w["near_rally"]) if "near_rally" in w else "")
        state = "armed" if t.get("armed", True) else "DISARMED: %s" % t.get("disarmed")
        lines.append("  %s: on %s%s%s -> %s | %s, fired %d%s" % (
            t["name"], ",".join(w["reasons"]) if w["reasons"] != DEFAULT_REASONS else "any urgent reason", where,
            (" if " + " and ".join("%s %s %d" % tuple(f) for f in w["facts"])) if w.get("facts") else "",
            "; ".join("%s %s" % (o["verb"], " ".join("%s=%s" % (k, o[k]) for k in ("thing_ids", "thing_id", "pos", "to_room", "power") if k in o))
                      for o in t["do"]),
            state, t.get("fired", 0), (" (last turn %d)" % t["last_turn"]) if t.get("last_turn") is not None else ""))
    if memory.rally:
        lines.append("  RALLY POINTS (subtiles): " + ", ".join("%s (%d,%d)" % (k, v[0], v[1]) for k, v in sorted(memory.rally.items())))
    recent = [e for e in memory.trigger_log or [] if since_turn is None or e["turn"] > since_turn]
    for e in recent[-6:]:
        lines.append("  FIRED turn %d %s (%s%s): sent %s%s%s" % (
            e["turn"], e["name"], ",".join(e["reasons"]), (" at (%d,%d)" % tuple(e["where"])) if e.get("where") else "",
            "; ".join("#%s %s%s" % (s.get("id"), s.get("summary") or s.get("verb"),
                                    (" " + s["status"] + ((" " + s["error"]) if s.get("error") else "")) if s.get("status") else "")
                      for s in e["sent"]) or "nothing",
            ("; refused " + ", ".join("%s %s" % (r["verb"], r["error"]) for r in e["refused"])) if e["refused"] else "",
            ("; skipped " + "; ".join(e["skipped"])) if e.get("skipped") else ""))
    return lines


# ---- stock triggers --------------------------------------------------------------------------------------------------
PRESETS = {
    "evacuate_imps": {
        "description": ("an enemy on your ground or a fight: lift the imps near it to a safe spot (rally point 'safe'). Imps "
                        "run to defend an attacked heart, so a spot they can walk back from does not keep them out of the fight: "
                        "make 'safe' a pocket of your floor they cannot walk out of (walled in, or across water without a bridge)"),
        "needs_rally": ["safe"],
        "trigger": {"when": {"reasons": ["intruder", "enemy_fight", "room_under_attack", "ground_lost", "alarm", "breach"],
                             "facts": [["imps_near", ">", 0]]},
                    "do": [{"verb": "pick_up_and_drop", "thing_ids": "$imps", "pos": "$drop_near:rally:safe"}],
                    "cooldown": 120},
    },
    "lift_wounded": {
        "description": "a fight: lift your fighters near it under 35% health to a safe spot (rally point 'safe') to recover",
        "needs_rally": ["safe"],
        "trigger": {"when": {"reasons": ["enemy_fight", "heart_attacked", "room_under_attack"]},
                    "do": [{"verb": "pick_up_and_drop", "thing_ids": "$wounded", "pos": "$drop_near:rally:safe"}],
                    "cooldown": 60},
    },
    "reinforce_heart": {
        "description": "your heart attacked: drop up to 6 healthy fighters from elsewhere beside it",
        "needs_rally": [],
        "trigger": {"when": {"reasons": ["heart_attacked"]},
                    "do": [{"verb": "pick_up_and_drop", "thing_ids": "$reserve:6", "pos": "$drop_near:where"}],
                    "cooldown": 200},
    },
}


def preset(name, overrides=None):
    """The stock trigger `name` as a trigger dict named after it, with `overrides` (top-level fields: name, cooldown, once,
    expires_turn, when, do) applied. Raises TriggerError("UNKNOWN_PRESET")."""
    if name not in PRESETS:
        raise TriggerError("UNKNOWN_PRESET", detail="presets: %s" % ", ".join(sorted(PRESETS)))
    t = copy.deepcopy(PRESETS[name]["trigger"])
    t["name"] = name
    for k, v in (overrides or {}).items():
        if k in ("name", "cooldown", "once", "expires_turn", "when", "do"):
            t[k] = copy.deepcopy(v)
    return t
