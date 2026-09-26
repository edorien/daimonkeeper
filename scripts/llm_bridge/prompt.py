"""Turns the agent's view into text for a language model, and defines the tool the model answers with."""

SYSTEM_PROMPT = """You are playing a Dungeon Keeper keeper (a dungeon builder) in real time through an API. The game keeps running while you think. You are called about four times per pay day and reply with a short batch of orders that your creatures and imps then carry out over the next minute or so.

Coordinates: the map is a grid of slabs (the `map` rows show two characters per slab: a kind letter and the owner digit, '..' = never seen). Verbs that build, dig or sell take a slab rectangle [x0, y0, x1, y1]. Verbs that touch a spot (place_trap, place_door, drop, cast_power) take a subtile position [x, y]; a slab (sx, sy) has its centre at subtile (3*sx+1, 3*sy+1). Creature and room positions in the state are subtiles.

Verbs: build_room(kind, slab_rect) on claimed floor you own (PRETTY_PATH in the legend; DIRT is diggable earth, PATH is unclaimed floor); mark_dig(slab_rect) to have imps dig; sell(slab_rect) for what you own there; place_trap(kind, pos) and place_door(kind, pos) from your stock; cast_power(power, thing_id | pos | nothing, overcharge_turns 0-32; every 4 held turns is one charge level, and a higher level costs more gold, see the costs listed); slap(thing_id); pick_up(thing_id) then drop(pos); move_creature(thing_id, pos) sends one of your creatures to a subtile (it abandons its work, walks there at creature speed, and holds there) and release_creature(thing_id) hands it back to its normal behaviour, so always release creatures you ordered once they have done their job. The game hands an ordered creature back on its own after hold_turns (default a quarter of a pay day), before pay day, when it is owed pay or getting hungry, and refuses such orders up front (PAYDAY_TOO_CLOSE, OWED_PAY, HUNGRY); SEAT/auto_released in the state tells you which creatures came back. The cost of every order is checked when it starts, so do not queue more than you can pay for. Orders you send now run one after another; earlier orders still running delay them. If an order goes stale it is dropped and reported under LAST ORDERS.

Play to grow a strong dungeon: claim and dig, build treasure room, lair, hatchery, training room and library in that spirit, keep creatures fed and paid, defend the heart, and be ready for events. Answer by calling submit_orders once, with at most ~8 orders. An empty list is a valid answer when waiting is best. You may call look first to see a window of the map."""

TOOLS = [
    {
        "name": "submit_orders",
        "description": "Send a batch of orders for your keeper. They run in order.",
        "input_schema": {
            "type": "object",
            "properties": {
                "reasoning": {"type": "string", "description": "One or two sentences on the plan."},
                "orders": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "properties": {
                            "verb": {"type": "string", "enum": ["build_room", "mark_dig", "sell", "place_trap", "place_door",
                                                                  "cast_power", "slap", "pick_up", "drop", "move_creature", "release_creature", "cancel"]},
                            "kind": {"type": "string", "description": "room, trap or door code name (TREASURE, BOULDER, WOOD...)"},
                            "power": {"type": "string", "description": "power code name for cast_power (POWER_LIGHTNING...)"},
                            "slab_rect": {"type": "array", "items": {"type": "integer"}, "minItems": 4, "maxItems": 4},
                            "pos": {"type": "array", "items": {"type": "integer"}, "minItems": 2, "maxItems": 2},
                            "thing_id": {"type": "integer"},
                            "overcharge_turns": {"type": "integer", "minimum": 0, "maximum": 32},
                            "hold_turns": {"type": "integer", "minimum": 0, "description": "move_creature: release automatically after this many turns (0 = default)"},
                        },
                        "required": ["verb"],
                    },
                },
            },
            "required": ["orders"],
        },
    },
    {
        "name": "look",
        "description": "Show a window of the map you know (slab rectangle, at most 40 x 30), with coordinates.",
        "input_schema": {
            "type": "object",
            "properties": {"slab_rect": {"type": "array", "items": {"type": "integer"}, "minItems": 4, "maxItems": 4}},
            "required": ["slab_rect"],
        },
    },
]

ORDER_FIELDS = ("verb", "kind", "power", "slab_rect", "pos", "thing_id", "overcharge_turns", "hold_turns")


def order_to_request(order, seat, view_turn, max_age_turns):
    """A model order -> the submit_action request (queued, expiring)."""
    req = {"action": "submit_action", "player": seat, "queue": True, "view_turn": view_turn, "max_age_turns": max_age_turns}
    for k in ORDER_FIELDS:
        if k in order and order[k] is not None:
            req[k] = order[k]
    if req.get("verb") == "cancel":
        req.pop("queue", None)
    return req


def render_window(view, rect):
    x0, y0, x1, y1 = rect
    m = view["map"]
    x0, y0 = max(0, min(x0, x1)), max(0, min(y0, y1))
    x1, y1 = min(m["width"] - 1, max(rect[0], rect[2])), min(m["height"] - 1, max(rect[1], rect[3]))
    x1, y1 = min(x1, x0 + 39), min(y1, y0 + 29)
    lines = ["     " + "".join("%-2d" % (x % 100) for x in range(x0, x1 + 1))]
    for y in range(y0, y1 + 1):
        row = m["rows"][y]
        lines.append("%3d  " % y + "".join(row[2 * x: 2 * x + 2] for x in range(x0, x1 + 1)))
    legend = ", ".join("%s=%s" % (e["c"], e["kind"]) for e in m.get("legend", []))
    return "\n".join(lines) + "\nlegend: " + legend + "; owner digit after the letter"


