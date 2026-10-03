"""Turns the agent's view into text for a language model, and defines the tool the model answers with."""
import re

SYSTEM_PROMPT = """You are playing a Dungeon Keeper keeper (a dungeon builder) in real time through an API. The game keeps running while you think. You are called when a decision is due: at each quarter of a pay day and when something important happens (a fight with a rival, an attack on your heart or a room, a breach, a room lost, the first creature of a new kind). Reply with a short batch of orders that your creatures and imps then carry out over the next minute or so. You have a memory: YOUR PLAN and YOUR NOTES are yours to rewrite in every reply so that you keep one coherent strategy across the whole game; RECENT DECISIONS shows what you did and what became of each order. EXPERIENCE is what you learned in earlier games, in your own words: the playbook (any level), this campaign's rules, this level, and each opponent (PLAYERS names them) -- carry what applies into your plan. When a game ends you are asked for a debrief: rewrite those lessons with what this game taught you. Your memory is kept by the game and saved with it: if someone loads a saved game (the decision's reason is reloaded), your plan, notes and decisions are as they were when that save was made, and whatever you did after it never happened.

Coordinates: the map is a grid of slabs (the `map` rows show two characters per slab: a kind letter and the owner digit, '..' = never seen). Every gold, gem and impenetrable-rock slab on the map is shown from the start of the level, so '..' is never gold or rock: it is diggable earth, open ground or somebody's hidden rooms -- when no gold is left on the map, digging into '..' will not find more. Verbs that build, dig or sell take a slab rectangle [x0, y0, x1, y1]. Verbs that touch a spot (place_trap, place_door, drop, cast_power) take a subtile position [x, y]; a slab (sx, sy) has its centre at subtile (3*sx+1, 3*sy+1). Creature and room positions in the state are subtiles.

Verbs: build_room(kind, slab_rect) on claimed floor you own (PRETTY_PATH in the legend; DIRT is diggable earth, PATH is unclaimed floor; ROOM_COSTS in the state is gold per slab, so cost = area x that, before you send it) -- a room the rectangle cannot take anywhere is refused CANNOT_BUILD_HERE, and one it can only partly take is built where it can, the reply naming the skipped slabs (PARTLY_UNBUILDABLE); BRIDGE is the exception: it goes on water or lava, as a straight line one slab wide (BRIDGE_NOT_A_LINE otherwise) touching your land at one end -- your territory, and so your imps' claiming, only extends across water or lava over a bridge, and nothing walks over lava without one; a wider crossing is several lines and mark_dig(slab_rect) to have imps dig it (imps only reach a mark that touches floor they can walk to: a corridor must start on the slab next to your claimed floor, and your own walls, e.g. a room's fortified border, count as part of the dig, not as its start; a mark_dig that no imp could reach comes back with warning UNREACHABLE and its slabs, and DIG MARKS lists any such marks until you fix them); sell(slab_rect) for what you own there; place_trap(kind, pos) and place_door(kind, pos) from your stock (STOCK), on your own claimed floor only (CANNOT_PLACE_HERE otherwise): one trap per slab, and a door needs a one-slab-wide corridor with a wall on each side. Traps are triggered by enemies, but BOULDER and TNT hit every creature in their way, yours included (a boulder rolls at the enemy it sees and crushes whatever is in its path), and the cloud POISON_GAS releases hurts anyone in it -- keep those off the corridors your own creatures use. LAVA turns its slab to lava under the enemy that triggers it: nothing crosses until they claim up to it and bridge it, so a GUARD_POST beside it has your guards fighting them while they try. Doors stop enemies (they must break them down) while your own creatures pass through (a locked door stops them too), and your own door beside a room counts toward its efficiency like a wall; cast_power(power, thing_id | pos | nothing, overcharge_turns 0-32; every 4 held turns is one charge level, and a higher level costs more gold -- see POWER_COSTS); slap(thing_id) -- a creature, or one of your own armed traps (TRAPS shows which are slappable): a slapped TNT goes off where it stands, and a slapped BOULDER rolls off the way you say (direction: N/NE/E/SE/S/SW/W/NW; MISSING_DIRECTION otherwise), crushing whatever is in its path, yours included -- so put a boulder at the end of a long straight corridor an enemy must come down, pointed along it, and slap it when they are in the corridor (it also fires by itself at an enemy it sees); put TNT where enemies bunch up (a doorway, a chokepoint) away from your own creatures and slap it when they gather; a trap must be armed first (your imps bring it a crate from the workshop). unmark_dig(slab_rect) takes the dig marks off whatever is marked in it (NOTHING_MARKED otherwise). set_door_lock(pos, enabled): a locked door stops your own creatures too (keep imps out of a trapped corridor, or creatures in a room), an unlocked one only enemies. power_off(power) ends a lasting power early: POWER_CALL_TO_ARMS (your creatures go back to work), POWER_SIGHT, POWER_OBEY (Must Obey costs gold for as long as it runs) -- NOT_ACTIVE if it is not running. use_special(thing_id) uses a dungeon special box on your own land (SPECIAL BOX; CANNOT_REACH off it): a resurrect box needs kind + level of one of YOUR DEAD, a transfer box target_thing (the creature to take on to the next level). Loose gold (LOOSE GOLD) can be carried home with pick_up_and_drop(thing_ids: [...], to_room: <treasure room>), and gold dropped on one of your creatures pays it. send_message(message): at most 63 characters, one per 100 turns, and never a game command -- use it to coordinate with allied keepers (and the humans among them): short, concrete, e.g. "Attacking the hero fort from the east at turn 20000". CHAT shows what the players said; pick_up_and_drop(thing_id, pos) lifts one of your creatures with the hand and drops it at a subtile in one order -- the fastest way to move a creature, e.g. onto a fight at your heart (your hand must be empty; the spot must be one the hand may drop on: your own claimed floor or rooms, else CANNOT_DROP_HERE; if the pick does not take, nothing is dropped and the order reports PICK_UP_FAILED); pick_up(thing_id) and drop(pos) also exist as separate orders, but a drop is checked against the hand when you send it, so it cannot go in the same batch as its pick_up; move_creature(thing_id, pos) sends one of your creatures to a subtile (it abandons its work, walks there at creature speed, and holds there) and release_creature(thing_id) hands it back to its normal behaviour, so always release creatures you ordered once they have done their job. A held creature does not fight back on its own, so the game hands it back the moment an enemy attacks it (auto-released: attacked); still, release creatures you sent into danger once enemies are near rather than leaving them held. The game also hands an ordered creature back on its own after hold_turns (default a quarter of a pay day), before pay day, when it is owed pay or getting hungry, and refuses such orders up front (PAYDAY_TOO_CLOSE, OWED_PAY, HUNGRY); SEAT/auto_released in the state tells you which creatures came back. The cost of every order is checked when it starts, so do not queue more than you can pay for -- if unsure, check_orders validates a batch (the same checks a real submit would run, including cost) without spending it. Orders you send now run one after another; earlier orders still running delay them. If an order goes stale it is dropped and reported under LAST ORDERS.

Research is done by your creatures; RESEARCH in the state names what is being worked on right now, its progress, and what is queued behind it, and UNLOCKED lists what has already finished: build only rooms, traps and doors listed there. Creatures: the state gives your army by kind and level, and the first time you own a kind you are shown its profile (jobs, how it fights, pay, hunger, abilities): use it to decide who fights, who works and what to train. Each of your creatures keeps the same name (e.g. "Orc #3") for its whole life, shown with its level, health and current activity (in plain words, not the engine's internal code) and, if relevant, why it might be misbehaving (angry, and why: not_paid, hungry, no_lair, other; or fleeing a fight). Orders that take a creature accept `name` in place of `thing_id` -- either works, and the id stays listed too if you would rather use that. Imps (and the heroes' tunnellers) are workers: they dig, claim, mine gold and carry bodies, prisoners and crates, and barely fight -- judge a fight by ARMY's fighters and fighter score (the game's own worth of each creature, scaled by the health it has left), and keep imps out of battles. set_tendency(kind: imprison|flee, enabled): imprison on makes enemies your creatures knock out get carried to your prison instead of killed -- prisoners are a resource -- and it can only be switched on while you own a prison (NO_PRISON otherwise; without a prison your creatures simply kill); a knocked-out creature that is not carried off eventually wakes and goes back to what it was doing. flee on makes badly hurt creatures leave a fight to heal, which keeps them alive and levelled but can lose a fight if too many leave at once; off makes them fight to the death, sometimes right when defending the heart, usually not. set_alliance(ally_player, enabled) declares (or withdraws) an alliance with another keeper -- one-way, like the human alliance button; it only actually shares vision/stops friendly fire once the other side declares back too, which ALLIANCE in the state shows (declared vs mutual).

OBJECTIVE in the state is the level's win condition as the level states it: plan around it. Events carry the text the event box shows; information events are the level talking to you, so read them. When the level is won or lost the decision says so (victory / defeat) and no further orders are taken. Room efficiency (0-100%, ROOMS) scales what a room produces and how much it holds, and its health: each edge of a room scores best against more of the same room, your own reinforced wall or your own door, half as well against natural earth or rock, and nothing when it opens onto a corridor or another room -- so build compact, roughly square rooms, walled in, with doors at the entrances; 'open sides' lists the edges costing you efficiency ((x,y)N/E/S/W: that slab's side). Prisoners (with imprison on, knocked-out enemies are carried to your prison) are a resource: keep them alive to withhold them from their keeper (drop chickens from your hatchery in the prison, or cast POWER_HEAL_CREATURE on them); leave them unfed and a humanoid one that starves rises for you (on_death says as what); or carry them to the torture chamber, where once broken (roughly the kind's break time on the rack, longer in a less efficient chamber) each one may reveal its keeper's map (interrogated, again and again) or join you -- heal them now and then, since the rack hurts them; one that dies on the rack rises for you too. The hand carries prisoners and chickens like your own creatures: pick_up_and_drop(thing_ids: [...], to_room: <room id>) moves up to the hand's limit in one order. A prisoner dropped anywhere but your prison or torture chamber goes free, so that is refused unless you say release: true; a room with no space left turns prisoners away free too (ROOM_FULL), and each torture victim needs a free device. Graveyard: imps carry bodies there to rot, and every so many rotted bodies (GRAVEYARD shows the count) raise a vampire for you, so fights near your dungeon feed it. Temple: creatures in it pray, which calms their anger and cures bad spells on them (disease, chicken...); a creature dropped into the temple pool is sacrificed (prisoners too), and when the offerings match a recipe (listed once you own a temple, exact; get_recipes lists them again) it fires -- a new creature, a boon or a curse on all your creatures, or a special effect. A pool drop must say sacrifice: true (else WOULD_SACRIFICE); to_room: <temple id> without it drops the creature beside the pool to pray. Play to grow a strong dungeon: claim and dig, build treasure room, lair, hatchery, training room and library in that spirit, keep creatures fed and paid, defend the heart, and be ready for events. Answer by calling submit_orders once, with at most ~8 orders. An empty list is a valid answer when waiting is best. You may call look first to see a window of the map, or check_orders first to validate a batch without spending it. If you are clearly behind on thinking time, set_game_speed can slow the whole game down for everyone watching, not just you -- use it sparingly. get_log_tail shows the game's own recent log if something is confusing and you want to see what actually happened."""

