# LLM bridge

Two ways to play a keeperfx External seat, and a shared core underneath them.

## Recommended: MCP

`mcp_server.py` exposes the same loop as MCP tools, so whichever assistant is already open -- Claude Desktop, Claude Code,
or another MCP-capable client, any provider -- makes the decisions. No model API key lives in this bridge.

**Why not call a model API directly from the bridge?** `bridge.py --policy anthropic` does that, with a raw
`ANTHROPIC_API_KEY`, and its spend is metered on that key alone: it is not the interactive product's own usage limits,
so an unattended script with a key can spend past a limit nobody set out to watch, and it only works with one provider.
Putting the loop behind MCP instead means the "decide" step runs inside the assistant session the person already has
open, so the spend goes through whatever governance that session already has, and it works with any MCP-capable client,
not just Anthropic's. (The residual case this does not cover: an MCP *client* that itself holds an unmetered key has the
same problem one layer up -- the fix here is "no key in the bridge", not "no key anywhere it could be misused.")

**Setup**: register it with your client, e.g. for Claude Code:

```bash
claude mcp add keeperfx -- python3 /path/to/scripts/llm_bridge/mcp_server.py
```

or in Claude Desktop's `claude_desktop_config.json`:

```json
"keeperfx": {"command": "python3", "args": ["/path/to/scripts/llm_bridge/mcp_server.py"]}
```

