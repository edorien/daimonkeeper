#!/usr/bin/env python3
"""Call one tool of a running mcp_http_server.py from the command line: for an agent or person with a shell but no working
MCP connection to it (a client that started before the server, or none at all), and for checking that a server answers.

    python3 scripts/llm_bridge/mcp_http_client.py --list
    python3 scripts/llm_bridge/mcp_http_client.py connect '{"agent": "Claude"}'
    python3 scripts/llm_bridge/mcp_http_client.py --brief --save last.json wait_for_decision
    python3 scripts/llm_bridge/mcp_http_client.py submit_orders - < orders.json

Arguments are one JSON object, given inline or as '-' to read it from stdin (no shell quoting to get wrong: an apostrophe
in a reasoning text ends a single-quoted shell argument). The tool's text goes to stdout as the server sent it.

--brief shortens a decision (wait_for_decision's JSON with a `state`) to what changes from one decision to the next: the
other fields on one JSON line, then the state without the memory echo (plan, notes, recent decisions), the experience text
and the full map a level's first decision carries -- the plan and notes are what you last wrote, and `look` shows any map
window with coordinates. --save FILE keeps the tool's full text regardless.

Exit status: 0 on success, 1 when the tool reports an error (its text still printed), 2 when the server cannot be
reached or answers something that is not MCP. Each run is its own MCP session (initialize, then the call); the game
connection lives in the server, so nothing is lost between runs. Standard library only, like the rest of
scripts/llm_bridge/.
"""
import argparse
import http.client
import json
import os
import sys
from urllib.parse import urlsplit

DEFAULT_URL = "http://127.0.0.1:5600/mcp"
PROTOCOL_VERSION = "2025-11-25"
# The request waits as long as the tool may block (wait_for_decision's timeout_seconds), plus this margin.
TIMEOUT_MARGIN_SECONDS = 30.0
DEFAULT_TIMEOUT_SECONDS = 90.0

# Blocks --brief leaves out. The memory echo and the experience text come before the state proper, which starts at one of
# STATE_START; anything before the memory is a one-off note (e.g. the game was reloaded) and is kept.
MEMORY_START = "YOUR PLAN"
STATE_START = ("DECISION DUE", "TURN ")
FULL_MAP_START = "MAP "          # "MAP 85x85 slabs ..." then one row per y, to the end; "MAP CHANGES:" is kept
MAP_CHANGES = "MAP CHANGES"


class ClientError(Exception):
    """The server could not be reached, or did not answer as an MCP server."""


class Client:
    def __init__(self, url=DEFAULT_URL, token=None, client_name="mcp_http_client"):
        parts = urlsplit(url)
        if parts.scheme != "http" or not parts.hostname:
            raise ClientError("only http:// URLs are supported: %r" % (url,))
        self.host, self.port, self.path = parts.hostname, parts.port or 80, parts.path or "/"
        self.token, self.client_name = token, client_name
        self.protocol_version = None
        self._next_id = 0

    def _post(self, msg, timeout):
        headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.token:
            headers["Authorization"] = "Bearer " + self.token
        if self.protocol_version:
            headers["MCP-Protocol-Version"] = self.protocol_version
        conn = http.client.HTTPConnection(self.host, self.port, timeout=timeout)
        try:
            conn.request("POST", self.path, body=json.dumps(msg), headers=headers)
            resp = conn.getresponse()
            data = resp.read()
        except OSError as e:
            raise ClientError("cannot reach %s:%d%s: %s (is mcp_http_server.py running?)" % (self.host, self.port, self.path, e))
        finally:
            conn.close()
        if resp.status == 202:
            return None
        try:
            body = json.loads(data) if data else None
        except ValueError:
            raise ClientError("HTTP %d, not JSON: %r" % (resp.status, data[:200]))
        if not isinstance(body, dict):
            raise ClientError("HTTP %d, unexpected answer: %r" % (resp.status, body))
        if "error" in body:
            raise ClientError("HTTP %d: %s" % (resp.status, body["error"].get("message")))
        return body.get("result")

    def request(self, method, params=None, timeout=DEFAULT_TIMEOUT_SECONDS):
        self._next_id += 1
        msg = {"jsonrpc": "2.0", "id": self._next_id, "method": method}
        if params is not None:
            msg["params"] = params
        return self._post(msg, timeout)

    def initialize(self):
        result = self.request("initialize", {"protocolVersion": PROTOCOL_VERSION, "capabilities": {},
                                             "clientInfo": {"name": self.client_name, "version": "1"}})
        self.protocol_version = (result or {}).get("protocolVersion") or PROTOCOL_VERSION
        self._post({"jsonrpc": "2.0", "method": "notifications/initialized"}, DEFAULT_TIMEOUT_SECONDS)

    def list_tools(self):
        return (self.request("tools/list") or {}).get("tools", [])

    def call(self, name, arguments=None):
        """(text, is_error) of one tool call."""
        arguments = arguments or {}
        timeout = DEFAULT_TIMEOUT_SECONDS
        blocks_for = arguments.get("timeout_seconds")
        if isinstance(blocks_for, (int, float)):
            timeout = max(timeout, blocks_for + TIMEOUT_MARGIN_SECONDS)
        result = self.request("tools/call", {"name": name, "arguments": arguments}, timeout=timeout) or {}
        text = "\n".join(c.get("text", "") for c in result.get("content", []) if c.get("type") == "text")
        return text, bool(result.get("isError"))


