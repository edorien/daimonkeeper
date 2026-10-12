#!/usr/bin/env python3
"""The combat baseline (docs/newFeatures/llm/03 P0): how fast a keeper reacts to fights, from the game's own combat
statistics (kfx_script/api_seat_combatstats.c).

Two sources, the same numbers:
  - the experience store: each game's record keeps the seat's last get_combat_stats (final.combat), written by
    mcp_session.py when the game ends;
  - daimonkeeper.log: COMBATSTAT: lines, written for every keeper not played by the built-in AI (an agent's seat and
    the local human alike), so a human game gives a baseline too.

    python3 scripts/llm_bridge/combat_report.py --store /path/to/experience.sqlite
    python3 scripts/llm_bridge/combat_report.py --log daimonkeeper.log [--log other.log] [--player 0]

Times are game turns; seconds assume the default 20 turns per second (command_line.cpp). The headline numbers:
  window_low / window_critical  turns a creature that died in a fight had between first dropping under 35% / 20%
                                health and dying -- the time a reflex had to save it;
  first_cast_after              turns from a fight's start to the keeper's first power cast in it;
  decision_after / orders_after turns from a fight's start to the agent being told (DECISION_DUE) and to its first
                                orders after that;
  upkeep                        gold Call to Arms and Must Obey took while they ran.
"""
import argparse
import json
import os
import re
import sqlite3
import sys

TURNS_PER_SECOND = 20
_LINE = re.compile(r"COMBATSTAT: (\w+) (.*)$")
_KV = re.compile(r"(\w+)=(\S+)")


