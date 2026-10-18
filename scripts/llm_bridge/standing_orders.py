"""The combat assistant on the bridge side (docs/newFeatures/llm/): the agent's standing orders for fights.

The game carries the rules out every few turns (kfx_ai/keeper_assist.c); the agent sets them with set_assist and reads
them back in its view (own.assist). This module is what the bridge adds: the agent's own names for its rules (kept in
its memory, by slot), the STANDING ORDERS section of the prompt (what each rule did since the last decision), and preset
handling. Shared by mcp_session.py and prompt.py so the two cannot drift apart.
"""

import json

SETTINGS = ("enabled", "interval", "reserve", "min_gap", "max_spend", "charge_time")

# The cap a set with casting rules gets when nothing caps it yet (the game's own default is none): one bad set of rules
# once spent 14,300 gold in a pay day. The agent may set any cap, or max_spend: 0 for none.
DEFAULT_MAX_SPEND = 5000


def default_cap(req, view):
    """`req` (a set_assist request) with DEFAULT_MAX_SPEND added when it brings casting rules, says nothing about
    max_spend, and the live set has no cap; returns the cap added, or None."""
    if "max_spend" in req or not any(r.get("power") for r in req.get("rules") or []):
        return None
    a = (view or {}).get("own", {}).get("assist") or {}
    if a.get("max_spend", 0) > 0:
        return None
    req["max_spend"] = DEFAULT_MAX_SPEND
    return DEFAULT_MAX_SPEND


class RulesError(Exception):
    def __init__(self, code, rule=None, name=None):
        super().__init__("%s%s" % (code, (" (rule %d%s)" % (rule, ", %s" % name if name else "")) if rule is not None else ""))
        self.code, self.rule, self.name = code, rule, name


def prepare(rules, settings):
    """The set_assist request for `rules` (a list, in priority order; each may carry a "name") and the settings given,
    and the names by slot. A rule without a name is called "rule N"; two rules may not share one."""
    req = {k: v for k, v in (settings or {}).items() if k in SETTINGS and v is not None}
    names = []
    if rules is not None:
        if not isinstance(rules, list):
            raise RulesError("BAD_RULES")
        for i, r in enumerate(rules):
            if not isinstance(r, dict):
                raise RulesError("BAD_RULE", i)
            name = str(r.get("name") or "rule %d" % i)
            if name in names:
                raise RulesError("DUPLICATE_NAME", i, name)
            names.append(name)
        req["rules"] = [dict(r) for r in rules]   # "name" goes along: the game ignores it
    return req, names


_RULE_FIELDS = ("power", "power_off", "level", "target", "pick", "when", "cooldown", "target_cooldown", "budget", "max_allies_hit",
                "hand", "to", "count", "radius", "min_match")
HAND_WORDS = {"evacuate_diggers": "lift the imps near an enemy", "lift_wounded": "lift wounded fighters out of a fight",
              "reinforce": "drop matching fighters beside a fight they can turn"}


def rule_sig(rule):
    """A live rule's content (not its counters), to tell whether the rule in a slot is still the one a name was given to."""
    return json.dumps({k: rule[k] for k in _RULE_FIELDS if k in rule}, sort_keys=True)


def live_sigs(view):
    """{slot: rule_sig} of the rules live in `view`."""
    a = (view or {}).get("own", {}).get("assist") or {}
    return {r["slot"]: rule_sig(r) for r in a.get("rules", [])}


def name_rules(memory, names, view, verb_id=None, unchanged=False):
    """A rule change was accepted: its names belong to its rules, which go live only when the queued set_assist runs
    (a few turns, or once a paused game runs again). Until then they wait as pending (sync_names adopts them when the
    live rules change, drops them if the order is refused); a change that altered nothing names the live rules now."""
    if unchanged:
        live = live_sigs(view)
        memory.rule_names = list(names)
        memory.rule_sigs = [live.get(i) for i in range(len(names))]
        memory.pending_rules = None
    else:
        memory.pending_rules = {"names": list(names), "before": {str(k): v for k, v in live_sigs(view).items()}, "id": verb_id}


