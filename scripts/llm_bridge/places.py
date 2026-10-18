"""Named places the bridge keeps for the agent (docs/plans/llm-bridge-memory.md, Phase F): where things are, by name, so
orders and lessons say "heart" or "treasury_P2" instead of coordinates the model had to read off the map.

Filled from the view each decision (`refresh`), without the model:
- the seat's own rooms by kind: `heart`, `portal`, `treasury`, `lair`, `hatchery`, `library`, ... (a second one of a kind
  is `treasury_2`; a room keeps its name while it stands);
- each hero gate seen: `hero_gate_<n>`;
- each enemy's heart once seen (`heart_P<player>`, kept after the fog returns, with the turn last seen) and its rooms
  while they stand (`treasury_P<player>`, ...).
The agent names its own (`name`: a subtile, or a slab rectangle); those, and the rally points, come before the bridge's.

Where a position goes, a name does: an order's `pos` (the place's subtile), `slab_rect` (a place named with a rectangle),
`to_room` (one of the seat's rooms), a hand rule's `to`. An unknown name is refused before anything is sent. At the end of
a game the places the agent named, the enemy hearts and the hero gates become lessons of the level (provisional, for the
debrief to keep).

Coordinates: `pos` is subtiles, `rect` slabs (inclusive), as everywhere in the API (docs/Architecture/agent-api.md).
Kept in the agent's memory (memory.places = {"named": {...}, "auto": {...}}, the agent's and the bridge's apart, so a
name the agent gives never loses the bridge's entry of that name), which the game saves.
"""
ROOM_NAMES = {"DUNGEON_HEART": "heart", "ENTRANCE": "portal", "TREASURE": "treasury", "LAIR": "lair", "GARDEN": "hatchery",
              "RESEARCH": "library", "TRAINING": "training", "WORKSHOP": "workshop", "BARRACKS": "barracks", "PRISON": "prison",
              "TORTURE": "torture", "TEMPLE": "temple", "GRAVEYARD": "graveyard", "SCAVENGER": "scavenger",
              "GUARD_POST": "guard_post"}
NAME_CAP = 24
RENDER_MAX = 30


class PlaceError(ValueError):
    """An order named a place that does not exist (or cannot serve there). str() is the refusal's error text."""


