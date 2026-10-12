"""The combat assistant on the bridge side (docs/newFeatures/llm/): the agent's standing orders for fights.

The game carries the rules out every few turns (kfx_ai/keeper_assist.c); the agent sets them with set_assist and reads
them back in its view (own.assist). This module is what the bridge adds: the agent's own names for its rules (kept in
its memory, by slot), the STANDING ORDERS section of the prompt (what each rule did since the last decision), and preset
handling. Shared by mcp_session.py and prompt.py so the two cannot drift apart.
"""

SETTINGS = ("enabled", "interval", "reserve", "min_gap", "max_spend", "charge_time")


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


def describe(rule):
    """One rule in a line, in the words it was written in."""
    if "power_off" in rule:
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
               "cannot_afford": "not enough gold"}


def rules_in_words(view, memory):
    """The live rules as the agent would write them again (with their names), plus the settings: what get_combat_rules
    returns, ready to edit and send back."""
    a = (view or {}).get("own", {}).get("assist") or {}
    out = {k: a[k] for k in SETTINGS if k in a}
    rules = []
    for r in a.get("rules", []):
        w = {"name": memory.rule_name(r["slot"]) if memory else "rule %d" % r["slot"]}
        for k in ("power", "power_off", "level", "target", "pick", "when", "cooldown", "target_cooldown", "budget", "max_allies_hit"):
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
    a = (view or {}).get("own", {}).get("assist")
    if not a or (not a.get("rules") and not a.get("enabled")):
        return []
    lines = ["STANDING ORDERS (combat assistant %s; reserve %d, spent this pay day %d; every %d turns):" % (
        "ON" if a.get("enabled") else "OFF", a.get("reserve", 0), a.get("spent_this_payday", 0), a.get("interval", 0))]
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