def sync_names(view, memory):
    """Keeps the names with their rules: a pending change's names are adopted once the live rules have changed, and
    dropped if its order was refused; a slot whose live rule is no longer the one named loses its name. (Before this a
    preset switch renamed the old rules at once: striker's casting rules showed under miser's names until -- or, when
    the order never ran, for good after -- the change went live.)"""
    if memory is None or view is None or not (view.get("own") or {}).get("assist"):
        return
    live = live_sigs(view)
    p = memory.pending_rules
    if p:
        before = {int(k): v for k, v in (p.get("before") or {}).items()}
        if live != before:
            memory.rule_names = list(p["names"])
            memory.rule_sigs = [live.get(i) for i in range(len(p["names"]))]
            memory.pending_rules = None
        else:
            res = next((r for r in (view.get("seat") or {}).get("results", []) if r.get("id") == p.get("id")), None)
            if res is not None and res.get("status") == "rejected":
                memory.pending_rules = None
                memory._assist_note = "your last change of standing orders was refused (%s): the rules below are still the ones live" % res.get("error")
    for i, sig in enumerate(list(memory.rule_sigs or [])):
        if sig is not None and live.get(i) != sig:
            if i < len(memory.rule_names):
                memory.rule_names[i] = None
            memory.rule_sigs[i] = None


def describe(rule):
    """One rule in a line, in the words it was written in."""
    if "hand" in rule:
        head = "%s (hand: %s%s, up to %d%s)" % (
            rule["hand"], HAND_WORDS.get(rule["hand"], "?"),
            (" to (%d,%d)" % tuple(rule["to"])) if rule.get("to") else (" to a lair" if rule["hand"] == "lift_wounded" else ""),
            rule.get("count", 0), (", each at least %d%% of the strongest enemy" % rule.get("min_match", 50)) if rule["hand"] == "reinforce" else "")
    elif "power_off" in rule:
        head = "switch %s off" % rule["power_off"]
    else:
        head = "%s L%d on %s%s" % (rule["power"], rule.get("level", 0), rule.get("target", "?"),
                                   (" (%s first)" % rule["pick"].replace("_", " ")) if rule.get("pick") else "")
    when = rule.get("when") or []
    cond = (" when " + ", ".join("%s %s %s" % (c[0], c[1], c[2]) for c in when)) if when else " always"
    extra = []
    if rule.get("budget"):
        extra.append("budget %d per pay day" % rule["budget"])
    if rule.get("max_allies_hit"):
        extra.append("may hit up to %d of ours" % rule["max_allies_hit"])
    return head + cond + ((" (" + "; ".join(extra) + ")") if extra else "")


STATE_WORDS = {"ok": "ok", "unavailable": "power not researched", "budget_spent": "budget spent for this pay day",
               "max_spend": "the assistant's cap for this pay day is reached", "reserve": "held back by the reserve",
               "cannot_afford": "not enough gold", "HEAL_READY": "held back: a heal rule can heal them",
               "NO_MATCH": "held back: no free fighter is a match for the enemy", "NO_DROP_SPOT": "held back: nowhere to drop near it",
               "WOULD_NOT_TURN_IT": "held back: all it may send would not turn the fight", "NO_LAIR": "held back: no lair to rest in"}


def rules_in_words(view, memory):
    """The live rules as the agent would write them again (with their names), plus the settings: what get_combat_rules
    returns, ready to edit and send back."""
    sync_names(view, memory)
    a = (view or {}).get("own", {}).get("assist") or {}
    out = {k: a[k] for k in SETTINGS if k in a}
    if memory is not None and memory.pending_rules:
        out["pending"] = "a change to %s is queued and not live yet%s" % (", ".join(memory.pending_rules["names"]) or "no rules",
                                                                          " (the game is paused: it goes live when it runs)" if (view or {}).get("paused") else "")
    rules = []
    for r in a.get("rules", []):
        w = {"name": memory.rule_name(r["slot"]) if memory else "rule %d" % r["slot"]}
        for k in ("power", "power_off", "hand", "to", "count", "radius", "min_match", "level", "target", "pick", "when", "cooldown",
                  "target_cooldown", "budget", "max_allies_hit"):
            if k in r:
                w[k] = r[k]
        rules.append(w)
    out["rules"] = rules
    return out


def _creature_label(view, memory, thing_id):
    for c in (view or {}).get("own", {}).get("creatures", []):
        if c["id"] == thing_id:
            name = memory.name_for(c["id"], c["kind"], c.get("born")) if memory else "#%d" % c["id"]
            return "%s (now %d hp)" % (name, c["health"])
    for c in (view or {}).get("visible", {}).get("creatures", []):
        if c["id"] == thing_id:
            return "%s #%d (now %d hp)" % (c["kind"], c["id"], c["health"])
    return "#%d (gone)" % thing_id


