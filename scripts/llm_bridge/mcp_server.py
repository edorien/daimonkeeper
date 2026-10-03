#!/usr/bin/env python3
"""MCP server: play a keeperfx External seat through whichever MCP-capable assistant is already connected, instead of a
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

    claude mcp add keeperfx -- python3 /path/to/scripts/llm_bridge/mcp_server.py

or in Claude Desktop's config (claude_desktop_config.json):

    "keeperfx": {"command": "python3", "args": ["/path/to/scripts/llm_bridge/mcp_server.py"]}

Then, in a conversation: connect (host/port of the running keeperfx, API_ENABLED=TRUE; the game must already have an
External seat, made from Skirmish's Slots & AI page, or pass `claim` to convert a computer keeper), then repeatedly
wait_for_decision -> decide -> submit_orders, as long as you want to keep playing. get_instructions returns the system
prompt this was designed against, for a client that has nowhere else to put it; `prompts/get "play_keeperfx"` offers the
same text as an MCP prompt, for a client that surfaces those to the person as a slash command.

**Protocol notes.** Implements just enough of MCP (2024-11-05) over stdio for this purpose: initialize, notifications/initialized,
tools/list, tools/call, prompts/list, prompts/get, ping. One line of JSON per message, newline-terminated, on stdin/stdout; every
diagnostic goes to stderr so stdout stays clean JSON-RPC. One Session (one game connection) per server process, matching the
game's own "one API client at a time" rule. No `$/cancelRequest` support: a blocked wait_for_decision runs to its own timeout.
Standard library only, like the rest of scripts/llm_bridge/.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import prompt  # noqa: E402
from mcp_session import Session, SessionError  # noqa: E402

_ORDER_ITEM_SCHEMA = {"type": "object", "properties": prompt.ORDER_ITEM_PROPERTIES, "required": prompt.ORDER_ITEM_REQUIRED}

PROTOCOL_VERSION = "2024-11-05"
SERVER_NAME, SERVER_VERSION = "keeperfx-bridge", "0.1.0"

TOOLS = [
    {
        "name": "connect",
        "description": "Connect to a running keeperfx and take its External seat. Call this once, before anything else.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "host": {"type": "string", "description": "default 127.0.0.1"},
                "port": {"type": "integer", "description": "default 5599 (keeperfx.cfg API_PORT)"},
                "claim": {"type": "integer", "description": "convert this player's computer keeper into the seat, if the game did not already make one"},
                "min_interval_turns": {"type": "integer", "description": "least game turns between two decision-due notices (default 100)"},
                "takeover": {"type": "boolean", "description": "let the built-in AI take the seat if this connection drops"},
                "experience_path": {"type": "string", "description": "sqlite file of game records and lessons across games (default: experience.sqlite in the bridge's own folder; \"\" to play without one)"},
                "agent": {"type": "string", "description": "a name for you in the game records (default: the MCP client's name)"},
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
                         "record_debrief, get_experience, get_log_tail and disconnect are accepted."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "timeout_seconds": {"type": "number", "description": "how long to wait this call (default 50)"},
                "treat_timeout_as_decision": {"type": "boolean", "description": "if true, a timeout still returns due=true with reasons [\"timer\"] instead of due=false"},
            },
        },
    },
    {
        "name": "status",
        "description": "A quick look at the game (turn, gold, victory state, queued orders) without it counting as a decision.",
        "inputSchema": {"type": "object", "properties": {}},
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
        "name": "get_log_tail",
        "description": "The game's own recent log output, for debugging a confusing session without a human tailing keeperfx.log by hand.",
        "inputSchema": {
            "type": "object",
            "properties": {"lines": {"type": "integer", "minimum": 1, "maximum": 500, "description": "default 100"}},
        },
    },
    {
        "name": "get_recipes",
        "description": "The active temple sacrifice recipes (listed once you own a temple): which creatures, dropped into the pool together, do what.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "get_experience",
        "description": "Your lessons from earlier games that apply to this one (playbook, campaign, level, opponents) and your record here, in full: shown at the start of a game, and again here on demand.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "record_debrief",
        "description": ("After a game ends (wait_for_decision gave game_over with a debrief): keep what it taught you. The summary "
                         "goes on the game's record; each lessons text you give replaces that set of lessons (omit one to keep it)."),
        "inputSchema": {"type": "object", "properties": prompt.DEBRIEF_PROPERTIES},
    },
    {
        "name": "disconnect",
        "description": "Close the connection to the game. Call this when you are done playing.",
        "inputSchema": {"type": "object", "properties": {}},
    },
]

PROMPTS = [
    {"name": "play_keeperfx", "description": "How to play a keeperfx External seat through this server's tools."},
]


def _text_result(text, is_error=False):
    result = {"content": [{"type": "text", "text": text}]}
    if is_error:
        result["isError"] = True
    return result


def _dispatch_tool(session, name, args):
    args = args or {}
    if name == "connect":
        return session.connect(**args)
    if name == "get_instructions":
        return session.instructions()
    if name == "wait_for_decision":
        return json.dumps(session.wait_for_decision(**args))
    if name == "status":
        return session.status()
    if name == "look":
        return session.look(args["slab_rect"])
    if name == "submit_orders":
        return json.dumps(session.submit(args.get("orders", []), reasoning=args.get("reasoning", ""),
                                          plan=args.get("plan"), notes=args.get("notes")))
    if name == "check_orders":
        return json.dumps(session.check_orders(args.get("orders", [])))
    if name == "set_game_speed":
        return session.set_speed(args["turns_per_second"])
    if name == "get_log_tail":
        return session.log_tail(args.get("lines", 100))
    if name == "get_recipes":
        return session.recipes()
    if name == "get_experience":
        return session.experience_text()
    if name == "record_debrief":
        return json.dumps(session.record_debrief(summary=args.get("summary"), playbook=args.get("playbook"),
                                                 campaign_lessons=args.get("campaign_lessons"),
                                                 level_lessons=args.get("level_lessons"),
                                                 opponent_lessons=args.get("opponent_lessons")))
    if name == "disconnect":
        return session.disconnect()
    raise KeyError(name)


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
                client = ((req.get("params") or {}).get("clientInfo") or {}).get("name")
                if client:
                    self.session.client_name = client
                result = {"protocolVersion": PROTOCOL_VERSION, "capabilities": {"tools": {}, "prompts": {}},
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
                if (req.get("params") or {}).get("name") != "play_keeperfx":
                    return self._error(req_id, -32602, "unknown prompt")
                result = {"messages": [{"role": "user", "content": {"type": "text", "text": prompt.SYSTEM_PROMPT}}]}
            elif method == "tools/call":
                params = req.get("params") or {}
                tool_name = params.get("name")
                try:
                    text = _dispatch_tool(self.session, tool_name, params.get("arguments"))
                    result = _text_result(text)
                except KeyError:
                    result = _text_result("unknown tool: %r" % (tool_name,), is_error=True)
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
            print("keeperfx-bridge: bad JSON on stdin: %s" % (e,), file=sys.stderr)
            sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}}) + "\n")
            sys.stdout.flush()
            continue
        resp = server.handle(req)
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
