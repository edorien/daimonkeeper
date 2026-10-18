"""Decision policies. A policy is decide(state, ctx) -> {"reasoning": str, "orders": [order dicts]}.

ScriptedPolicy needs no model: it is the deterministic stand-in the end-to-end test uses, and a template for what a real
policy must do with the view (read the map legend, find claimed floor, spend the gold the state shows).
"""


def _kind_char(view, kind):
    for e in view["map"].get("legend", []):
        if e["kind"] == kind:
            return e["c"]
    return None


def _find_block(view, kind, owner, w, h, near):
    """Nearest w x h block of slabs of `kind` (and owner digit `owner`, or any when None) to slab `near`."""
    ch = _kind_char(view, kind)
    if ch is None:
        return None
    rows = view["map"]["rows"]
    H, W = len(rows), view["map"]["width"]
    best = None
    for y in range(H - h + 1):
        for x in range(W - w + 1):
            ok = all(rows[y + j][2 * (x + i)] == ch and (owner is None or rows[y + j][2 * (x + i) + 1] == str(owner))
                     for j in range(h) for i in range(w))
            if ok:
                d = abs(x - near[0]) + abs(y - near[1])
                if best is None or d < best[0]:
                    best = (d, [x, y, x + w - 1, y + h - 1])
    return best[1] if best else None


class ScriptedPolicy:
    name = "scripted"

    def __init__(self, heal_below=100):
        self.heal_below = heal_below
        self.did_room = False
        self.did_dig = False
        self.healed = set()

    def decide(self, state, ctx):
        v = state.view
        own = v["own"]
        orders, why = [], []
        seat = v["seat"]["player"]
        heart_room = next((r for r in own["rooms"] if r["kind"] == "DUNGEON_HEART"), None)
        near = ((heart_room["pos"][0] // 3, heart_room["pos"][1] // 3) if heart_room else (v["map"]["width"] // 2, v["map"]["height"] // 2))

        if "POWER_HEAL_CREATURE" in own["powers"]:
            for c in own["creatures"]:
                if c["health"] < self.heal_below and c["id"] not in self.healed and not c.get("digger"):
                    orders.append({"verb": "cast_power", "power": "POWER_HEAL_CREATURE", "thing_id": c["id"], "overcharge_turns": 4})
                    self.healed.add(c["id"])
                    why.append("heal #%d" % c["id"])
                    break
        if not self.did_room and not any(r["kind"] == "TREASURE" for r in own["rooms"]):
            block = _find_block(v, "PRETTY_PATH", seat, 3, 3, near)
            if block:
                orders.append({"verb": "build_room", "kind": "TREASURE", "slab_rect": block})
                self.did_room = True
                why.append("treasure room at %s" % (block,))
        if not self.did_dig and len(own["dig_marks"]) < 20:
            block = _find_block(v, "DIRT", None, 3, 3, near)
            if block:
                orders.append({"verb": "mark_dig", "slab_rect": block})
                self.did_dig = True
                why.append("dig %s" % (block,))
        return {"reasoning": "; ".join(why) or "nothing to do this beat", "orders": orders,
                "plan": "scripted: heal the wounded, build one treasure room, mark one dig patch, then wait",
                "notes": "decisions so far: %d" % (ctx["memory"].count + 1 if ctx.get("memory") else 1)}

    def debrief(self, text, ctx):
        """A scripted player learns nothing; it only says how the game went (no lessons, so none are touched)."""
        first = text.splitlines()[0] if text else ""
        return {"summary": "scripted policy: " + first[len("DEBRIEF: "):] if first.startswith("DEBRIEF: ") else first}


class EvacuatePolicy:
    """A reflex stand-in, no model: while enemies stand on the seat's ground (own.intruders), lift its imps near them to
    the heart in one hand trip. The raid scenario (scripts/ai_bridge_raid_e2e.py, docs/plans/llm-bridge-realtime.md
    Phase 0) times how fast this reaches the hand, so the number moves only with the bridge and the engine, never with a
    model's thinking."""
    name = "evacuate"

    def __init__(self, radius=15, max_lift=8):
        self.radius = radius        # subtiles from the nearest intruder
        self.max_lift = max_lift    # one hand trip (MaxThingsInHand in rules.cfg)

    def decide(self, state, ctx):
        own = state.view["own"]
        intruders = (own.get("intruders") or {}).get("creatures") or []
        heart = next((r for r in own["rooms"] if r["kind"] == "DUNGEON_HEART"), None)
        plan = "evacuate: lift imps away from enemies on my ground"
        if not intruders or heart is None:
            return {"reasoning": "no enemy on my ground", "orders": [], "plan": plan}

        def dist(c):
            return min(max(abs(c["pos"][0] - i["pos"][0]), abs(c["pos"][1] - i["pos"][1])) for i in intruders)
        near = sorted((c for c in own["creatures"] if c.get("digger") and c.get("pos") and dist(c) <= self.radius), key=dist)
        ids = [c["id"] for c in near[:self.max_lift]]
        if not ids:
            return {"reasoning": "%d intruder(s), no imp near them" % len(intruders), "orders": [], "plan": plan}
        return {"reasoning": "%d intruder(s): lift %d imp(s) to the heart" % (len(intruders), len(ids)),
                "orders": [{"verb": "pick_up_and_drop", "thing_ids": ids, "to_room": heart["id"]}], "plan": plan}

    debrief = ScriptedPolicy.debrief
