"""Turns the agent's view into text for a language model, and defines the tool the model answers with."""
import re

SYSTEM_PROMPT = """You are playing a Dungeon Keeper keeper (a dungeon builder) in real time through an API. The game keeps running while you think. You are called when a decision is due: at each quarter of a pay day and when something important happens (a fight with a rival, an attack on your heart or a room, a breach, a room lost, the first creature of a new kind). Reply with a short batch of orders that your creatures and imps then carry out over the next minute or so. You have a memory: YOUR PLAN and YOUR NOTES are yours to rewrite in every reply so that you keep one coherent strategy across the whole game; RECENT DECISIONS shows what you did and what became of each order.

Coordinates: the map is a grid of slabs (the `map` rows show two characters per slab: a kind letter and the owner digit, '..' = never seen). Verbs that build, dig or sell take a slab rectangle [x0, y0, x1, y1]. Verbs that touch a spot (place_trap, place_door, drop, cast_power) take a subtile position [x, y]; a slab (sx, sy) has its centre at subtile (3*sx+1, 3*sy+1). Creature and room positions in the state are subtiles.

Verbs: build_room(kind, slab_rect) on claimed floor you own (PRETTY_PATH in the legend; DIRT is diggable earth, PATH is unclaimed floor; ROOM_COSTS in the state is gold per slab, so cost = area x that, before you send it) and mark_dig(slab_rect) to have imps dig it; sell(slab_rect) for what you own there; place_trap(kind, pos) and place_door(kind, pos) from your stock; cast_power(power, thing_id | pos | nothing, overcharge_turns 0-32; every 4 held turns is one charge level, and a higher level costs more gold -- see POWER_COSTS); slap(thing_id); pick_up(thing_id) then drop(pos); move_creature(thing_id, pos) sends one of your creatures to a subtile (it abandons its work, walks there at creature speed, and holds there) and release_creature(thing_id) hands it back to its normal behaviour, so always release creatures you ordered once they have done their job. The game hands an ordered creature back on its own after hold_turns (default a quarter of a pay day), before pay day, when it is owed pay or getting hungry, and refuses such orders up front (PAYDAY_TOO_CLOSE, OWED_PAY, HUNGRY); SEAT/auto_released in the state tells you which creatures came back. The cost of every order is checked when it starts, so do not queue more than you can pay for -- if unsure, check_orders validates a batch (the same checks a real submit would run, including cost) without spending it. Orders you send now run one after another; earlier orders still running delay them. If an order goes stale it is dropped and reported under LAST ORDERS.

Research is done by your creatures; RESEARCH in the state names what is being worked on right now, its progress, and what is queued behind it, and UNLOCKED lists what has already finished: build only rooms, traps and doors listed there. Creatures: the state gives your army by kind and level, and the first time you own a kind you are shown its profile (jobs, how it fights, pay, hunger, abilities): use it to decide who fights, who works and what to train. Each of your creatures keeps the same name (e.g. "Orc #3") for its whole life, shown with its level, health and current activity (in plain words, not the engine's internal code) and, if relevant, why it might be misbehaving (angry, and why: not_paid, hungry, no_lair, other; or fleeing a fight). Orders that take a creature accept `name` in place of `thing_id` -- either works, and the id stays listed too if you would rather use that. set_tendency(kind: imprison|flee, enabled) sets whether captured enemies are imprisoned instead of killed and whether hurt creatures flee. set_alliance(ally_player, enabled) declares (or withdraws) an alliance with another keeper -- one-way, like the human alliance button; it only actually shares vision/stops friendly fire once the other side declares back too, which ALLIANCE in the state shows (declared vs mutual).

Play to grow a strong dungeon: claim and dig, build treasure room, lair, hatchery, training room and library in that spirit, keep creatures fed and paid, defend the heart, and be ready for events. Answer by calling submit_orders once, with at most ~8 orders. An empty list is a valid answer when waiting is best. You may call look first to see a window of the map, or check_orders first to validate a batch without spending it. If you are clearly behind on thinking time, set_game_speed can slow the whole game down for everyone watching, not just you -- use it sparingly. get_log_tail shows the game's own recent log if something is confusing and you want to see what actually happened."""