def render(view, memory, seen):
    """The STANDING ORDERS section: each rule by name, what it did since the last decision (`seen`, {slot: [fired,
    spent]}, updated here), why it is held back if it is, and the casts since. Empty when the assistant has no rules
    and is off."""
    sync_names(view, memory)
    a = (view or {}).get("own", {}).get("assist")
    if not a or (not a.get("rules") and not a.get("enabled") and not a.get("banks")):
        return []
    cap = a.get("max_spend", 0)
    lines = ["STANDING ORDERS (combat assistant %s; reserve %d, spent this pay day %d%s; every %d turns):" % (
        "ON" if a.get("enabled") else "OFF", a.get("reserve", 0), a.get("spent_this_payday", 0),
        " of its cap %d" % cap if cap > 0 else " (no cap)", a.get("interval", 0))]
    since = seen.get("_turn")
    for r in a.get("rules", []):
        slot = r["slot"]
        before = seen.get(slot, [0, 0])
        fired, spent = r.get("fired", 0), r.get("spent", 0)
        delta = "" if since is None else " (+%d, +%d gold since last decision)" % (fired - before[0], spent - before[1])
        state = r.get("state", "ok")
        lines.append("  %s: %s | fired %d, spent %d%s%s" % (
            memory.rule_name(slot) if memory else "rule %d" % slot, describe(r), fired, spent, delta,
            "" if state == "ok" else " | " + STATE_WORDS.get(state, state)))
        seen[slot] = [fired, spent]
    if memory is not None and memory.pending_rules:
        lines.append("  (a change to %s is queued and not live yet%s; the rules above are the ones live)" % (
            ", ".join(memory.pending_rules["names"]) or "no rules", ": the game is paused, it goes live when it runs" if view.get("paused") else ""))
    if memory is not None and memory._assist_note:
        lines.append("  (" + memory._assist_note + ")")
        memory._assist_note = None
    for b in a.get("banks", []):
        meta = ((memory.bank_names or {}).get(str(b["bank"])) or {}) if memory is not None else {}
        names = [n for n in meta.get("names", []) if n]
        lines.append("  BANK %d%s: %d rule(s)%s, %s%s -- use_combat_bank(%d) makes it live in one packet" % (
            b["bank"], (" '%s'" % meta["label"]) if meta.get("label") else "", len(b.get("rules", [])),
            (" (" + ", ".join(names) + ")") if names else "", "on" if b.get("enabled") else "off",
            " [LIVE NOW]" if a.get("active_bank") == b["bank"] else "", b["bank"]))
    moves = [x for x in a.get("reflexes", []) if since is None or x["turn"] > since]
    if moves:
        lines.append("  hand rules since last decision: " + "; ".join(
            "turn %d %s moved %s to %s" % (x["turn"], memory.rule_name(x["rule"]) if memory else "rule %d" % x["rule"],
                                          ", ".join(_creature_label(view, memory, t) for t in x.get("things", [])),
                                          ("(%d,%d)" % tuple(x["pos"])) if x.get("pos") else "room %s" % x.get("to_room"))
            for x in moves[-6:]))
    casts = [c for c in a.get("casts", []) if since is None or c["turn"] > since]
    if casts:
        lines.append("  done since last decision: " + "; ".join(
            "turn %d %s %s%s" % (c["turn"], memory.rule_name(c["rule"]) if memory else "rule %d" % c["rule"], c["power"],
                                 (" -> " + _creature_label(view, memory, c["target"])) if c.get("target") else " (switched off)")
            for c in casts[-8:]))
    if a.get("pending"):
        p = a["pending"]
        lines.append("  charging: %s on #%d, cast at turn %d" % (memory.rule_name(p["rule"]) if memory else "rule %d" % p["rule"], p["target"], p["due"]))
    seen["_turn"] = view.get("turn")
    return lines


def apply_overrides(preset, overrides):
    """A preset's set_assist request with the agent's changes: settings replaced, "drop": [names] removed, and
    "rules": {name: {fields}} merged into the named rules (or appended when the name is new)."""
    req = {k: v for k, v in preset.items() if k not in ("description", "error", "rule")}
    rules = [dict(r) for r in preset.get("rules", [])]
    overrides = overrides or {}
    for k in SETTINGS:
        if k in overrides:
            req[k] = overrides[k]
    drop = set(overrides.get("drop") or [])
    rules = [r for r in rules if r.get("name") not in drop]
    for name, fields in (overrides.get("rules") or {}).items():
        match = next((r for r in rules if r.get("name") == name), None)
        if match is None:
            rules.append(dict(fields, name=name))
        else:
            match.update(fields)
    req["rules"] = rules
    return req