def median(values):
    """The lower middle value (as the game computes it), or None for no values."""
    vals = sorted(v for v in values if v is not None)
    if not vals:
        return None
    return vals[(len(vals) - 1) // 2]


def _num(v):
    try:
        return int(v)
    except ValueError:
        return v


def parse_log_lines(lines):
    """COMBATSTAT: lines -> {player: {"deaths": [...], "fights": [...], "casts": [...], "paydays": [...], "upkeep": [...],
    "orders": [...]}} (orders: a decision answered, with its latency).
    A level start in the log (turn going back) is not split out: pass one game's log, or use --player."""
    out = {}
    kinds = {"death": "deaths", "fight": "fights", "cast": "casts", "payday": "paydays", "upkeep": "upkeep",
             "orders": "orders"}
    for line in lines:
        m = _LINE.search(line)
        if not m or m.group(1) not in kinds:
            continue
        rec = {k: _num(v) for k, v in _KV.findall(m.group(2))}
        player = rec.pop("player", None)
        if player is None:
            continue
        out.setdefault(player, {k: [] for k in kinds.values()})[kinds[m.group(1)]].append(rec)
    return out


def headline_from_log(rec):
    """The headline numbers from one player's parsed log records (the same keys as headline_from_stats)."""
    deaths, fights = rec["deaths"], rec["fights"]
    upkeep = {}
    for u in rec["upkeep"]:
        upkeep[u["power"]] = upkeep.get(u["power"], 0) + u["gold"]
    combat_casts = [c for c in rec["casts"] if c.get("in_fight") == 1]
    return {
        "fights": len(fights),
        "combat_deaths": len(deaths),
        "combat_digger_deaths": sum(1 for d in deaths if d.get("digger") == 1),
        "deaths_seen_low": sum(1 for d in deaths if d.get("window_low", -1) >= 0),
        "deaths_healed": sum(1 for d in deaths if d.get("heal_after_low", -1) >= 0),
        "median_window_low": median(d["window_low"] for d in deaths if d.get("window_low", -1) >= 0),
        "median_window_critical": median(d["window_critical"] for d in deaths if d.get("window_critical", -1) >= 0),
        "fights_with_cast": sum(1 for f in fights if f.get("first_cast_after", -1) >= 0),
        "median_first_cast_after": median(f["first_cast_after"] for f in fights if f.get("first_cast_after", -1) >= 0),
        "median_decision_after": median(f["decision_after"] for f in fights if f.get("decision_after", -1) >= 0),
        "median_orders_after": median(f["orders_after"] for f in fights if f.get("orders_after", -1) >= 0),
        "median_decision_latency": median(o["latency"] for o in rec["orders"]),
        "median_decision_latency_combat": median(o["latency"] for o in rec["orders"] if o.get("combat") == 1),
        "casts": len(rec["casts"]),
        "casts_in_fights": len(combat_casts),
        "cast_gold": sum(c.get("gold", 0) for c in rec["casts"]),
        "cta_upkeep_gold": upkeep.get("POWER_CALL_TO_ARMS", 0),
        "obey_upkeep_gold": upkeep.get("POWER_OBEY", 0),
        "paydays": len(rec["paydays"]),
        "paydays_short": sum(1 for p in rec["paydays"] if p.get("gold", 0) < p.get("wages", 0)),
    }


def _m(v):
    """A median from the game: -1 (or absent) means there was nothing to take it over."""
    return None if v is None or v < 0 else v


def headline_from_stats(stats):
    """The headline numbers from get_combat_stats' reply (the game's own medians, over the fights and deaths it holds)."""
    f, d, c, u, p, dec = (stats.get(k, {}) for k in ("fights", "deaths", "casts", "upkeep", "paydays", "decisions"))
    return {
        "fights": f.get("count", 0),
        "combat_deaths": d.get("combat", 0),
        "combat_digger_deaths": d.get("combat_diggers", 0),
        "deaths_seen_low": d.get("seen_low", 0),
        "deaths_healed": d.get("healed", 0),
        "median_window_low": _m(d.get("median_window_low")),
        "median_window_critical": _m(d.get("median_window_critical")),
        "fights_with_cast": f.get("with_cast", 0),
        "median_first_cast_after": _m(f.get("median_first_cast_after")),
        "median_decision_after": _m(f.get("median_decision_after")),
        "median_orders_after": _m(f.get("median_orders_after")),
        "median_decision_latency": _m(dec.get("median_latency")),
        "median_decision_latency_combat": _m(dec.get("median_latency_combat")),
        "casts": c.get("count", 0),
        "casts_in_fights": sum(x.get("casts", 0) for x in f.get("recent", [])),
        "cast_gold": c.get("gold", 0),
        "cta_upkeep_gold": u.get("cta_gold", 0),
        "obey_upkeep_gold": u.get("obey_gold", 0),
        "paydays": p.get("count", 0),
        "paydays_short": p.get("short", 0),
    }


def _turns(v):
    return "-" if v is None else "%d (%.1fs)" % (v, v / TURNS_PER_SECOND)


def render_text(stats):
    """A short plain-text summary of get_combat_stats, for the MCP tool."""
    h = headline_from_stats(stats)
    by_power = ", ".join("%s %d (%d gold)" % (k.replace("POWER_", "").lower(), v["count"], v["gold"])
                         for k, v in stats.get("casts", {}).get("by_power", {}).items()) or "none"
    return "\n".join([
        "Combat statistics since turn %s (now %s):" % (stats.get("since_turn"), stats.get("turn")),
        "  fights %d; your creatures killed in them %d (workers %d)" % (h["fights"], h["combat_deaths"], h["combat_digger_deaths"]),
        "  of those, seen under %s%% health first: %d, healed after that: %d" % (stats.get("low_pct", 35), h["deaths_seen_low"], h["deaths_healed"]),
        "  time they had, under %s%% -> death: %s; under %s%% -> death: %s" % (
            stats.get("low_pct", 35), _turns(h["median_window_low"]), stats.get("critical_pct", 20), _turns(h["median_window_critical"])),
        "  fight start -> first power cast: %s (in %d of %d fights)" % (_turns(h["median_first_cast_after"]), h["fights_with_cast"], h["fights"]),
        "  fight start -> decision due: %s; -> your orders: %s" % (_turns(h["median_decision_after"]), _turns(h["median_orders_after"])),
        "  decision due -> your orders: %s (fight decisions: %s)" % (_turns(h["median_decision_latency"]), _turns(h["median_decision_latency_combat"])),
        "  casts: %s" % by_power,
        "  upkeep: call to arms %d gold, must obey %d gold" % (h["cta_upkeep_gold"], h["obey_upkeep_gold"]),
        "  pay days %d, short of gold at %d" % (h["paydays"], h["paydays_short"]),
    ])


def rows_from_store(path):
    """(label, headline) per game in an experience store whose record has combat statistics."""
    con = sqlite3.connect("file:%s?mode=ro" % path, uri=True)
    con.row_factory = sqlite3.Row
    rows = []
    for r in con.execute("SELECT level_key, level_name, result, end_turn, final FROM games ORDER BY started_at"):
        final = json.loads(r["final"] or "{}")
        if "combat" not in final:
            continue
        label = "%s %s (%s, turn %s)" % (r["level_key"], r["level_name"] or "", r["result"], r["end_turn"])
        rows.append((label, headline_from_stats(final["combat"])))
    con.close()
    return rows


def rows_from_logs(paths, player=None):
    rows = []
    for path in paths:
        with open(path, "rb") as f:
            lines = f.read().decode("utf-8", "replace").splitlines()
        for plyr, rec in sorted(parse_log_lines(lines).items()):
            if player is not None and plyr != player:
                continue
            rows.append(("%s player %d" % (os.path.basename(path), plyr), headline_from_log(rec)))
    return rows


COLUMNS = [
    ("fights", "fights"), ("combat_deaths", "deaths"), ("combat_digger_deaths", "of them workers"),
    ("deaths_seen_low", "seen <35%"), ("deaths_healed", "healed"),
    ("median_window_low", "<35% -> death"), ("median_window_critical", "<20% -> death"),
    ("fights_with_cast", "fights with a cast"), ("median_first_cast_after", "start -> first cast"),
    ("median_decision_after", "start -> decision"), ("median_orders_after", "start -> orders"),
    ("median_decision_latency", "decision -> orders"), ("casts_in_fights", "casts in fights"),
    ("cta_upkeep_gold", "CTA upkeep"), ("obey_upkeep_gold", "Obey upkeep"), ("paydays_short", "pay days short"),
]
TIMED = {"median_window_low", "median_window_critical", "median_first_cast_after", "median_decision_after",
         "median_orders_after", "median_decision_latency"}


def markdown(rows):
    """One column per game, one row per number (games are few, numbers are many)."""
    if not rows:
        return "(no games with combat statistics)"
    out = ["| | " + " | ".join(label for label, _ in rows) + " |", "|---|" + "---|" * len(rows)]
    for key, title in COLUMNS:
        cells = [(_turns(h[key]) if key in TIMED else ("-" if h[key] is None else str(h[key]))) for _, h in rows]
        out.append("| %s | %s |" % (title, " | ".join(cells)))
    return "\n".join(out)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--store", help="experience.sqlite: one column per game that recorded combat statistics")
    p.add_argument("--log", action="append", default=[], help="daimonkeeper.log (repeatable): one column per keeper in it")
    p.add_argument("--player", type=int, default=None, help="with --log: only this player")
    args = p.parse_args(argv)
    if not args.store and not args.log:
        p.error("give --store and/or --log")
    rows = []
    if args.store:
        rows += rows_from_store(args.store)
    if args.log:
        rows += rows_from_logs(args.log, args.player)
    print(markdown(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
