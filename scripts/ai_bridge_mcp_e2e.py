#!/usr/bin/env python3
"""Client half of the MCP bridge end-to-end test: drives scripts/llm_bridge/mcp_server.py as a real MCP client would
(JSON-RPC over its stdin/stdout), against a real running keeperfx, and checks that orders sent through it take effect in
the game. Companion to scripts/ai_bridge_reference_e2e.py, which proves the same engine plumbing through bridge.py's
direct-API policy; this proves it through the MCP path that scripts/llm_bridge/mcp_server.py's docstring recommends
instead. Run through scripts/run_ftest_ai_bridge_mcp.sh, not by hand.
Usage: ai_bridge_mcp_e2e.py [port]"""
import json
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "llm_bridge"))

from api import Api  # noqa: E402
from policies import _find_block  # noqa: E402

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5599
failures = []


def check(what, cond, detail=""):
    print(("ok   - " if cond else "FAIL - ") + what + ("" if cond else " " + str(detail)))
    if not cond:
        failures.append(what)


class McpClient:
    """A JSON-RPC client for mcp_server.py's stdio transport, minimal enough to drive it in a test."""

    def __init__(self, server_path):
        self.proc = subprocess.Popen([sys.executable, server_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True, bufsize=1)
        self._id = 0

    def notify(self, method, params=None):
        self.proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}}) + "\n")
        self.proc.stdin.flush()

    def request(self, method, params=None):
        self._id += 1
        self.proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self._id, "method": method, "params": params or {}}) + "\n")
        self.proc.stdin.flush()
        line = self.proc.stdout.readline()
        if not line:
            raise ConnectionError("mcp_server.py produced no output (stderr: %s)" % self.proc.stderr.read())
        resp = json.loads(line)
        if resp.get("id") != self._id:
            raise ConnectionError("response id mismatch: %r" % (resp,))
        return resp

    def call_tool(self, name, arguments=None):
        """Calls a tool; returns its text content, raising on a protocol-level error or a tool-level isError."""
        resp = self.request("tools/call", {"name": name, "arguments": arguments or {}})
        if "error" in resp:
            raise ConnectionError("JSON-RPC error calling %s: %r" % (name, resp["error"]))
        result = resp["result"]
        text = result["content"][0]["text"]
        if result.get("isError"):
            raise RuntimeError("%s: %s" % (name, text))
        return text

    def close(self):
        try:
            self.proc.stdin.close()
            self.proc.terminate()
            self.proc.wait(timeout=5)
        except Exception:  # noqa: BLE001
            self.proc.kill()
            self.proc.wait(timeout=5)
        finally:
            self.proc.stdout.close()
            self.proc.stderr.close()


def wait_for_flag1(api, value, timeout=120):
    deadline = time.time() + timeout
    while time.time() < deadline:
        r = api.call(action="read_var", var="FLAG1", player=0)
        if r.get("success") and r.get("data") == value:
            return
        time.sleep(0.2)
    raise SystemExit("the game never reached FLAG1=%d" % value)