# Shared between prompt.TOOLS (submit_orders, this file's Anthropic tool-use loop) and mcp_server.py's own
# submit_orders/check_orders tool schemas (MCP's inputSchema, not Anthropic's input_schema) -- one place names every
# order field, so the two surfaces cannot drift apart on what an order may contain.
ORDER_ITEM_PROPERTIES = {
    "verb": {"type": "string", "enum": ["build_room", "mark_dig", "sell", "place_trap", "place_door", "cast_power",
                                          "slap", "pick_up", "drop", "pick_up_and_drop", "move_creature", "release_creature",
                                          "set_tendency", "set_alliance", "cancel", "unmark_dig", "set_door_lock", "power_off",
                                          "use_special", "send_message"]},
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
    "thing_ids": {"type": "array", "items": {"type": "integer"}, "minItems": 1, "maxItems": 16,
                  "description": "pick_up_and_drop: several things (creatures, prisoners, chickens) carried in one trip, in place of thing_id"},
    "to_room": {"type": "integer", "description": "pick_up_and_drop / drop: one of your rooms (its id), in place of pos"},
    "release": {"type": "boolean", "description": "hand drops: yes, drop this prisoner outside your prison/torture chamber (it goes free)"},
    "sacrifice": {"type": "boolean", "description": "hand drops: yes, drop this creature into the temple pool (it is sacrificed)"},
    "direction": {"type": "string", "enum": ["N", "NE", "E", "SE", "S", "SW", "W", "NW"],
                  "description": "slap on a trap that shoots (a boulder): the way it goes (N is up the map, y decreasing)"},
    "level": {"type": "integer", "minimum": 1, "description": "use_special on a resurrect box: the dead creature's level (with kind)"},
    "target_thing": {"type": "integer", "description": "use_special on a transfer box: the creature to transfer"},
    "message": {"type": "string", "maxLength": 63, "description": "send_message: a short chat message to every player"},
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

ORDER_FIELDS = ("verb", "kind", "power", "slab_rect", "pos", "thing_id", "overcharge_turns", "hold_turns", "enabled", "ally_player",
                "thing_ids", "to_room", "release", "sacrifice", "direction", "level", "target_thing", "message")


# record_debrief's fields, shared by the MCP tool (mcp_server.py) and the direct-API policy's debrief call.
DEBRIEF_PROPERTIES = {
    "summary": {"type": "string", "description": "what happened in this game and why, in a few sentences (max 600 chars)"},
    "playbook": {"type": "string", "description": "your rewritten lessons for any level (max 3000 chars; omit to keep them)"},
    "campaign_lessons": {"type": "string", "description": "your rewritten lessons for this campaign's rules and creatures (max 1500; omit to keep)"},
    "level_lessons": {"type": "string", "description": "your rewritten lessons for this level: its map, objective, enemies (max 1000; omit to keep)"},
    "opponent_lessons": {"type": "object", "additionalProperties": {"type": "string"},
                         "description": "{opponent identity: rewritten lessons about how that opponent plays} (max 800 each); identities as the debrief lists them"},
}


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
        name = memory.name_for(c["id"], c["kind"], c.get("born")) if memory is not None else "%s #%d" % (c["kind"].title(), c["id"])
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
    """ARMY (the fighters, with the fighter score) and WORKERS (diggers: imps and the like) on separate lines, so a
    workforce is never mistaken for an army."""
    summ = own.get("creature_summary", {})
    force = own.get("force") or {}
    fight, work = [], []
    for kind, e in sorted(summ.items()):
        part = "%s x%d (max level %d, avg %.1f, avg hp %d)" % (kind, e["count"], e["max_level"], e["avg_level_x10"] / 10.0, e["avg_health"])
        (work if e.get("digger") else fight).append(part)
    head = "ARMY"
    if force:
        head = "ARMY (fighters %d, fighter score %d)" % (force.get("fighters", 0), force.get("fighter_score", 0))
    lines = [head + ": " + ("; ".join(fight) or "none")]
    if work:
        lines.append("WORKERS: " + "; ".join(work) + " -- dig, claim, mine and carry; not fighters")
    return "\n".join(lines)


def render_profile(kind, p):
    ab = ", ".join("%s (level %d)" % (a["name"], a["from_level"]) for a in p.get("abilities", [])) or "none"
    return ("  %s: %s; works as %s (secondary %s); health %d, strength %d, armour %d, defence %d, speed %d%s, pay %d, hunger rate %d, lair size %d; abilities: %s" % (
        kind, p.get("combat", "?"), "/".join(p.get("primary_jobs", [])) or "nothing in particular", "/".join(p.get("secondary_jobs", [])) or "none",
        p["health"], p["strength"], p["armour"], p["defence"], p["speed"], " (flies)" if p.get("flying") else "", p["pay"], p["hunger_rate"], p["lair_size"], ab))


def render_for_agent(state, memory, reasons=None, first=False, note=None, experience=None):
    """Full text for a decision: memory (plan/notes/recent decisions) plus the rendered state, with a profile shown once per
    creature kind the seat newly owns and a persistent name per creature. Shared by anthropic_policy.py and
    mcp_session.py so the two entry points (a direct-API policy loop, and MCP tools driven by whichever assistant is
    connected) show the model the same thing."""
    info = state.view["own"].get("creature_info", {})
    if memory is not None:
        memory.note_live(state.view["own"].get("creatures", []))
    fresh = memory.new_kinds(sorted(info)) if memory is not None else []
    text = render_state(state, first=first, reasons=reasons, fresh_kinds=fresh, memory=memory)
    prefix = (memory.render() + "\n\n") if memory is not None else ""
    if experience:
        prefix += experience + "\n\n"
    if note:
        prefix = note + "\n\n" + prefix
    return prefix + text


def _fmt_room(r):
    """One room on one line: size and place, then how well it works (efficiency, gold / fill, health, who is in it) and
    which of its edges are open -- what walling in or a door would improve. A perfect room stays short."""
    head = "#%d %s %d slabs at (%d,%d)" % (r["id"], r["kind"], r["slabs"], r["pos"][0], r["pos"][1])
    if "efficiency" not in r:
        return head
    bits = ["efficiency %d%%" % r["efficiency"]]
    if "gold" in r:
        bits.append("gold %d" % r["gold"])
    cap = r.get("capacity")
    if cap:
        bits.append("used %d/%d" % (cap["used"], cap["total"]))
    if r.get("health") is not None and r.get("max_health") and r["health"] < r["max_health"]:
        bits.append("hp %d/%d" % (r["health"], r["max_health"]))
    occ = r.get("occupants") or {}
    if occ:
        bits.append("in it: " + ", ".join("%s %d" % (k, n) for k, n in sorted(occ.items())))
    line = head + ": " + ", ".join(bits)
    osd = r.get("open_sides") or {}
    if osd.get("count"):
        eg = " ".join("(%d,%d)%s" % (x, y, side) for x, y, side in osd.get("slabs", [])[:4])
        line += "; %d open side%s e.g. %s" % (osd["count"], "" if osd["count"] == 1 else "s", eg)
    return line


def render_custody(own):
    """Prisons, torture chambers and the prisoners in them; your own creatures an enemy holds; chickens the hand could
    carry. Only when something applies."""
    c = own.get("custody")
    if not c:
        return []
    td = own.get("tendencies") or {}
    lines = []
    for r in c.get("prisons", []):
        note = "" if td.get("imprison") else " (imprison is OFF: no new prisoners will arrive)"
        lines.append("PRISON room %d: %d/%d held%s" % (r["room"], r["held"], r["capacity"], note))
    for r in c.get("torture", []):
        lines.append("TORTURE room %d: %d/%d, %d device(s) free" % (r["room"], r["held"], r["capacity"], r.get("devices_free", 0)))
    for p in c.get("prisoners", []):
        bits = ["#%d %s L%d owner %d hp %d/%d in %s%s" % (p["id"], p["kind"], p["level"], p["owner"], p["health"], p["max_health"],
                                                          p["where"], (" %d" % p["room"]) if p.get("room") else "")]
        if p.get("hungry"):
            bits.append("hungry")
        t = p.get("torture")
        if t:
            bits.append("%d turns on the rack (breaks from ~%d)" % (t["turns_in"], t["break_time"]))
        if p.get("on_death"):
            bits.append("-> %s for you if it dies there" % p["on_death"])
        lines.append("  PRISONER " + ", ".join(bits))
    for h in c.get("held_by_enemy", []):
        lines.append("  YOURS HELD BY AN ENEMY: #%d %s L%d at (%d,%d)" % (h["id"], h["kind"], h["level"], h["pos"][0], h["pos"][1]))
    for room, f in sorted((c.get("food") or {}).items()):
        lines.append("FOOD: hatchery %s has %d chicken(s) (ids %s)" % (room, f["chickens"], " ".join(str(i) for i in f.get("ids", []))))
    return lines


def render_recipes(view, full):
    """The active sacrifice recipes (present while you own a temple): in full, or as a one-line reminder."""
    recipes = (view.get("rules") or {}).get("sacrifices")
    if recipes is None:
        return "No temple, so no sacrifice recipes to show." if full else ""
    if not full:
        return "RECIPES: %d active (listed in full when your temple appeared; get_recipes shows them again)" % len(recipes)
    return "SACRIFICE RECIPES (exact; drop these creatures into the temple pool, together or one after another): " + "; ".join(recipes)


def render_temple_and_graveyard(state, first):
    own = state.view["own"]
    lines = []
    gy = own.get("graveyard")
    if gy:
        lines.append("GRAVEYARD: %d/%d bodies toward the next vampire; %d body(ies) lying where you can see them (imps carry them in)" % (
            gy["toward_vampire"], gy["per_vampire"], gy.get("bodies_lying", 0)))
    for t in own.get("temples", []):
        pool = ("pool at (%d,%d)" % tuple(t["pool"])) if t.get("pool") else "no pool (too small: a temple needs an inside, 3x3 or more)"
        lines.append("TEMPLE room %d: efficiency %d%%, %s, %d praying" % (t["id"], t["efficiency"], pool, t.get("praying", 0)))
    sac = own.get("sacrifices") or {}
    if sac.get("offered"):
        lines.append("OFFERED SO FAR: " + ", ".join("%s x%d" % (k, n) for k, n in sorted(sac["offered"].items())))
    for o in sac.get("outcomes", [])[-3:]:
        lines.append("SACRIFICE FIRED turn %d: %s" % (o["turn"], o["recipe"]))
    newly = state.last_diff is not None and "sacrifices" in (state.last_diff.get("rules") or {})
    rec = render_recipes(state.view, full=first or newly)
    if rec and own.get("temples"):
        lines.append(rec)
    return lines


def render_placed_and_pickups(view):
    """Your placed traps (which a slap sets off now, and which need a direction) and doors (locked or not), lasting powers
    you can end, special boxes and loose gold for the hand, your dead (once a resurrect box is about), and the chat."""
    own = view["own"]
    lines = []
    traps = own.get("traps") or []
    if traps:
        lines.append("TRAPS: " + "; ".join("#%d %s at (%d,%d)%s" % (
            t["id"], t["kind"], t["pos"][0], t["pos"][1],
            (" [slappable%s]" % (", give a direction" if t.get("aimed") else "")) if t.get("slappable") else " [not armed / spent]")
            for t in traps[:20]))
    doors = own.get("doors") or []
    if doors:
        lines.append("DOORS: " + "; ".join("#%d %s at (%d,%d)%s" % (d["id"], d["kind"], d["pos"][0], d["pos"][1], " LOCKED" if d.get("locked") else "")
                                           for d in doors[:20]))
    ap = own.get("active_powers") or {}
    if ap:
        lines.append("ACTIVE POWERS (power_off ends one): " + ", ".join(
            "%s%s" % (k, (" at (%d,%d)" % tuple(e["pos"])) if isinstance(e, dict) and e.get("pos") else "") for k, e in sorted(ap.items())))
    for b in own.get("specials", []):
        lines.append("SPECIAL BOX #%d %s at (%d,%d)%s" % (b["id"], b["kind"], b["pos"][0], b["pos"][1],
                                                        "" if b.get("reachable") else " (not on your land: your hand cannot reach it)"))
    if own.get("dead_creatures"):
        lines.append("YOUR DEAD (for a resurrect box): " + ", ".join("%s L%d" % (d["kind"], d["level"]) for d in own["dead_creatures"][:20]))
    lg = own.get("loose_gold")
    if lg:
        lines.append("LOOSE GOLD: %d in %d pile(s) outside your treasure rooms, e.g. %s" % (
            lg["total"], lg["count"], "; ".join("#%d %d at (%d,%d)%s" % (g["id"], g["gold"], g["pos"][0], g["pos"][1], "" if g.get("reachable") else " (out of reach)")
                                                for g in lg.get("piles", [])[:6])))
    chat = view.get("chat") or []
    if chat:
        me = view["seat"]["player"]
        lines.append("CHAT (latest last): " + " | ".join("%s: %s" % ("you" if c["player"] == me else "player %d" % c["player"], _oneline(c["text"], 80))
                                                     for c in chat[-6:]))
    return lines


def render_rooms(rooms):
    if not rooms:
        return ["ROOMS: none"]
    return ["ROOMS:"] + ["  " + _fmt_room(r) for r in rooms]


def _oneline(text, limit=400):
    t = " ".join(str(text).split())
    return t if len(t) <= limit else t[:limit - 3] + "..."


def _fmt_event(e):
    """An event marker as the event box would read to a human: its text when it has one, else kind and target."""
    where = "at (%d,%d)" % (e["pos"][0], e["pos"][1])
    if e.get("text"):
        return '%s %s: "%s"' % (e["kind"], where, _oneline(e["text"], 240))
    return "%s %s target %s" % (e["kind"], where, e["target"])


def render_players(view):
    """Everyone in the level, as the player tooltips name them: "PLAYERS: 0 Robin (human); 2 computer (AI: ...); ..."."""
    me = view.get("seat", {}).get("player")
    out = []
    for p in view.get("players") or []:
        if p.get("kind") == "heroes":
            out.append("%d heroes" % p["player"])
            continue
        who = p.get("name") or ("Player %d" % p["player"])
        detail = [p.get("kind", "?")]
        if p.get("ai_type"):
            detail.append("AI: " + p["ai_type"].rstrip("."))
        if p.get("alive") is False:
            detail.append("no heart")
        if p["player"] == me:
            detail.append("you")
        out.append("%d %s (%s)" % (p["player"], who, ", ".join(detail)))
    return ("PLAYERS: " + "; ".join(out)) if out else ""


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
    lv = v.get("level") or {}
    if first and lv.get("name"):
        lines.append("LEVEL %s: %s%s" % (lv.get("number", "?"), lv["name"], (" -- " + _oneline(lv["description"])) if lv.get("description") else ""))
    players_line = render_players(v)
    changed_players = state.last_diff is not None and "players" in state.last_diff
    if players_line and (first or changed_players):
        lines.append(players_line)
    if own.get("objective"):
        lines.append("OBJECTIVE: " + _oneline(own["objective"]))
    hist = own.get("objective_history") or []
    if first and len(hist) > 1:
        lines.append("EARLIER OBJECTIVES: " + " | ".join("turn %d: %s" % (o["turn"], _oneline(o["text"])) for o in hist[:-1]))
    ev = own.get("events", [])
    if state.last_diff is not None:
        new_ev = state.last_diff.get("own", {}).get("events", {}).get("added", [])
        lines.append("NEW EVENTS: " + ("; ".join(_fmt_event(e) for e in new_ev) or "none"))
    elif ev:
        lines.append("EVENTS: " + "; ".join(_fmt_event(e) for e in ev))
    lines.append("CREATURES (%d):" % len(own["creatures"]))
    lines += _fmt_creatures(own["creatures"], memory=memory)
    lines += render_rooms(own["rooms"])
    lines += render_custody(own)
    lines += render_temple_and_graveyard(state, first)
    stock = own["stock"]
    lines.append("STOCK: traps %s, doors %s" % (stock["traps"] or "none", stock["doors"] or "none"))
    lines += render_placed_and_pickups(v)
    costs = own.get("power_costs", {})
    lines.append("POWERS: " + (", ".join("%s (L0 %d, L2 %d)" % (p, costs[p][0], costs[p][2]) if p in costs else p for p in own["powers"]) or "none"))
    dig = "DIG MARKS: %d of %d" % (len(own["dig_marks"]), own["dig_marks_limit"])
    unreach = own.get("unreachable_dig_marks") or []
    if unreach:
        n = own.get("unreachable_dig_marks_count", len(unreach))
        dig += " | UNREACHABLE %d (no imp can get to them; connect them to walkable floor): %s%s" % (
            n, " ".join("(%d,%d)" % (x, y) for x, y in unreach[:12]), " ..." if n > 12 else "")
    lines.append(dig)
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
        fbo = vis.get("force_by_owner") or {}
        lines.append("VISIBLE OTHER CREATURES" + ((" (" + "; ".join(
            "owner %s: fighters %d, fighter score %d, workers %d" % (o, f.get("fighters", 0), f.get("fighter_score", 0), f.get("workers", 0))
            for o, f in sorted(fbo.items())) + ")") if fbo else "") + ":")
        for c in vis["creatures"][:30]:
            lines.append("  #%d %s%s owner %d%s hp%d%s at (%d,%d)" % (
                c["id"], c["kind"], (" L%d" % c["level"]) if c.get("level") else "", c["owner"], " (ally)" if c.get("ally") else "",
                c["health"], " [digger]" if c.get("digger") else "", c["pos"][0], c["pos"][1]))
    news = _map_news(state)
    if news:
        lines.append(news)
    if first:
        m = v["map"]
        lines.append("MAP %dx%d slabs (rows y=0..; 2 chars per slab: kind, owner; '..' unknown, never gold or rock); legend: %s" % (
            m["width"], m["height"], ", ".join("%s=%s" % (e["c"], e["kind"]) for e in m.get("legend", []))))
        lines += m["rows"]
    return "\n".join(lines)