def _fmt_results(view):
    res = view["seat"].get("results", [])
    if not res:
        return "none yet"
    return "; ".join("#%d %s%s" % (r["id"], r["status"], (" " + r["error"]) if r.get("error") else "") for r in res[-10:])


def _map_news(state):
    """A sentence or two on what changed on the map since the last update, from the diff."""
    d = state.last_diff
    if not d or "map" not in d:
        return ""
    m = d["map"]
    changed = sum(len(r["cells"]) // 2 for r in m.get("changes", []))
    revealed = sum(r["len"] for r in m.get("revealed", []))
    parts = []
    if revealed:
        ys = [r["y"] for r in m["revealed"]]
        xs = [r["x"] for r in m["revealed"]] + [r["x"] + r["len"] - 1 for r in m["revealed"]]
        parts.append("%d newly revealed slabs (x %d-%d, y %d-%d)" % (revealed, min(xs), max(xs), min(ys), max(ys)))
    other = changed - revealed
    if other > 0:
        parts.append("%d slabs changed (built/dug/claimed)" % other)
    return "MAP CHANGES: " + "; ".join(parts) if parts else ""


def _fmt_creatures(lst, limit=40):
    out = []
    for c in lst[:limit]:
        out.append("  #%d %s L%d hp%d %s%s at (%d,%d)" % (c["id"], c["kind"], c["level"], c["health"], c["state"],
                                                        " [imp]" if c.get("digger") else "", c["pos"][0], c["pos"][1]))
    if len(lst) > limit:
        out.append("  ... and %d more" % (len(lst) - limit))
    return out


def render_state(state, first=False):
    """Compact text of the current view. `first` adds the whole known map."""
    v = state.view
    seat, own, vis = v["seat"], v["own"], v["visible"]
    pd = seat.get("payday", {})
    lines = ["TURN %d%s | victory: %s | gold %s | pay day: quarter %d of 4 (next quarter in ~%s turns, pay day in ~%s)" % (
        v["turn"], " (paused)" if v["paused"] else "", seat["victory_state"], own["gold"], pd.get("quarter", 0) + 1,
        pd.get("turns_to_next_quarter", "?"), pd.get("turns_to_payday", "?"))]
    lines.append("SEAT player %d | orders waiting: %d | LAST ORDERS: %s" % (seat["player"], seat.get("queued_verbs", 0), _fmt_results(v)))
    if seat.get("ordered_creatures") or seat.get("auto_released"):
        lines.append("ORDERED CREATURES held: %d | handed back automatically: %s" % (seat.get("ordered_creatures", 0), "; ".join(
            "#%d %s @%d" % (a["id"], a["reason"], a["turn"]) for a in seat.get("auto_released", [])) or "none"))
    ev = own.get("events", [])
    if state.last_diff is not None:
        new_ev = state.last_diff.get("own", {}).get("events", {}).get("added", [])
        lines.append("NEW EVENTS: " + ("; ".join("%s at (%d,%d) target %s" % (e["kind"], e["pos"][0], e["pos"][1], e["target"]) for e in new_ev) or "none"))
    elif ev:
        lines.append("EVENTS: " + "; ".join("%s at (%d,%d)" % (e["kind"], e["pos"][0], e["pos"][1]) for e in ev))
    lines.append("CREATURES (%d):" % len(own["creatures"]))
    lines += _fmt_creatures(own["creatures"])
    lines.append("ROOMS: " + ("; ".join("#%d %s %d slabs at (%d,%d)" % (r["id"], r["kind"], r["slabs"], r["pos"][0], r["pos"][1]) for r in own["rooms"]) or "none"))
    stock = own["stock"]
    lines.append("STOCK: traps %s, doors %s" % (stock["traps"] or "none", stock["doors"] or "none"))
    costs = own.get("power_costs", {})
    lines.append("POWERS: " + (", ".join("%s (L0 %d, L2 %d)" % (p, costs[p][0], costs[p][2]) if p in costs else p for p in own["powers"]) or "none"))
    lines.append("DIG MARKS: %d of %d" % (len(own["dig_marks"]), own["dig_marks_limit"]))
    if vis["creatures"]:
        lines.append("VISIBLE OTHER CREATURES:")
        for c in vis["creatures"][:30]:
            lines.append("  #%d %s owner %d%s hp%d at (%d,%d)" % (c["id"], c["kind"], c["owner"], " (ally)" if c.get("ally") else "", c["health"], c["pos"][0], c["pos"][1]))
    news = _map_news(state)
    if news:
        lines.append(news)
    if first:
        m = v["map"]
        lines.append("MAP %dx%d slabs (rows y=0..; 2 chars per slab: kind, owner; '..' unknown); legend: %s" % (
            m["width"], m["height"], ", ".join("%s=%s" % (e["c"], e["kind"]) for e in m.get("legend", []))))
        lines += m["rows"]
    return "\n".join(lines)