# Shared between prompt.TOOLS (submit_orders, this file's Anthropic tool-use loop) and mcp_server.py's own
# submit_orders/check_orders tool schemas (MCP's inputSchema, not Anthropic's input_schema) -- one place names every
# order field, so the two surfaces cannot drift apart on what an order may contain.
ORDER_ITEM_PROPERTIES = {
    "verb": {"type": "string", "enum": ["build_room", "mark_dig", "sell", "place_trap", "place_door", "cast_power",
                                          "slap", "pick_up", "drop", "move_creature", "release_creature",
                                          "set_tendency", "set_alliance", "cancel"]},
    "kind": {"type": "string", "description": "room, trap or door code name (TREASURE, BOULDER, WOOD...), or imprison/flee for set_tendency"},
    "power": {"type": "string", "description": "power code name for cast_power (POWER_LIGHTNING...)"},
    "slab_rect": {"type": "array", "items": {"type": "integer"}, "minItems": 4, "maxItems": 4},
    "pos": {"type": "array", "items": {"type": "integer"}, "minItems": 2, "maxItems": 2},
    "thing_id": {"type": "integer"},
    "overcharge_turns": {"type": "integer", "minimum": 0, "maximum": 32},
    "enabled": {"type": "boolean", "description": "set_tendency / set_alliance: turn it on or off"},
    "hold_turns": {"type": "integer", "minimum": 0, "description": "move_creature: release automatically after this many turns (0 = default)"},
    "ally_player": {"type": "integer", "description": "set_alliance: the other keeper's player number"},
    "name": {"type": "string", "description": "a creature's assigned name (from the CREATURES list, e.g. 'Orc #3'), in place of thing_id"},
}
ORDER_ITEM_REQUIRED = ["verb"]

