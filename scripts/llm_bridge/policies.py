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
