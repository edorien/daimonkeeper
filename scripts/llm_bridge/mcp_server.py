#!/usr/bin/env python3
"""MCP server: play a dAImon Keeper External seat through whichever MCP-capable assistant is already connected, instead of a
script calling a model API on its own.

**Why MCP instead of bridge.py --policy anthropic.** That policy calls the Messages API directly with a raw
ANTHROPIC_API_KEY: the spend is metered on the key alone, outside whatever usage limits the person's organisation puts on
their normal interactive session, so an unattended bridge with a key can spend past a limit nobody is watching, and it only
ever works with one provider's API. Putting the same loop behind MCP moves the "decide" step into the assistant the person is
already using in Claude Desktop, Claude Code, or any other MCP client, whatever the provider: the spend then goes through
that session's own governance, not a separate metered path, and no model API key lives in this bridge at all. The residual
case this does not cover: if someone points an MCP *client* that itself holds a raw, unmetered API key at this server, that
client, not this server, is where the limit is bypassed -- the guarantee here is "no key in the bridge", not "no key
anywhere it is possible to misuse one".

**Usage.** Register this as an MCP server (stdio transport) with your client, e.g. for Claude Code:

    claude mcp add daimonkeeper -- python3 /path/to/scripts/llm_bridge/mcp_server.py

or in Claude Desktop's config (claude_desktop_config.json):

    "daimonkeeper": {"command": "python3", "args": ["/path/to/scripts/llm_bridge/mcp_server.py"]}

Then, in a conversation: connect (host/port of the running keeperfx, API_ENABLED=TRUE; the game must already have an
External seat, made from Skirmish's Slots & AI page, or pass `claim` to convert a computer keeper), then repeatedly
wait_for_decision -> decide -> submit_orders, as long as you want to keep playing. get_instructions returns the system
prompt this was designed against, for a client that has nowhere else to put it; `prompts/get "play_daimonkeeper"` offers the
same text as an MCP prompt, for a client that surfaces those to the person as a slash command.

**Protocol notes.** Implements just enough of MCP (2024-11-05; later versions are answered the same) over stdio for this purpose: initialize, notifications/initialized,
tools/list, tools/call, prompts/list, prompts/get, ping. One line of JSON per message, newline-terminated, on stdin/stdout; every
diagnostic goes to stderr so stdout stays clean JSON-RPC. One Session (one game connection) per server process, matching the
game's own "one API client at a time" rule. No `$/cancelRequest` support: a blocked wait_for_decision runs to its own timeout.
Standard library only, like the rest of scripts/llm_bridge/. The same Server over HTTP on localhost, for a client that
cannot start a stdio server itself: mcp_http_server.py.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import prompt  # noqa: E402
from mcp_session import Session, SessionError  # noqa: E402

_ORDER_ITEM_SCHEMA = {"type": "object", "properties": prompt.ORDER_ITEM_PROPERTIES, "required": prompt.ORDER_ITEM_REQUIRED}

PROTOCOL_VERSION = "2024-11-05"
# Versions this server can answer as: what it uses (initialize, tools, prompts, ping) is the same in each. The client's
# requested one is echoed back when listed here (HTTP clients, mcp_http_server.py, ask for 2025-03-26 or later), else ours.
SUPPORTED_PROTOCOL_VERSIONS = ("2024-11-05", "2025-03-26", "2025-06-18", "2025-11-25")
SERVER_NAME, SERVER_VERSION = "daimonkeeper-bridge", "0.1.0"
# The prompt's name before the game was renamed; still answered, so existing client setups keep working.
OLD_PROMPT_NAME = "play_keeperfx"

_ASSIST_RULE_SCHEMA = {
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "your name for the rule"},
        "power": {"type": "string", "enum": ["POWER_HEAL_CREATURE", "POWER_SPEED", "POWER_PROTECT", "POWER_CONCEAL",
                                             "POWER_CHICKEN", "POWER_DISEASE", "POWER_LIGHTNING", "POWER_CAVE_IN"],
                  "description": "cast this (the first four on your creatures, the rest on enemies)"},
        "power_off": {"type": "string", "enum": ["POWER_CALL_TO_ARMS", "POWER_OBEY"],
                      "description": "instead of a cast: switch this power off (a gold guard)"},
        "hand": {"type": "string", "enum": ["evacuate_diggers", "lift_wounded", "reinforce"],
                 "description": ("instead of a power: a hand rule, carried out with your hand as an urgent order. evacuate_diggers: lift "
                                 "your imps within radius slabs of an enemy fighter to `to` (required: a safe spot -- imps run back "
                                 "to an attacked heart, so best a pocket they cannot walk out of). lift_wounded: lift your fighters "
                                 "in a fight under 35% health (or your health_pct condition) to `to`, else a lair -- only when no "
                                 "heal rule of yours can heal them now (none, unavailable, or its budget/cap/reserve spent). "
                                 "reinforce: drop healthy fighters from elsewhere beside a fight you are losing or matching "
                                 "(force_ratio >= 80, or your condition), each at least min_match % of the strongest enemy's force, "
                                 "and only if they would turn it")},
        "to": {"type": "array", "items": {"type": "integer"}, "minItems": 2, "maxItems": 2, "description": "hand rules: the subtile to drop on"},
        "count": {"type": "integer", "minimum": 1, "maximum": 8, "description": "hand rules: most creatures one trip moves"},
        "radius": {"type": "integer", "minimum": 1, "maximum": 15, "description": "hand rules: slabs from an enemy (evacuate) or the fight (reinforce, from where it takes them) (default 6)"},
        "min_match": {"type": "integer", "minimum": 0, "maximum": 200, "description": "reinforce: % of the strongest enemy's force each fighter sent must have (default 50)"},
        "level": {"type": "integer", "minimum": 0, "maximum": 8},
        "target": {"type": "string", "enum": ["ally_in_fight", "ally_any", "enemy_in_fight"],
                   "description": "default: ally_in_fight for your powers, enemy_in_fight for the others"},
        "pick": {"type": "string", "enum": ["lowest_health", "strongest", "highest_level", "most_valuable"]},
        "when": {"type": "array", "maxItems": 4, "items": {"type": "array", "minItems": 3, "maxItems": 3},
                 "description": ("conditions, all must hold: [fact, op, value]. op: < <= > >= == !=. Target facts: health_pct, "
                                 "health, level, force, is_digger, on_own_ground, fleeing, kind (a creature name, == or !=), "
                                 "battle_ally_force, battle_enemy_force, force_ratio (enemy force x100 / yours: over 100 you are "
                                 "losing), battle_size, allies_near (your or your allies' creatures the power's area would hit). "
                                 "Dungeon facts: gold, gold_after_wages, payday_turns, fights, fighters, cta_active, cta_paying, "
                                 "obey_active, heart_health_pct.")},
        "cooldown": {"type": "integer", "minimum": 0, "maximum": 10000, "description": "turns between two casts by this rule (default 20)"},
        "target_cooldown": {"type": "integer", "minimum": 0, "maximum": 10000, "description": "turns before the same creature again (default 100 for your creatures)"},
        "budget": {"type": "integer", "minimum": 0, "description": "gold this rule may spend per pay day (0 = no limit)"},
        "max_allies_hit": {"type": "integer", "minimum": 0, "maximum": 15, "description": "Lightning / Cave-in: how many of yours it may also hit (default 0)"},
    },
}
_ASSIST_SETTINGS = {
    "enabled": {"type": "boolean"},
    "reserve": {"type": "integer", "minimum": 0},
    "interval": {"type": "integer", "minimum": 1, "maximum": 20},
    "min_gap": {"type": "integer", "minimum": 1, "maximum": 200},
    "max_spend": {"type": "integer", "minimum": 0},
    "charge_time": {"type": "boolean"},
}

TOOLS = [
    {
        "name": "connect",
        "description": "Connect to a running dAImon Keeper game and take its External seat. Call this once, before anything else.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "host": {"type": "string", "description": "default 127.0.0.1"},
                "port": {"type": "integer", "description": "default 5599 (daimonkeeper.cfg API_PORT)"},
                "claim": {"type": "integer", "description": "convert this player's computer keeper into the seat, if the game did not already make one"},
                "min_interval_turns": {"type": "integer", "description": "least game turns between two decision-due notices (default 100)"},
                "max_age_turns": {"type": "integer", "minimum": 1, "description": "game turns after a decision's view that its orders are still accepted (default 1500, about 75 s at normal speed); later ones are refused STALE_VIEW. Raise it if you decide slowly, at the risk of acting on an older view"},
                "takeover": {"type": "boolean", "description": "let the built-in AI take the seat if this connection drops"},
                "experience_path": {"type": "string", "description": "sqlite file of game records and lessons across games (default: experience.sqlite in the bridge's own folder; \"\" to play without one)"},
                "agent": {"type": "string", "description": "a name for you in the game records (default: the MCP client's name)"},
                "start_paused": {"type": "boolean", "description": "in a game no human plays a keeper in: pause it on connect, for you to resume (default true)"},
                "on_urgent": {"type": "string", "enum": ["auto", "pause", "slow", "none"],
                              "description": "hold the game for your answer to each urgent decision: pause it, or slow it (slow_turns_per_second), for at most hold_seconds. Only where you may pause anyway (no human keeper, or LLM Pause). auto (default): pause where allowed, else none"},
                "hold_seconds": {"type": "integer", "minimum": 1, "maximum": 600, "description": "the longest a hold lasts without your answer (default 60)"},
                "slow_turns_per_second": {"type": "integer", "minimum": 1, "maximum": 100, "description": "on_urgent slow: the speed while held (default 4)"},
            },
        },
    },
    {
        "name": "get_instructions",
        "description": "The system prompt this bridge was designed against: how to play, the verbs, coordinates, your memory. Read this once near the start of the session.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "wait_for_decision",
        "description": ("Waits for the game to say a decision is due (each quarter of a pay day, or an event such as a fight, "
                         "an attack, a breach, a new creature kind), up to timeout_seconds. Returns due=false if none arrived in "
                         "time (call it again); due=true with the full state text to decide from otherwise. Call submit_orders "
                         "right after a due=true result. When the level is won or lost (or the game leaves it) the result has game_over=true "
                         "and victory_state, and usually a debrief: answer it with record_debrief. After game over only "
                         "record_debrief, get_debrief, get_experience, get_log_tail, get_combat_stats, get_combat_rules and disconnect are accepted."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "timeout_seconds": {"type": "number", "description": "how long to wait this call (default 50, at most 50: a "
                                    "client gives up on a reply after about a minute; a decision that came at the end of a "
                                    "long wait is handed out again by the next call until submit_orders answers it)"},
                "treat_timeout_as_decision": {"type": "boolean", "description": "if true, a timeout still returns due=true with reasons [\"timer\"] instead of due=false"},
            },
        },
    },
    {
        "name": "status",
        "description": ("A quick look at the game (turn, gold, victory state, queued orders) without it counting as a decision. "
                        "full: true gives the whole state as a full decision shows it (for a flash decision that needs more)."),
        "inputSchema": {"type": "object", "properties": {"full": {"type": "boolean"}}},
    },
    {
        "name": "look",
        "description": "A window of the map you know, as text, with coordinates (slab rectangle, at most 40 x 30).",
        "inputSchema": {
            "type": "object",
            "properties": {"slab_rect": {"type": "array", "items": {"type": "integer"}, "minItems": 4, "maxItems": 4}},
            "required": ["slab_rect"],
        },
    },
    {
        "name": "submit_orders",
        "description": "Send your decided batch of orders. They run one after another; each is re-validated when its turn comes, so a stale one is dropped and reported rather than misapplied. Call after a wait_for_decision that returned due=true.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "reasoning": {"type": "string", "description": "one or two sentences on why these orders"},
                "plan": {"type": "string", "description": "your rewritten strategic plan (omit to keep the current one)"},
                "notes": {"type": "string", "description": "your rewritten notes (omit to keep the current ones)"},
                "orders": {"type": "array", "items": _ORDER_ITEM_SCHEMA},
                "facts": prompt.FACTS_PROPERTY,
                "chapter_note": prompt.CHAPTER_NOTE_PROPERTY,
                "places": prompt.PLACES_PROPERTY,
                "lessons": prompt.LESSONS_PROPERTY,
            },
            "required": ["orders"],
        },
    },
    {
        "name": "check_orders",
        "description": ("Validate a batch of candidate orders -- the same checks submit_orders would run, including cost -- "
                         "without spending them: for each, would_succeed and its step count, or the error a real submit would "
                         "give. Use this before submit_orders when unsure an order (a room's cost, a rectangle's bounds, a "
                         "power's availability) will be accepted."),
        "inputSchema": {
            "type": "object",
            "properties": {"orders": {"type": "array", "items": _ORDER_ITEM_SCHEMA}},
            "required": ["orders"],
        },
    },
    {
        "name": "set_combat_rules",
        "description": ("Set your standing orders for fights: rules the game itself carries out every couple of turns, between "
                         "your decisions, so a creature dropping low is healed in a second, not after your next call. Give the "
                         "whole list, in priority order (the first rule that can act each time wins); each rule may have a "
                         "\"name\" you refer to it by. Only what changed is sent; it goes live a few turns later, all at once. "
                         "Settings: enabled, reserve (gold no cast may go below), interval (turns between looks, 1-20), "
                         "min_gap (turns between any two casts), max_spend (gold per pay day, 0 = no cap), charge_time "
                         "(wait 4 turns per level as a human charging must; default on). Leave rules out to change only "
                         "settings. A rule may be a hand rule instead (\"hand\": evacuate_diggers, lift_wounded, reinforce -- see the field): "
                         "the game moves your creatures with your hand, as an urgent order, one trip at a time. An error names the rule at fault."),
        "inputSchema": {
            "type": "object",
            "properties": dict({"rules": {"type": "array", "items": _ASSIST_RULE_SCHEMA, "maxItems": 16},
                                "bank": {"type": "integer", "minimum": 1, "maximum": 3,
                                         "description": "store the set in this bank instead of making it live (use_combat_bank switches to it in one packet)"},
                                "label": {"type": "string", "description": "your name for the bank's set (e.g. war, calm)"}}, **_ASSIST_SETTINGS),
        },
    },
    {
        "name": "use_combat_bank",
        "description": ("Make one of your combat assistant banks (stored with set_combat_rules bank=N) the live rule set: one packet, "
                        "ahead of every routine order, so a calm set becomes a war set the moment a fight starts. Rules that "
                        "are the same in both keep their cooldowns and budgets."),
        "inputSchema": {"type": "object", "properties": {"bank": {"type": "integer", "minimum": 1, "maximum": 3}}, "required": ["bank"]},
    },
    {
        "name": "set_triggers",
        "description": ("Contingency orders you write ahead, sent by the bridge the moment the game pushes the urgent decision "
                        "they are for -- no reply of yours in between, so they act in a couple of turns. The whole list (at "
                        "most 12; [] clears it). Each: name; when {reasons (default: any urgent one), region [x0,y0,x1,y1] "
                        "slabs or near_rally NAME, facts [[fact, op, value]] with facts enemies_near, imps_near, "
                        "fighters_near, intruders, gold}; do: up to 4 ordinary orders with placeholders -- pos \"$where\", "
                        "\"$rally:NAME\", \"$drop_near:where\", \"$drop_near:rally:NAME\"; thing_ids \"$imps[:N]\", "
                        "\"$fighters[:N]\", \"$wounded[:N]\" (under 35% health), \"$reserve[:N]\" (healthy fighters away from "
                        "it); thing_id \"$enemy\" / \"$wounded\"; to_room \"$heart\" -- sent urgent (priority now); "
                        "cooldown (turns, default 300), once, expires_turn. They are kept with your memory (saved with the "
                        "game); TRIGGERS in the state says what they did. Two refusals in a row disarm one."),
        "inputSchema": {"type": "object", "properties": {"triggers": {"type": "array", "items": {"type": "object"}, "maxItems": 12}}},
    },
    {
        "name": "check_triggers",
        "description": "set_triggers without changing anything: the same checks.",
        "inputSchema": {"type": "object", "properties": {"triggers": {"type": "array", "items": {"type": "object"}, "maxItems": 12}}},
    },
    {
        "name": "get_triggers",
        "description": "Your triggers as written (ready to edit and send back), their counters, your rally points, the latest firings and the stock presets.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "apply_trigger_preset",
        "description": ("Add a stock trigger to your list (or replace the one of its name): evacuate_imps (lift imps near an "
                        "enemy to rally point safe), lift_wounded (lift fighters under 35% out of a fight to safe), "
                        "reinforce_heart (drop healthy fighters beside your heart when it is attacked). overrides: name, "
                        "cooldown, once, expires_turn, when, do. Without a name: the presets."),
        "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}, "overrides": {"type": "object"},
                                                         "dry_run": {"type": "boolean"}}},
    },
    {
        "name": "set_rally_points",
        "description": ("Name spots (subtiles [x, y]) your triggers and flash decisions refer to, e.g. {\"safe\": [40, 61]} -- "
                        "pick safe spots away from where enemies go (heroes head for your heart; imps run back to defend an "
                        "attacked heart, so a safe spot for imps is a pocket of floor they cannot walk out of); null removes one."),
        "inputSchema": {"type": "object", "properties": {"points": {"type": "object"}}, "required": ["points"]},
    },
    {
        "name": "check_combat_rules",
        "description": "set_combat_rules without sending anything: the same checks the game would make, and how many rules would change.",
        "inputSchema": {
            "type": "object",
            "properties": dict({"rules": {"type": "array", "items": _ASSIST_RULE_SCHEMA, "maxItems": 16}}, **_ASSIST_SETTINGS),
        },
    },
    {
        "name": "get_combat_rules",
        "description": "Your standing orders as they are live now, by name, ready to edit and send back, with what each did and why any is held back.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "apply_combat_preset",
        "description": ("Apply one of the game's preset standing orders (guardian, striker, miser; call without a name to see "
                         "what each does), optionally changed: overrides = {settings..., \"drop\": [rule names], "
                         "\"rules\": {name: {fields to change}}} (a new name adds a rule)."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "name": {"type": "string"},
                "overrides": {"type": "object"},
                "dry_run": {"type": "boolean"},
            },
        },
    },
    {
        "name": "set_game_speed",
        "description": ("Slow down or speed up the simulation's real-time pace (how many simulated turns run per real "
                         "second) so you have more or less wall-clock time to decide. Global to the game, not just your "
                         "seat -- a human sharing the session sees the same change. turns_per_second=0 resets to the "
                         "game's own configured rate."),
        "inputSchema": {
            "type": "object",
            "properties": {"turns_per_second": {"type": "integer", "minimum": 0, "maximum": 100}},
            "required": ["turns_per_second"],
        },
    },
    {
        "name": "set_pause",
        "description": ("Pause or resume the game, in a game where no human plays a keeper (otherwise refused: use "
                         "set_game_speed). Such a level starts paused so you can look it over first; pause again whenever "
                         "you need longer to think -- nothing moves while paused, and the orders you send wait for it to "
                         "run. paused=false with turns=N runs N turns, then pauses again."),
        "inputSchema": {
            "type": "object",
            "properties": {"paused": {"type": "boolean"},
                           "turns": {"type": "integer", "minimum": 1, "maximum": 100000,
                                     "description": "with paused=false: run this many turns, then pause again"}},
            "required": ["paused"],
        },
    },
    {
        "name": "get_log_tail",
        "description": "The game's own recent log output, for debugging a confusing session without a human tailing daimonkeeper.log by hand.",
        "inputSchema": {
            "type": "object",
            "properties": {"lines": {"type": "integer", "minimum": 1, "maximum": 500, "description": "default 100"}},
        },
    },
    {
        "name": "get_combat_stats",
        "description": ("How quickly fights are being answered, as the game measured it: creatures lost in fights and how long "
                         "they had between dropping low and dying, time from a fight's start to the first power cast and to your "
                         "orders, what Call to Arms and Must Obey cost in upkeep, pay days short of gold."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "get_recipes",
        "description": "The active temple sacrifice recipes (listed once you own a temple): which creatures, dropped into the pool together, do what.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "get_experience",
        "description": "Your lessons from earlier games that apply to this one (playbook, campaign, level, opponents), every one with its id, and your record here: shown at the start of a game (the best-ranked within a budget), and all of them here on demand.",
        "inputSchema": {"type": "object", "properties": {"topic": {"type": "string", "description": "only this topic's lessons: setup, economy, defence, combat, attack, heroes, creatures, rooms, research, map, tools, opponent, general"}}},
    },
    {
        "name": "name_place",
        "description": ("Name a spot to use later as pos (or slab_rect, for a rectangle) in orders and hand rules: a subtile pos, or "
                        "a slab_rect; neither drops your place of that name. Your heart, rooms, hero gates and enemy hearts are named "
                        "for you (PLACES). Same as places on submit_orders."),
        "inputSchema": {"type": "object", "properties": {
            "name": {"type": "string"},
            "pos": {"type": "array", "items": {"type": "integer"}, "minItems": 2, "maxItems": 2},
            "slab_rect": {"type": "array", "items": {"type": "integer"}, "minItems": 4, "maxItems": 4}}, "required": ["name"]},
    },
    {
        "name": "remember_lesson",
        "description": "Keep something this game just taught you, for next time (one idea, concrete). It is kept as new until the debrief keeps, revises or drops it, and recalled meanwhile marked (new). Same as lessons.remember on submit_orders.",
        "inputSchema": {"type": "object", "properties": {
            "text": {"type": "string"},
            "scope": {"type": "string", "description": "playbook, campaign, level or opponent:<identity> (default: level if it names places, else playbook)"},
            "topic": {"type": "string"}}, "required": ["text"]},
    },
    {
        "name": "confirm_lesson",
        "description": "A lesson (its id, as RECALL / EXPERIENCE show it) just held in this game: counted for it, so it ranks higher.",
        "inputSchema": {"type": "object", "properties": {"id": {"type": "integer"}}, "required": ["id"]},
    },
    {
        "name": "doubt_lesson",
        "description": "A lesson just failed in this game: counted against it (the debrief decides whether to revise or retire it).",
        "inputSchema": {"type": "object", "properties": {"id": {"type": "integer"}, "why": {"type": "string"}}, "required": ["id"]},
    },
    {
        "name": "get_debrief",
        "description": "The debrief of an earlier attempt that never had one (EXPERIENCE lists them): its record and every lesson, to answer with record_debrief(attempt=...).",
        "inputSchema": {"type": "object", "properties": {"attempt": {"type": "string", "description": "the attempt as EXPERIENCE names it (abcd1234/0123456789ab)"}},
                        "required": ["attempt"]},
    },
    {
        "name": "record_debrief",
        "description": ("After a game ends (wait_for_decision gave game_over with a debrief), or for an earlier attempt (attempt): "
                         "keep what it taught you. The summary goes on the game's record; confirm, revise, retire and add edit the "
                         "lessons by id. A lesson you do not mention stays as it is."),
        "inputSchema": {"type": "object", "properties": dict(prompt.DEBRIEF_PROPERTIES, **{
            "attempt": {"type": "string", "description": "an earlier attempt EXPERIENCE listed (omit for the game that just ended)"},
            "decline": {"type": "boolean", "description": "with attempt: it taught nothing worth keeping; it is not offered again"}})},
    },
    {
        "name": "disconnect",
        "description": ("Close the connection to the game. Call this when you are done playing. An attempt still being played stays "
                        "open (connecting again continues it) unless end says otherwise: end=quit closes it and gives its debrief "
                        "(answer with record_debrief, then disconnect again); end=suspend keeps it to be continued from a save."),
        "inputSchema": {"type": "object", "properties": {"end": {"type": "string", "enum": ["quit", "suspend"]}}},
    },
]

PROMPTS = [
    {"name": "play_daimonkeeper", "description": "How to play a dAImon Keeper External seat through this server's tools."},
]


def _text_result(text, is_error=False):
    result = {"content": [{"type": "text", "text": text}]}
    if is_error:
        result["isError"] = True
    return result


# Tools whose reply carries the urgent decisions that came in meanwhile (Session.alerts_text): everything but the ones that
# are themselves the decision loop's start, its wait, or its end.
_NO_ALERTS = ("connect", "get_instructions", "wait_for_decision", "submit_orders", "record_debrief", "disconnect")


def _dispatch_tool(session, name, args):
    out = _run_tool(session, name, args)
    alerts = session.alerts_text() if (name not in _NO_ALERTS and hasattr(session, "alerts_text")) else ""
    if not alerts:
        return out
    if out.startswith("{") and out.endswith("}"):
        try:
            data = json.loads(out)
            data["alerts"] = alerts
            return json.dumps(data)
        except ValueError:
            pass
    return out + "\n\n" + alerts


def _run_tool(session, name, args):
    args = args or {}
    if name == "connect":
        return session.connect(**args)
    if name == "get_instructions":
        return session.instructions()
    if name == "wait_for_decision":
        return json.dumps(session.wait_for_decision(**args))
    if name == "status":
        return session.status(full=bool(args.get("full"))) if args.get("full") else session.status()
    if name == "look":
        return session.look(args["slab_rect"])
    if name == "submit_orders":
        return json.dumps(session.submit(args.get("orders", []), reasoning=args.get("reasoning", ""),
                                          plan=args.get("plan"), notes=args.get("notes"), lessons=args.get("lessons"),
                                          facts=args.get("facts"), chapter_note=args.get("chapter_note"), places=args.get("places")))
    if name == "check_orders":
        return json.dumps(session.check_orders(args.get("orders", [])))
    if name == "set_game_speed":
        return session.set_speed(args["turns_per_second"])
    if name == "set_pause":
        return session.set_pause(bool(args["paused"]), args.get("turns"))
    if name == "get_log_tail":
        return session.log_tail(args.get("lines", 100))
    if name == "set_combat_rules":
        return json.dumps(session.set_combat_rules(args.get("rules"), bank=args.get("bank"), label=args.get("label"),
                                                   **{k: args[k] for k in _ASSIST_SETTINGS if k in args}))
    if name == "check_combat_rules":
        return json.dumps(session.check_combat_rules(args.get("rules"), **{k: args[k] for k in _ASSIST_SETTINGS if k in args}))
    if name == "use_combat_bank":
        return json.dumps(session.use_combat_bank(args["bank"]))
    if name == "set_triggers":
        return json.dumps(session.set_triggers(args.get("triggers")))
    if name == "check_triggers":
        return json.dumps(session.check_triggers(args.get("triggers")))
    if name == "get_triggers":
        return json.dumps(session.get_triggers())
    if name == "apply_trigger_preset":
        return json.dumps(session.apply_trigger_preset(args.get("name"), args.get("overrides"), bool(args.get("dry_run"))))
    if name == "set_rally_points":
        return json.dumps(session.set_rally_points(args.get("points")))
    if name == "get_combat_rules":
        return json.dumps(session.get_combat_rules())
    if name == "apply_combat_preset":
        return json.dumps(session.apply_combat_preset(args.get("name"), args.get("overrides"), bool(args.get("dry_run"))))
    if name == "get_combat_stats":
        return session.combat_stats()
    if name == "get_recipes":
        return session.recipes()
    if name == "get_experience":
        return session.experience_text(args.get("topic"))
    if name == "name_place":
        return json.dumps(session.name_place(args["name"], args.get("pos"), args.get("slab_rect")))
    if name == "remember_lesson":
        return json.dumps(session.remember_lesson(args["text"], args.get("scope"), args.get("topic")))
    if name == "confirm_lesson":
        return json.dumps(session.confirm_lesson(args["id"]))
    if name == "doubt_lesson":
        return json.dumps(session.doubt_lesson(args["id"], args.get("why", "")))
    if name == "get_debrief":
        return session.get_debrief(args.get("attempt"))
    if name == "record_debrief":
        edits = {k: v for k, v in (args or {}).items() if k not in ("attempt", "decline")}
        return json.dumps(session.record_debrief(edits, attempt=args.get("attempt"), decline=bool(args.get("decline"))))
    if name == "disconnect":
        out = session.disconnect(end=args.get("end"))
        return out if isinstance(out, str) else json.dumps(out)
    raise KeyError(name)


_TOOL_SCHEMAS = {t["name"]: t["inputSchema"] for t in TOOLS}


def _missing_arguments(name, args):
    """The tool's required arguments (its inputSchema) that the call left out."""
    return [a for a in _TOOL_SCHEMAS[name].get("required", []) if a not in (args or {})]