TOOLS = [
    {
        "name": "submit_orders",
        "description": "Send a batch of orders for your keeper. They run in order.",
        "input_schema": {
            "type": "object",
            "properties": {
                "reasoning": {"type": "string", "description": "One or two sentences on why these orders."},
                "plan": {"type": "string", "description": "Your strategic plan, rewritten in full when it changes (omit to keep the current one). Goals, next steps, what you are waiting for."},
                "notes": {"type": "string", "description": "Facts worth remembering (opponent behaviour, map knowledge, lessons), rewritten in full and kept short (omit to keep)."},
                "orders": {
                    "type": "array",
                    "items": {"type": "object", "properties": ORDER_ITEM_PROPERTIES, "required": ORDER_ITEM_REQUIRED},
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

ORDER_FIELDS = ("verb", "kind", "power", "slab_rect", "pos", "thing_id", "overcharge_turns", "hold_turns", "enabled", "ally_player")


def order_to_request(order, seat, view_turn, max_age_turns, memory=None):
    """A model order -> the submit_action request (queued, expiring). `name` (a creature's assigned name) is resolved to
    thing_id via `memory` when the order gives a name and no explicit thing_id; an unresolvable name is left out, so the
    real submit answers with its own MISSING_THING rather than this silently sending nothing."""
    req = {"action": "submit_action", "player": seat, "queue": True, "view_turn": view_turn, "max_age_turns": max_age_turns}
    for k in ORDER_FIELDS:
        if k in order and order[k] is not None:
            req[k] = order[k]
    if ("thing_id" not in req) and order.get("name") and (memory is not None):
        resolved = memory.id_for(order["name"])
        if resolved is not None:
            req["thing_id"] = resolved
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


def _prettify_state(code):
    """The engine's own internal state code (e.g. "CreatureCombatFlee", "CreatureCannotFindWork") as plain words, with
    no hand-maintained glossary to fall out of date: strip a leading "Creature", then split the rest at capital
    letters. Unrecognised codes still come out readable, just not necessarily idiomatic."""
    s = code[len("Creature"):] if code.startswith("Creature") and len(code) > len("Creature") else code
    # An all-caps run (tried first, so e.g. "IDLE" stays one word) else a capital-led word else a lowercase run.
    words = re.findall(r"[A-Z]+(?![a-z])|[A-Z][a-z0-9]*|[a-z0-9]+", s)
    return " ".join(words) if words else code


def _fmt_creatures(lst, memory=None, limit=40):
    out = []
    for c in lst[:limit]:
        name = memory.name_for(c["id"], c["kind"]) if memory is not None else "%s #%d" % (c["kind"].title(), c["id"])
        tags = []
        if c.get("digger"):
            tags.append("imp")
        if c.get("angry"):
            tags.append("angry: %s" % c.get("angry_reason", "?"))
        if c.get("fleeing"):
            tags.append("fleeing")
        tagstr = " [" + ", ".join(tags) + "]" if tags else ""
        out.append("  %s (id %d, %s L%d, hp%d): %s%s at (%d,%d)" % (
            name, c["id"], c["kind"], c["level"], c["health"], _prettify_state(c["state"]), tagstr, c["pos"][0], c["pos"][1]))
    if len(lst) > limit:
        out.append("  ... and %d more" % (len(lst) - limit))
    return out


def render_army(own):
    summ = own.get("creature_summary", {})
    if not summ:
        return "ARMY: none"
    parts = []
    for kind, e in sorted(summ.items()):
        parts.append("%s x%d (max level %d, avg %.1f, avg hp %d)" % (kind, e["count"], e["max_level"], e["avg_level_x10"] / 10.0, e["avg_health"]))
    return "ARMY: " + "; ".join(parts)


def render_profile(kind, p):
    ab = ", ".join("%s (level %d)" % (a["name"], a["from_level"]) for a in p.get("abilities", [])) or "none"
    return ("  %s: %s; works as %s (secondary %s); health %d, strength %d, armour %d, defence %d, speed %d%s, pay %d, hunger rate %d, lair size %d; abilities: %s" % (
        kind, p.get("combat", "?"), "/".join(p.get("primary_jobs", [])) or "nothing in particular", "/".join(p.get("secondary_jobs", [])) or "none",
        p["health"], p["strength"], p["armour"], p["defence"], p["speed"], " (flies)" if p.get("flying") else "", p["pay"], p["hunger_rate"], p["lair_size"], ab))


def render_for_agent(state, memory, reasons=None, first=False):
    """Full text for a decision: memory (plan/notes/recent decisions) plus the rendered state, with a profile shown once per
    creature kind the seat newly owns and a persistent name per creature. Shared by anthropic_policy.py and
    mcp_session.py so the two entry points (a direct-API policy loop, and MCP tools driven by whichever assistant is
    connected) show the model the same thing."""
    info = state.view["own"].get("creature_info", {})
    fresh = memory.new_kinds(sorted(info)) if memory is not None else []
    text = render_state(state, first=first, reasons=reasons, fresh_kinds=fresh, memory=memory)
    prefix = (memory.render() + "\n\n") if memory is not None else ""
    return prefix + text


def render_state(state, first=False, reasons=None, fresh_kinds=None, memory=None):
    """Compact text of the current view. `first` adds the whole known map; `reasons` is why the decision is due;
    `fresh_kinds` are creature kinds to show a profile for (first time the agent owns them); `memory` (if given) names
    each creature persistently instead of a throwaway kind+id label."""
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
    if reasons:
        lines.insert(0, "DECISION DUE because: " + ", ".join(reasons))
    ev = own.get("events", [])
    if state.last_diff is not None:
        new_ev = state.last_diff.get("own", {}).get("events", {}).get("added", [])
        lines.append("NEW EVENTS: " + ("; ".join("%s at (%d,%d) target %s" % (e["kind"], e["pos"][0], e["pos"][1], e["target"]) for e in new_ev) or "none"))
    elif ev:
        lines.append("EVENTS: " + "; ".join("%s at (%d,%d)" % (e["kind"], e["pos"][0], e["pos"][1]) for e in ev))
    lines.append("CREATURES (%d):" % len(own["creatures"]))
    lines += _fmt_creatures(own["creatures"], memory=memory)
    lines.append("ROOMS: " + ("; ".join("#%d %s %d slabs at (%d,%d)" % (r["id"], r["kind"], r["slabs"], r["pos"][0], r["pos"][1]) for r in own["rooms"]) or "none"))
    stock = own["stock"]
    lines.append("STOCK: traps %s, doors %s" % (stock["traps"] or "none", stock["doors"] or "none"))
    costs = own.get("power_costs", {})
    lines.append("POWERS: " + (", ".join("%s (L0 %d, L2 %d)" % (p, costs[p][0], costs[p][2]) if p in costs else p for p in own["powers"]) or "none"))
    lines.append("DIG MARKS: %d of %d" % (len(own["dig_marks"]), own["dig_marks_limit"]))
    un = own.get("unlocked")
    if un:
        lines.append("UNLOCKED: rooms %s | traps %s | doors %s" % (", ".join(un["rooms"]) or "none", ", ".join(un["traps"]) or "none", ", ".join(un["doors"]) or "none"))
    rs = own.get("research")
    if rs:
        cur = rs.get("current")
        cur_text = "%s %s (%d%%)" % (cur["category"], cur["name"], cur["progress_pct"]) if cur else "nothing (fully researched, or no library worker)"
        lines.append("RESEARCH: working on %s | queue: %s" % (cur_text, ", ".join(rs.get("queue", [])) or "empty"))
    td = own.get("tendencies")
    if td:
        lines.append("TENDENCIES: imprison captured enemies %s, hurt creatures flee %s" % ("on" if td["imprison"] else "off", "on" if td["flee"] else "off"))
    lines.append(render_army(own))
    if fresh_kinds:
        lines.append("NEW CREATURE KIND(S), first time you own them:")
        for k in fresh_kinds:
            if k in own.get("creature_info", {}):
                lines.append(render_profile(k, own["creature_info"][k]))
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