def brief_state(state):
    """A decision's state without the memory echo, the experience text and the full first-decision map."""
    lines = state.split("\n")
    start = next((i for i, l in enumerate(lines) if l.startswith(STATE_START)), None)
    if start is None:
        return state
    memory_at = next((i for i, l in enumerate(lines[:start]) if l.startswith(MEMORY_START)), start)
    kept = lines[:memory_at]
    while kept and not kept[-1].strip():
        kept.pop()
    if kept:
        kept.append("")
    for line in lines[start:]:
        if line.startswith(FULL_MAP_START) and not line.startswith(MAP_CHANGES):
            kept.append("MAP: full map omitted (--brief); use look for a window of it")
            break
        kept.append(line)
    return "\n".join(kept)


def brief(text):
    """--brief for any tool's text: a decision is shortened, anything else is returned as it is."""
    try:
        data = json.loads(text)
    except ValueError:
        return text
    if not isinstance(data, dict) or not isinstance(data.get("state"), str):
        return text
    head = {k: v for k, v in data.items() if k != "state"}
    return json.dumps(head) + "\n" + brief_state(data["state"])


def _read_arguments(raw, stdin):
    if raw is None:
        return {}
    if raw == "-":
        raw = stdin.read()
    try:
        args = json.loads(raw) if raw.strip() else {}
    except ValueError as e:
        raise ClientError("arguments are not JSON: %s" % (e,))
    if not isinstance(args, dict):
        raise ClientError("arguments must be one JSON object, not %s" % (type(args).__name__,))
    return args


def main(argv=None, stdin=None, stdout=None):
    stdin, stdout = stdin or sys.stdin, stdout or sys.stdout
    ap = argparse.ArgumentParser(description="Call one tool of a running mcp_http_server.py.")
    ap.add_argument("tool", nargs="?", help="tool name (see --list)")
    ap.add_argument("arguments", nargs="?", help="one JSON object, or '-' to read it from stdin (default {})")
    ap.add_argument("--url", default=os.environ.get("DAIMONKEEPER_MCP_URL", DEFAULT_URL),
                    help="server endpoint (env DAIMONKEEPER_MCP_URL; default %(default)s)")
    ap.add_argument("--token", default=os.environ.get("DAIMONKEEPER_MCP_TOKEN"),
                    help="bearer token, if the server was started with --token (env DAIMONKEEPER_MCP_TOKEN)")
    ap.add_argument("--list", action="store_true", help="list the server's tools and exit")
    ap.add_argument("--brief", action="store_true", help="shorten a decision's state (see the module docstring)")
    ap.add_argument("--save", metavar="FILE", help="also write the tool's full text to FILE")
    ap.add_argument("--client-name", default="mcp_http_client",
                    help="name sent as the MCP clientInfo (connect's default agent name; default %(default)s)")
    args = ap.parse_args(argv)
    if not args.list and not args.tool:
        ap.error("give a tool name, or --list")
    try:
        client = Client(args.url, token=args.token, client_name=args.client_name)
        tool_args = None if args.list else _read_arguments(args.arguments, stdin)
        client.initialize()
        if args.list:
            for t in client.list_tools():
                stdout.write("%s: %s\n" % (t["name"], t.get("description", "").split(". ")[0]))
            return 0
        text, is_error = client.call(args.tool, tool_args)
    except ClientError as e:
        sys.stderr.write("mcp_http_client: %s\n" % (e,))
        return 2
    if args.save:
        with open(args.save, "w", encoding="utf-8") as f:
            f.write(text)
    if is_error:
        stdout.write("TOOL ERROR: " + text + "\n")
        return 1
    stdout.write((brief(text) if args.brief else text) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