def _slab_of(pos):
    return [pos[0] // 3, pos[1] // 3]


def _rect_center(rect):
    x0, y0, x1, y1 = rect
    return [3 * ((min(x0, x1) + max(x0, x1)) // 2) + 1, 3 * ((min(y0, y1) + max(y0, y1)) // 2) + 1]


def clean_name(name):
    return "_".join(str(name or "").strip().split())[:NAME_CAP]


def _reg(mem):
    """(the agent's places, the bridge's), creating them in the memory as needed."""
    p = mem.places if isinstance(getattr(mem, "places", None), dict) else {}
    p.setdefault("named", {})
    p.setdefault("auto", {})
    mem.places = p
    return p["named"], p["auto"]


def refresh(mem, view):
    """Brings the bridge's own entries up to date with the view (the agent's named ones are left alone)."""
    if view is None:
        return
    places = _reg(mem)[1]
    turn = view.get("turn")
    me = (view.get("seat") or {}).get("player")
    # The seat's rooms: one name per room, kept while it stands.
    rooms = sorted((r for r in (view.get("own") or {}).get("rooms") or [] if r.get("kind") in ROOM_NAMES and r.get("pos")),
                   key=lambda r: r["id"])
    have = {e.get("room"): n for n, e in places.items() if e.get("kind") == "own"}
    standing = {r["id"] for r in rooms}
    for n in [n for n, e in places.items() if e.get("kind") == "own" and e.get("room") not in standing]:
        del places[n]
    for r in rooms:
        name = have.get(r["id"])
        if name is None:
            name = _free(places, ROOM_NAMES[r["kind"]])
        places[name] = {"pos": list(r["pos"]), "kind": "own", "room": r["id"], "what": r["kind"], "seen": turn}
    # What the seat can see of the others: hero gates, enemy rooms (hearts kept after they are out of sight).
    vis = view.get("visible") or {}
    for g in vis.get("hero_gates") or []:
        if g.get("pos"):
            places["hero_gate_%s" % g.get("number", g.get("id"))] = {"pos": list(g["pos"]), "kind": "gate", "seen": turn}
    seen_rooms = {}
    for r in vis.get("rooms") or []:
        if r.get("ally") or r.get("owner") in (None, me) or r.get("kind") not in ROOM_NAMES or not r.get("pos"):
            continue
        seen_rooms[r["id"]] = r
    have = {e.get("room"): n for n, e in places.items() if e.get("kind") == "enemy"}
    for n in [n for n, e in places.items() if e.get("kind") == "enemy" and e.get("room") not in seen_rooms
              and e.get("what") != "DUNGEON_HEART"]:
        del places[n]
    for rid, r in sorted(seen_rooms.items()):
        name = have.get(rid) or _free(places, "%s_P%d" % (ROOM_NAMES[r["kind"]], r["owner"]))
        places[name] = {"pos": list(r["pos"]), "kind": "enemy", "room": rid, "what": r["kind"], "owner": r["owner"], "seen": turn}


def _free(places, base):
    if base not in places:
        return base
    n = 2
    while "%s_%d" % (base, n) in places:
        n += 1
    return "%s_%d" % (base, n)


def name_place(mem, name, pos=None, slab_rect=None, turn=None):
    """The agent names a spot: a subtile `pos`, or a slab rectangle `slab_rect` (its centre is its pos). Neither drops
    the agent's place of that name. Returns the entry (or None when dropped); PlaceError for a bad one."""
    name = clean_name(name)
    if not name:
        raise PlaceError("BAD_NAME: a place needs a name")
    named = _reg(mem)[0]
    if pos is None and slab_rect is None:
        if name not in named:
            raise PlaceError("UNKNOWN_PLACE: you named no place %r" % name)
        del named[name]
        return None
    entry = {"kind": "named", "seen": turn}
    if slab_rect is not None:
        if not (isinstance(slab_rect, (list, tuple)) and len(slab_rect) == 4 and all(isinstance(v, int) for v in slab_rect)):
            raise PlaceError("BAD_RECT: slab_rect is [x0, y0, x1, y1] in slabs")
        entry["rect"] = list(slab_rect)
        entry["pos"] = _rect_center(slab_rect)
    else:
        if not (isinstance(pos, (list, tuple)) and len(pos) == 2 and all(isinstance(v, int) for v in pos)):
            raise PlaceError("BAD_POSITION: pos is [x, y] in subtiles")
        entry["pos"] = list(pos)
    named[name] = entry
    return entry


def apply_ops(mem, ops, turn=None):
    """`places` on a decision's answer: {name: [x, y] (subtiles) | [x0, y0, x1, y1] (slabs) | {"pos"} | {"slab_rect"} |
    null (drop it)}. Returns {"named": [...], "dropped": [...], "errors": [...]}."""
    out = {"named": [], "dropped": [], "errors": []}
    if not isinstance(ops, dict):
        if ops:
            out["errors"].append("places is an object {name: [x, y] | [x0, y0, x1, y1] | null}")
        return out
    for name, v in ops.items():
        pos = rect = None
        if isinstance(v, dict):
            pos, rect = v.get("pos"), v.get("slab_rect")
        elif isinstance(v, (list, tuple)):
            pos, rect = (list(v), None) if len(v) == 2 else (None, list(v))
        elif v is not None:
            out["errors"].append("%s: a place is [x, y], [x0, y0, x1, y1] or null" % name)
            continue
        try:
            e = name_place(mem, name, pos=pos, slab_rect=rect, turn=turn)
        except PlaceError as err:
            out["errors"].append("%s: %s" % (name, err))
            continue
        out["named" if e is not None else "dropped"].append(clean_name(name))
    return out


def lookup(mem, name):
    """The place called `name`: the agent's own first, then a rally point, then the bridge's. None if there is none."""
    key = clean_name(name)
    named, auto = _reg(mem)
    if key in named:
        return named[key]
    rally = getattr(mem, "rally", None) or {}
    if key in rally:
        return {"pos": list(rally[key]), "kind": "rally"}
    return auto.get(key)


def resolve_pos(mem, name):
    e = lookup(mem, name)
    if e is None:
        raise PlaceError("UNKNOWN_PLACE: no place %r (PLACES lists them; name_place adds one)" % name)
    return list(e["pos"])


def resolve_rect(mem, name):
    e = lookup(mem, name)
    if e is None:
        raise PlaceError("UNKNOWN_PLACE: no place %r (PLACES lists them; name_place adds one)" % name)
    if e.get("rect"):
        return list(e["rect"])
    x, y = _slab_of(e["pos"])
    if e.get("kind") in ("own", "enemy"):
        raise PlaceError("PLACE_HAS_NO_RECT: %r is a room (its centre is known, not its shape): give slab_rect" % name)
    return [x, y, x, y]


def resolve_room(mem, name):
    e = lookup(mem, name)
    if e is None or e.get("kind") != "own":
        raise PlaceError("NOT_YOUR_ROOM: %r is not one of your rooms (PLACES lists them)" % name)
    return e["room"]


def resolve_order(order, mem):
    """An order with place names in pos / slab_rect / to_room, with them replaced by coordinates (a copy; the order
    itself when it names none). PlaceError when a name does not resolve."""
    names = {k: order[k] for k in ("pos", "slab_rect", "to_room") if isinstance(order.get(k), str)}
    if not names:
        return order
    out = dict(order)
    if "pos" in names:
        out["pos"] = resolve_pos(mem, names["pos"])
    if "slab_rect" in names:
        out["slab_rect"] = resolve_rect(mem, names["slab_rect"])
    if "to_room" in names:
        out["to_room"] = resolve_room(mem, names["to_room"])
    return out


def _where(e):
    if e.get("rect"):
        x0, y0, x1, y1 = e["rect"]
        return "slabs %d-%d,%d-%d" % (min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1))
    return "%d,%d" % tuple(e["pos"])


def render(mem, view, limit=RENDER_MAX):
    """The PLACES lines of a decision: yours first, then rally points, your rooms, gates, the enemy's."""
    named, auto = _reg(mem)
    rally = getattr(mem, "rally", None) or {}
    turn = (view or {}).get("turn")
    order = {"named": 0, "rally": 1, "own": 2, "gate": 3, "enemy": 4}
    rows = list(named.items()) + [(n, {"pos": p, "kind": "rally"}) for n, p in rally.items() if n not in named]
    rows += [(n, e) for n, e in auto.items() if n not in named and n not in rally]   # a name the agent took hides the bridge's
    if not rows:
        return []
    rows.sort(key=lambda ne: (order.get(ne[1].get("kind"), 9), ne[0]))
    parts = []
    for n, e in rows[:limit]:
        seen = e.get("seen")
        stale = e.get("kind") == "enemy" and seen is not None and turn is not None and turn - seen > 0
        parts.append("%s (%s%s)" % (n, _where(e), (", seen t%d" % seen) if stale else ""))
    more = len(rows) - limit
    return ["PLACES (subtiles unless slabs; a name works as pos, slab_rect for a named rectangle, or to_room for your rooms; "
            "name_place adds yours): " + " | ".join(parts) + (" | +%d more" % more if more > 0 else "")]


def lesson_texts(mem):
    """What the places of this game are worth remembering on this level: the ones the agent named, the enemy hearts,
    the hero gates."""
    out = []
    named, auto = _reg(mem)
    rank = {"named": 0, "enemy": 1, "gate": 2}
    for n, e in sorted(list(named.items()) + list(auto.items()), key=lambda ne: (rank.get(ne[1].get("kind"), 9), ne[0])):
        kind = e.get("kind")
        if kind == "named":
            what = _where(e) if e.get("rect") else "subtile (%d,%d) = slab (%d,%d)" % (tuple(e["pos"]) + tuple(_slab_of(e["pos"])))
            out.append("Place %s: %s." % (n, what))
        elif kind == "enemy" and e.get("what") == "DUNGEON_HEART":
            out.append("Player %s's heart is at slab (%d,%d) = subtile (%d,%d)." % (
                (e.get("owner"),) + tuple(_slab_of(e["pos"])) + tuple(e["pos"])))
        elif kind == "gate":
            out.append("Hero gate %s is at slab (%d,%d) = subtile (%d,%d)." % (
                (n[len("hero_gate_"):],) + tuple(_slab_of(e["pos"])) + tuple(e["pos"])))
    return out