**Playing**: `keeperfx.cfg` needs `API_ENABLED=TRUE`; start a Skirmish game with a slot set to **External agent (API)** on
the Slots & AI page (or the tool's `claim` argument converts a computer keeper in a running game). Then, in a
conversation: `connect`, `get_instructions` (once, near the start), then repeatedly `wait_for_decision` -> decide ->
`submit_orders`, for as long as you want to keep playing, `disconnect` when done. `wait_for_decision` blocks until the
game says a decision is due (each quarter of a pay day, or an event: a fight, an attack, a breach, a room lost, a new
creature kind) or its timeout elapses (`due: false`: call it again, e.g. from a scheduled wake-up in a client that
supports one, so the conversation is not left blocking). `--memory` (the `memory_path` argument) keeps the plan, notes
and recent decisions across a restart.

Tools: `connect`, `get_instructions`, `wait_for_decision`, `status`, `look`, `submit_orders`, `check_orders` (validate a
batch, including cost, without spending it), `set_game_speed` (slow the whole game down to buy more thinking time -- global,
not just your seat), `get_log_tail` (the game's own recent log, for debugging a confusing session), `disconnect`. One
`play_keeperfx` MCP prompt offers the same instructions text for a client that surfaces prompts as slash commands.
Each of your creatures keeps a stable name ("Orc #3") for its whole life; orders that take a creature accept `name` in
place of `thing_id`.
Tests: `test_mcp_server.py` (protocol, tool dispatch, and a fake-game integration test, offline); the real engine is
covered by `scripts/run_ftest_ai_bridge_mcp.sh`.

## Automated / benchmarking: bridge.py

```
python3 scripts/llm_bridge/bridge.py --port 5599 --policy anthropic --log run.jsonl   # needs ANTHROPIC_API_KEY
python3 scripts/llm_bridge/bridge.py --port 5599 --policy scripted                    # no model, deterministic
```

An autonomous script: it runs the whole loop itself with a `policy` object, no assistant needed to drive it turn by turn.
`--policy scripted` (`policies.ScriptedPolicy`) needs no model and is what the automated end-to-end test
(`scripts/ai_bridge_reference_e2e.py`, `scripts/run_ftest_ai_bridge_reference.sh`) uses. `--policy anthropic`
(`anthropic_policy.AnthropicPolicy`) calls the Messages API directly -- see the warning above before using it for
anything but an unattended, metered benchmark run. Design and protocol: `docs/refactor/AI/LLM/` (`02` §4a is the loop,
§4b is decision-due).

1. `keeperfx.cfg`: `API_ENABLED=TRUE` (and `API_PORT` if not 5599).
2. Start a Skirmish game with a slot set to **External agent (API)** on the Slots & AI page, or pass `--claim PLAYER` to convert a
   computer keeper in a running local game.
3. Run the bridge. It never pauses the game. By default (`--beat decision`) it thinks when the game says a decision is due: it subscribes to
   `DECISION_DUE`, which the game pushes at each quarter of a pay day and on major events (a fight with a rival, an attack on the heart or a
   room, a breach, a room lost, a new creature kind), and falls back to a timer (`--max-wait-seconds`). Fixed beats still exist (`quarter`,
   `turns:N`, `seconds:S`). Each time it fetches the view (a diff after the first), asks the policy, and sends the answer as queued,
   expiring orders (`--max-age-turns`). Something that becomes due while the model is thinking is handled right after, with all its reasons.
   `--memory FILE` keeps the model's plan, notes and recent decisions across restarts (`memory.py`).
4. `--log FILE` gets one JSON line per decision: game turn, think time, orders sent/refused, view bytes, and (anthropic policy)
   model calls, input/output tokens and seconds. That is the latency/token budget data.

## Agent-vs-agent harness

The game's TCP API accepts only one connected client at a time (`listen(srv, 1)`; a second connection while one is
active is rejected outright), so two independent `bridge.py` processes cannot play two seats of the same game
concurrently. `agent_vs_agent.py` instead multiplexes N seats over the one connection it holds, servicing whichever
seat's decision the game raises next -- each External seat already gets its own independent `DECISION_DUE` tracking
engine-side, so nothing changed there; only a scheduler was needed:

```bash
python3 scripts/llm_bridge/agent_vs_agent.py --port 5599 --seats 0,1 --policy scripted
python3 scripts/llm_bridge/agent_vs_agent.py --port 5599 --claim 0,1 --policy scripted   # convert two computer keepers first
```

`--memory-dir`/`--log-dir` write one `seat_<player>.json`/`.jsonl` file per seat (same shapes `memory.py`/`bridge.py`
already use). Tests: `scripts/run_ftest_ai_bridge_vs_agent.sh` (two External seats on a real multiplayer map, each
given its own build/dig scene and its own periodic fight event, to prove the scheduler dispatches by the event's
`player` field rather than mixing the seats up).

## Cost/latency report

`cost_report.py` turns one or more `--log`/`--log-dir` decision logs (either entry point's) into a summary: decisions,
orders sent/refused, think time (total/avg/max), and -- when the policy recorded any (`anthropic_policy.py` only) --
token counts. A dollar estimate is shown only when a `--prices` file is given (see `model_prices.example.json`): model
prices change and this tool has no live source for them, so without a price file it reports token counts only, never
a stale-and-wrong cost.

```bash
python3 scripts/llm_bridge/cost_report.py run.jsonl
python3 scripts/llm_bridge/cost_report.py --dir out/agent-logs --prices scripts/llm_bridge/model_prices.example.json --model claude-sonnet-5
```

## Shared core

`api.py` (TCP client to the game, plus MCP-style pushed-event handling: `wait_event`/`drain_events`), `viewstate.py` (full
view + diff patching), `prompt.py` (view -> text, `render_for_agent` used by both entry points, the `submit_orders`/`look`
tool schemas), `orders.py` (turning a decided order into `submit_action`, shared by `bridge.py` and `mcp_session.py`),
`memory.py` (plan, notes, decision log), `mcp_session.py` (the MCP-tools-driven step-by-step session), `policies.py`
(`ScriptedPolicy`), `anthropic_policy.py`, `bridge.py` (the autonomous loop), `agent_vs_agent.py` (the multi-seat
harness), `mcp_server.py` (the MCP transport), `cost_report.py` (decision-log summarizer), `test_bridge.py` /
`test_mcp_server.py` / `test_cost_report.py` (offline tests).
Environment (bridge.py --policy anthropic only): `ANTHROPIC_API_KEY`, `ANTHROPIC_MODEL` (default `claude-sonnet-5`),
`ANTHROPIC_BASE_URL`.

Notes: a second client, or any plain `get_player_view` without `since`, replaces the seat's diff baseline; the affected
entry point then gets one full view (a "resync") and carries on. The `anthropic` policy has been exercised only against a
local mock, not the live API.