def main():
    # Probe 1: wait for the game side to say the scene is ready, then let go of the connection -- the game answers only
    # one client at a time, and the MCP server (via mcp_session.Session) is going to be the "real" one.
    probe = Api("127.0.0.1", PORT)
    probe.connect(90)
    wait_for_flag1(probe, 1)
    view = probe.data(action="get_player_view", player=probe.data(action="get_seats")["seats"][0]["player"])
    seat_player = view["seat"]["player"]
    heart = next(r for r in view["own"]["rooms"] if r["kind"] == "DUNGEON_HEART")
    hx, hy = heart["pos"]
    wounded = next(c for c in view["own"]["creatures"] if c["health"] < 30)
    probe.close()
    time.sleep(1.0)  # let the game notice the close before the MCP server's own connection arrives

    # Found the same way ScriptedPolicy would, not by recomputing the game side's own exact rectangle: a 3x3 block of
    # owned claimed floor to build on, and one of earth to dig, near the heart -- robust to the room's centre subtile
    # not landing on exactly the same slab as the heart thing's own position.
    near = (hx // 3, hy // 3)
    room_rect = _find_block(view, "PRETTY_PATH", seat_player, 3, 3, near)
    dig_rect = _find_block(view, "DIRT", None, 3, 3, near)
    if room_rect is None or dig_rect is None:
        raise SystemExit("could not find a claimed-floor or earth patch near the heart (room_rect=%r dig_rect=%r)" % (room_rect, dig_rect))

    server_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "llm_bridge", "mcp_server.py")
    mem_path = tempfile.mktemp(suffix=".mcp_memory.json")
    client = McpClient(server_path)
    decisions = []
    try:
        init = client.request("initialize", {"protocolVersion": "2024-11-05"})
        check("initialize identifies the server", init["result"]["serverInfo"]["name"] == "keeperfx-bridge", init)
        client.notify("notifications/initialized")

        instructions = client.call_tool("get_instructions")
        check("get_instructions returns the system prompt", "Dungeon Keeper" in instructions, instructions[:80])

        connected = client.call_tool("connect", {"host": "127.0.0.1", "port": PORT, "memory_path": mem_path, "min_interval_turns": 40})
        check("connect reports the seat", ("player %d" % seat_player) in connected, connected)

        for i in range(4):
            due = json.loads(client.call_tool("wait_for_decision", {"timeout_seconds": 120}))
            check("decision %d arrived" % (i + 1), due.get("due") is True, due)
            decisions.append(due)
            if i == 0:
                check("the first decision was because the session started", due["reasons"] == ["start"], due["reasons"])
                orders = [
                    {"verb": "cast_power", "power": "POWER_HEAL_CREATURE", "thing_id": wounded["id"], "overcharge_turns": 8},
                    {"verb": "build_room", "kind": "TREASURE", "slab_rect": room_rect},
                    {"verb": "mark_dig", "slab_rect": dig_rect},
                ]
                reasoning, plan, notes = "heal, build, dig", "grow the dungeon", "sent via MCP end-to-end test"
            else:
                check("a later decision was raised by the game (an enemy_fight event), not a timer",
                      "enemy_fight" in due["reasons"], due["reasons"])
                orders, reasoning, plan, notes = [], "nothing more to do", None, None
            result = json.loads(client.call_tool("submit_orders", {"orders": orders, "reasoning": reasoning, "plan": plan, "notes": notes}))
            check("decision %d's orders were all accepted" % (i + 1), not result["refused"], result)

        status = client.call_tool("status")
        check("status is readable after several decisions", "turn" in status and "gold" in status, status)

        client.call_tool("disconnect")
    finally:
        client.close()

    if os.path.exists(mem_path):
        mem = json.load(open(mem_path))
        check("the memory file recorded the plan and four decisions", mem.get("plan") == "grow the dungeon" and len(mem.get("decisions", [])) == 4, mem)
        os.unlink(mem_path)
    else:
        check("the memory file was written", False, "not found: %s" % mem_path)

    # Signal the bridge phase is over (the game side stops treating a pause as a bug from here) and report the verdict.
    # The game answers one client at a time and needs a moment to notice the MCP subprocess's connection closed.
    time.sleep(1.0)
    report = Api("127.0.0.1", PORT)
    report.connect(30)
    report.call(action="set_var", var="FLAG1", value=2, player=0)
    report.call(action="set_var", var="FLAG0", value=2 if failures else 1, player=0)
    report.close()
    print("\n%d check(s) failed" % len(failures) if failures else "\nall checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:  # noqa: BLE001 - a smoke/e2e test reports any error as failure
        print("FAIL - unexpected error: %r" % (e,))
        import traceback
        traceback.print_exc()
        try:
            a = Api("127.0.0.1", PORT)
            a.connect(5)
            a.call(action="set_var", var="FLAG1", value=2, player=0)
            a.call(action="set_var", var="FLAG0", value=2, player=0)
        except Exception:  # noqa: BLE001
            pass
        sys.exit(1)