class Server:
    def __init__(self, session=None):
        self.session = session or Session()

    def handle(self, req):
        """One JSON-RPC request/notification dict -> a response dict, or None for a notification (no `id`)."""
        method = req.get("method")
        req_id = req.get("id")
        is_notification = "id" not in req
        try:
            if method == "initialize":
                params = req.get("params") or {}
                client = (params.get("clientInfo") or {}).get("name")
                if client:
                    self.session.client_name = client
                version = params.get("protocolVersion")
                if version not in SUPPORTED_PROTOCOL_VERSIONS:
                    version = PROTOCOL_VERSION
                result = {"protocolVersion": version, "capabilities": {"tools": {}, "prompts": {}},
                          "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION}}
            elif method == "notifications/initialized":
                return None
            elif method == "ping":
                result = {}
            elif method == "tools/list":
                result = {"tools": TOOLS}
            elif method == "prompts/list":
                result = {"prompts": PROMPTS}
            elif method == "prompts/get":
                if (req.get("params") or {}).get("name") not in ("play_daimonkeeper", OLD_PROMPT_NAME):
                    return self._error(req_id, -32602, "unknown prompt")
                result = {"messages": [{"role": "user", "content": {"type": "text", "text": prompt.SYSTEM_PROMPT}}]}
            elif method == "tools/call":
                params = req.get("params") or {}
                tool_name = params.get("name")
                missing = _missing_arguments(tool_name, params.get("arguments")) if tool_name in _TOOL_SCHEMAS else None
                try:
                    if tool_name not in _TOOL_SCHEMAS:
                        result = _text_result("unknown tool: %r" % (tool_name,), is_error=True)
                    elif missing:
                        result = _text_result("%s: missing argument%s %s (its inputSchema: %s)" % (
                            tool_name, "s" if len(missing) > 1 else "", ", ".join(missing),
                            json.dumps(_TOOL_SCHEMAS[tool_name].get("properties", {}))), is_error=True)
                    else:
                        result = _text_result(_dispatch_tool(self.session, tool_name, params.get("arguments")))
                except KeyError as e:
                    result = _text_result("%s: missing argument %s" % (tool_name, e), is_error=True)
                except SessionError as e:
                    result = _text_result(str(e), is_error=True)
                except Exception as e:  # noqa: BLE001 - a tool failure is reported to the model, not a protocol error
                    result = _text_result("%s: %s" % (type(e).__name__, e), is_error=True)
            else:
                if is_notification:
                    return None
                return self._error(req_id, -32601, "unknown method: %r" % (method,))
        except Exception as e:  # noqa: BLE001 - malformed request or the like; still answer if we can
            if is_notification:
                return None
            return self._error(req_id, -32603, "%s: %s" % (type(e).__name__, e))
        if is_notification:
            return None
        return {"jsonrpc": "2.0", "id": req_id, "result": result}

    @staticmethod
    def _error(req_id, code, message):
        return {"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}}


def main():
    server = Server()
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except ValueError as e:
            print("daimonkeeper-bridge: bad JSON on stdin: %s" % (e,), file=sys.stderr)
            sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}}) + "\n")
            sys.stdout.flush()
            continue
        resp = server.handle(req)
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
