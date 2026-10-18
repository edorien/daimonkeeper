# LLM bridge

Two ways to play a dAImon Keeper External seat, and a shared core underneath them. The engine side is described in
`docs/Architecture/architecture.md` §5a and the wire protocol in `docs/Architecture/agent-api.md`.

## Quick start: real-time play

A model replies in tens of seconds; a fight is settled in a few. So the agent writes its fast reactions ahead, on a calm
decision, and the game and the bridge carry them out. Setup:

1. **The game**: `API_ENABLED=TRUE` in `daimonkeeper.cfg`. A seat: a Skirmish slot set to **External agent (API)**, a
   campaign with **Seat control: LLM**, or `connect(claim: N)` in a running game.
2. **The server**, started before the client's session: `python3 scripts/llm_bridge/mcp_http_server.py`, registered
   with `claude mcp add --transport http daimonkeeper http://127.0.0.1:5600/mcp` (details below).
3. **In the conversation**: `connect` (`agent`, `experience_path`; `on_urgent` defaults to `auto`: the game pauses for
   each urgent decision where the agent may pause anyway), then `get_instructions` once.
4. **On the first, calm decision**, set up the fast layers:
   - `set_rally_points({"safe": [x, y]})`: a subtile on a pocket of your floor imps cannot walk out of (walled in, or
     across water with no bridge). Imps run back to defend an attacked heart, so anywhere they can walk from is not safe.
   - `set_combat_rules` with heals, gold guards and hand rules, for example:

     ```json
     {"enabled": true, "reserve": 1500, "max_spend": 3000, "rules": [
       {"name": "heal", "power": "POWER_HEAL_CREATURE", "level": 2, "when": [["health_pct", "<", 35]]},
       {"name": "evacuate", "hand": "evacuate_diggers", "to": [91, 217]},
       {"name": "lift", "hand": "lift_wounded"},
       {"name": "reinforce", "hand": "reinforce", "count": 4, "min_match": 50},
       {"name": "cta_floor", "power_off": "POWER_CALL_TO_ARMS", "when": [["gold", "<", 1500]]}]}
     ```

     `lift` acts only when `heal` cannot (its cap or the reserve reached, or no gold); `reinforce` sends only fighters
     that are a match for the enemy, and only if they turn the fight.
   - Optionally a second set in a bank (`set_combat_rules(bank: 2, label: "war", ...)`, switched to with
     `use_combat_bank(2)` in one packet), and bridge triggers (`apply_trigger_preset`, `set_triggers`) for reactions the
     rules do not cover.
5. **Then the loop**: `wait_for_decision`, decide, `submit_orders`. A **flash decision** (severity 1 or 0: a fight, an
   intruder, a loss) is short: answer it at once with at most 4 orders (urgent by default) or `[]`. An urgent decision
   that comes in while you are busy shows as `ALERT` on other replies and comes back in `submit_orders`' reply as
   `next_decision`.

What then happens without a reply:

| Layer | Set with | Acts |
|---|---|---|
| hand rules: evacuate_diggers, lift_wounded, reinforce | `set_combat_rules` (`"hand"`) | the seat, every 2 turns, with your hand |
| cast rules and gold guards | `set_combat_rules` (`"power"`, `"power_off"`) | the game, every 2 turns |
| triggers | `set_triggers`, `apply_trigger_preset` | the bridge, 1-3 turns after the game's push |
| the urgent hold | `connect(on_urgent)` | the game waits for your answer, where pausing is fair |

STANDING ORDERS and TRIGGERS in each decision say what these did, and why one held back.

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
claude mcp add daimonkeeper -- python3 /path/to/scripts/llm_bridge/mcp_server.py
```

or in Claude Desktop's `claude_desktop_config.json`:

```json
"daimonkeeper": {"command": "python3", "args": ["/path/to/scripts/llm_bridge/mcp_server.py"]}
```

**Over HTTP instead** (`mcp_http_server.py`): for a client that can no longer, or never could, start a local command
itself -- e.g. a desktop app whose local-extension rules changed. Run the server yourself, next to the game, and give the
client its URL:

```bash
python3 scripts/llm_bridge/mcp_http_server.py                     # listens on http://127.0.0.1:5600/mcp
claude mcp add --transport http daimonkeeper http://127.0.0.1:5600/mcp
```

Same tools and prompt, MCP's Streamable HTTP transport (plain JSON answers, no SSE stream). Loopback only by default,
with the Host/Origin headers checked (no DNS rebinding from a browser page); `--port` moves it, `--token SECRET` (or
`DAIMONKEEPER_MCP_TOKEN`) requires `Authorization: Bearer SECRET` and is required for a non-loopback `--host`. The game
connection belongs to the server process, not the client: a client that restarts finds the seat still connected, and
stopping the server (Ctrl-C) disconnects it. A client gives up on a reply after its own request
timeout (60 s in the reference SDKs), and a decision produced after that goes nowhere -- the agent never sees it (once
seen live: a quarter decision at the end of a long `advance_turns`). So a wait is capped at 50 s
(`DAIMONKEEPER_MAX_WAIT_SECONDS`; a longer `timeout_seconds` returns `due: false` with a note to call again), and a
decision that came after a wait of 30 s or more (`DAIMONKEEPER_RISKY_WAIT_SECONDS`, for a client with a shorter timeout)
is handed out again by the next `wait_for_decision` (`repeated: true`) unless `submit_orders` answered it first.

Start the server **before** the client session: a client connects to its MCP servers when the session starts, and one
started first reports the server as failed (connection refused) until that session is restarted.

**From a shell** (`mcp_http_client.py`): the same tools without an MCP client -- to check a server answers, to play from an
agent that has a shell but no MCP connection to it, or when the connection failed as above.

```bash
python3 scripts/llm_bridge/mcp_http_client.py --list
python3 scripts/llm_bridge/mcp_http_client.py connect '{"agent": "Claude"}'
python3 scripts/llm_bridge/mcp_http_client.py --brief --save last.json wait_for_decision
python3 scripts/llm_bridge/mcp_http_client.py submit_orders - < orders.json   # arguments from stdin: no shell quoting
```

`--brief` prints a decision without the plan/notes/recent-decisions echo, the experience text and the first decision's full
map (`look` shows a window of it); `--save FILE` keeps the full text. Exit status 0 / 1 (tool error) / 2 (no server).
`--url` / `DAIMONKEEPER_MCP_URL` and `--token` / `DAIMONKEEPER_MCP_TOKEN` as for the server.

**Order freshness**: orders are refused `STALE_VIEW` once the game is `max_age_turns` past the decision they answer
(default 1500 turns, about 75 s at normal speed). An agent that takes longer to decide can pass a larger `max_age_turns` to
`connect`, at the cost of acting on an older view, or slow the game with `set_game_speed`.

**What the seat sees of others**: `visible.traps` and `visible.doors` (another keeper's, a hidden trap only once it is
revealed; ENEMY TRAPS SEEN / ENEMY DOORS SEEN), `visible.hero_gates` (HERO GATES), and every trap's `charges` (0: empty
until re-armed with a crate, -1: unlimited), `max_charges` and `rearming`, its own too. Loose gold lists piles on other
keepers' land with whose land it is (`land`), and LOOSE GOLD names the ones the hand cannot reach yet. Sight of Evil
counts as seen while it is open, as on a human's screen (the engine uncovers its area only while a frame is drawn, so the
view is built the same way); `own.active_powers.POWER_SIGHT` gives its `pos` and `until_turn`, and the ground goes dark
again once it closes. Once it stops widening the agent is called at once (reason `sight_open`), and POWER_SIGHT reports
what it uncovered: `slab_rect`, and the other keepers' `hearts`, `rooms` (slabs in sight), `creatures`, `traps`, `doors`,
`loose_gold`, `stored_gold` and `gold_slabs` in it. The bridge renders it as SIGHT OF EVIL (with the map of the area) and
keeps the last report in the game's memory, shown as LAST SIGHT OF EVIL once the sight has closed, until the next cast.

**Holding and attacking**: `move_creature` holds a creature where it is sent (a held creature does not fight; the
game hands it back when attacked, hungry, owed pay, before pay day or after `hold_turns`: SEAT `auto_released`), and
`release_creature` hands it back (`NOT_HELD` if it is not held; a release whose clicks miss is finished by the game,
`auto_released: released`). `attack(thing_id, pos)` walks a creature there held and, on arrival, sets it fighting what is
there as at a Call to Arms banner: an enemy door beside the spot, the enemy heart in sight, an enemy trap beside it, the
enemy room it is in, else the enemies about (`auto_released: attacking`); `NOTHING_TO_ATTACK` when none is there. Call
to Arms itself calls only the creatures within its range (`own.active_powers.POWER_CALL_TO_ARMS.range`, larger when
overcharged); recast, the banner moves and the creatures it called follow it.

**Watching, and stepping in**: a human watching an agent play their own keeper (campaign Seat control: LLM) has pause,
chat, the camera and event boxes, but their clicks do not reach the dungeon -- they would land between the agent's steps.
In an emergency, with cheat mode on, the cheat menu works: its one-shot cheats (all rooms, magic, traps and doors,
reveal the map, everything free, win or lose the level) and its placement modes (make a creature, place terrain...: the
human's clicks act while the keeper is in that mode, and the agent's next order ends it). It can cut across what the
agent is doing; that is the price of an override.

**Large rooms and long bridges**: the engine's build drag is at most 9 slabs a side (it clips the rest without a word), so
`build_room` makes a larger rectangle as several drags from its start corner, each after the last is built -- a bridge of
any length goes out from the land in 9-slab pieces.

**Losses and the pay day report**: the game compares the seat's map turn by turn (`api_seat_decision.c`, `note_losses`)
rather than trusting its own events (a room taken by enemy imps, a smashed room slab, floor claimed or dug through raise
none, or one easily missed): `room_captured`, `room_destroyed` and `ground_lost` wake the agent at once, skipping the
minimum interval, then at most every 60 turns while the losses go on (so do the fight reasons `enemy_fight`,
`heart_attacked`, `room_under_attack` and `breach`: a fight is settled in seconds); the decision's `losses` (LOSSES in the state) say
what, how many slabs, where and by whom. An enemy creature on the seat's ground (not neutral, an ally's, a prisoner
or knocked out) raises `intruder` on the same rhythm, once per creature until it has been gone 1200 turns; `own.intruders`
(INTRUDERS) lists them while they are there. Not losses: a room the seat sold or rebuilt, a slab its own imps were marked to
dig, a bridge sold back to water or lava. On the decision a pay day's checkpoint is taken at, PAYDAY REPORT compares it
with the last one: fighters and fighter score (and three pay days back) against the enemy fighter score in sight,
ground (`own.territory`), room slabs by kind, lair places, creatures against the portal's limit (`own.creature_limit`),
gold -- with warnings for losing ground, falling behind, a full lair and the creature limit.

A keeper an agent plays never gets the level-start heart-zoom intro (about 32 turns with input locked): a seat claimed before the level starts skips it, and a claim during it ends it at once, so opening orders sent right after `connect` take effect. (An order queue also waits out any input lock a player instance sets, e.g. a zoom to a position.)

**Pausing (games no human plays a keeper in)**: a campaign or scenario started with **Seat control: LLM** (or a Skirmish
whose keepers are all External agents and built-in AI) starts **paused**, and so does a save of one when loaded, the pause the agent's (the view's
`agent_only`, `agent_pause`), so it can read the objective, look at the map and send first orders before anything moves;
`connect` also pauses such a game when the seat is claimed in one already running (`start_paused: false` leaves it
running). `set_pause(paused)` resumes or pauses it again whenever a decision needs more thought, and
`set_pause(paused: false, turns: N)` runs N turns and pauses again. Orders sent while paused wait for the game to run.
`wait_for_decision` answers at once while the agent holds the pause (`due: false, paused: true`). In such a game the
engine's 60 s pause watchdog does not fire. With a human playing, the engine refuses an agent's pause and advance
(`HUMAN_PLAYING`; the raw API's `force: true` is for tests and tools) unless the Skirmish page's **LLM pause** box was ticked
(the view's `can_pause` says which), and then `set_game_speed` is the only lever. `bridge.py`
and `agent_vs_agent.py` resume a level that started paused once their first decision is sent.

**Playing**: `daimonkeeper.cfg` needs `API_ENABLED=TRUE`; start a Skirmish game with a slot set to **External agent (API)** on
the Slots & AI page (or the tool's `claim` argument converts a computer keeper in a running game). Then, in a
conversation: `connect`, `get_instructions` (once, near the start), then repeatedly `wait_for_decision` -> decide ->
`submit_orders`, for as long as you want to keep playing, `disconnect` when done. `wait_for_decision` blocks until the
game says a decision is due (each quarter of a pay day, or an event: a fight, an attack, a breach, a room lost, a new
creature kind, open ground appearing behind your walls, your ground or rooms being taken, the event panel's problems --
hungry creatures, a full or missing treasure room, an annoyed creature, no lair space, an unreachable room -- or finished
research) or its timeout elapses (`due: false`: call it again, e.g. from a scheduled wake-up in a client that
supports one, so the conversation is not left blocking). The plan, notes and recent decisions are kept **by the game**
and saved with it (see Memory below), so a reconnect picks up where it left off and a loaded save brings back the plan as
it was. `connect(agent: "Claude")` names your seat; `experience_path` picks the experience store (`""`: none).

Tools: `connect`, `get_instructions`, `wait_for_decision`, `status`, `look`, `submit_orders`, `check_orders` (validate a
batch, including cost, without spending it), `set_game_speed` (slow the whole game down to buy more thinking time -- global,
not just your seat), `set_pause` (pause/resume, or run N turns, in a game no human plays), `get_log_tail` (the game's own recent log, for debugging a confusing session), `get_recipes` (the active temple sacrifice recipes, once you own a temple), `get_experience` (your lessons from earlier games, in full), `record_debrief` (after a game ends: a summary and rewritten lessons), `disconnect`. One
`play_daimonkeeper` MCP prompt (`play_keeperfx` still works) offers the same instructions text for a client that surfaces prompts as slash commands.
Each of your creatures keeps a stable name ("Orc #3") for its whole life; orders that take a creature accept `name` in
place of `thing_id`.

Orders that do more than they seem to are refused unless the order says it means it (each flag is refused off the spot
it is for). A hand drop gives a creature the job of the room it lands in, which the seat predicts with the engine's own
rule (`predict_hand_drop_job`): one of your creatures in your prison is jailed (`WOULD_IMPRISON`; `imprison: true`,
else `NOT_A_PRISON` -- for a skeleton, or to hold an angry creature), one in your torture chamber goes on the rack
(`WOULD_TORTURE`; `torture: true`, else `NOT_A_TORTURE_CHAMBER` -- for a ghost, or to make its kind work harder; a job
the creature keeps its own will in, the dark mistress's kinky torture, needs no flag), and any creature but an imp anywhere on your portal leaves the dungeon (`WOULD_DISMISS`;
`dismiss: true`, else `NOT_A_PORTAL` -- worth doing on purpose at the creature limit, to make room for stronger kinds);
a prisoner outside your prison/torture chamber goes free (`WOULD_RELEASE_PRISONER`; `release: true`) and anything on a
temple pool is sacrificed (`WOULD_SACRIFICE`; `sacrifice: true`, else `NOT_A_POOL`). A sell of library slabs whose
spellbooks have no free shelf in another library destroys them and the spells with them (`WOULD_LOSE_SPELLS`;
`lose_spells: true`). Every order is checked against the map when it is sent, so a drop onto a bridge an order in the
same batch builds is `CANNOT_DROP_HERE` (the engine lays a bridge one slab per turn); each `CANNOT_DROP_HERE` from
`pick_up_and_drop` logs which test failed (`log_drop_refusal`, a SYNCLOG line: revealed, blocking, slab and owner,
heights). The state shows LOOSE GOLD with the id of every pile the hand can reach, split into hand-sized trips, and
RESEARCH as DONE (nothing left) or STALLED (no library, or nobody researching) with the spellbooks on the shelves.

Standing orders for fights (the combat assistant, `docs/newFeatures/llm/`): the game itself carries out rules every
couple of turns, between the agent's decisions -- heal a fighter dropping low, speed fighters in a losing fight, chicken
a strong enemy on your ground, switch Call to Arms or Must Obey off before they empty the treasury. `set_combat_rules`
(the whole list, in priority order, each rule with an optional `name`; or only settings: `enabled`, `reserve`,
`interval`, `min_gap`, `max_spend`, `charge_time`), `check_combat_rules` (the same checks, nothing sent),
`get_combat_rules` (the live rules by name, ready to edit, and what they did), `apply_combat_preset` (`guardian`,
`striker`, `miser` from `fxdata/combat_assist_presets.toml`, with overrides; no name lists them), `get_combat_stats`
(fights, losses and reaction times as the game measured them). The rules' names live in the agent's memory, which the game
saves, and follow their rules: a change goes live only when its queued order runs (a few turns, or once a paused game
runs), so its names wait until then (STANDING ORDERS says the change is queued), are dropped if the order is refused,
and a slot whose rule changes some other way loses its name. `max_spend` caps what the whole set spends in a pay day:
guardian brings 3000, striker 5000, and casting rules sent while nothing caps them get `DEFAULT_MAX_SPEND` (5000; the
reply says so, and `max_spend: 0` means no cap). Each decision's state has a STANDING ORDERS section (spent this pay day
against the cap); `assist_budget` (a rule's budget or the cap), `assist_reserve` and `assist_gold_guard` call the agent
when a rule hits a limit. Code: `standing_orders.py`; tests: `test_standing_orders.py`. `bridge.py
--assist-preset NAME` starts an automated run from a preset.
Tests: `test_mcp_server.py` (protocol, tool dispatch, and a fake-game integration test, offline), `test_mcp_http_server.py`
(the HTTP transport: framing, batches, Host/Origin/token checks, tool calls during an open wait, a fake game over HTTP, offline),
`test_mcp_http_client.py` (the command-line client, against a stub and the fake game, offline); the
real engine is covered by `scripts/run_ftest_ai_bridge_mcp.sh`.

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

1. `daimonkeeper.cfg`: `API_ENABLED=TRUE` (and `API_PORT` if not 5599).
2. Start a Skirmish game with a slot set to **External agent (API)** on the Slots & AI page, or pass `--claim PLAYER` to convert a
   computer keeper in a running local game.
3. Run the bridge. It never pauses the game. By default (`--beat decision`) it thinks when the game says a decision is due: it subscribes to
   `DECISION_DUE`, which the game pushes at each quarter of a pay day and on major events (a fight with a rival, an attack on the heart or a
   room, a breach, a room lost, a new creature kind, open ground behind the seat's walls), and falls back to a timer (`--max-wait-seconds`). Fixed beats still exist (`quarter`,
   `turns:N`, `seconds:S`). Each time it fetches the view (a diff after the first), asks the policy, and sends the answer as queued,
   expiring orders (`--max-age-turns`). Something that becomes due while the model is thinking is handled right after, with all its reasons.
   The model's plan, notes and recent decisions are kept by the game and saved with it (`memory.py`; Memory below);
   `--experience FILE` / `--no-experience` pick the experience store.
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

`--log-dir` writes one `seat_<player>.jsonl` file per seat (the shape `bridge.py` already uses); each seat's memory is
kept by the game, per seat, and every seat's record goes into the one experience store (`--experience` / `--no-experience`). Tests: `scripts/run_ftest_ai_bridge_vs_agent.sh` (two External seats on a real multiplayer map, each
given its own build/dig scene and its own periodic fight event, to prove the scheduler dispatches by the event's
`player` field rather than mixing the seats up).

## Memory and experience

Two stores (docs/refactor/AI/omissions/09-persistent-memory.md):

- **Working memory, this game** (`memory.py`): plan, notes, the last 12 decisions, creature names, pay-day checkpoints.
  The game keeps it, one text per seat (API `set_agent_memory` / `get_agent_memory`), and writes it into save files
  (the optional AGNT chunk). A bridge restart resumes it (`restart`); a load in the running game (`GAME_LOADED`) gives the
  model the plan as it was at that save (`reloaded`); a new level starts empty (`start`). Each game has a `game_id`, each
  attempt at it a `branch` (a load starts a new one).
- **Experience, across games** (`experience.py`): `experience.sqlite`, next to the bridge's own files (the
  game's `mcp/` folder once installed; a reinstall never touches it, and in a source tree it is git-ignored). Every attempt's record (level, opponents, turns,
  counts, refusals by error, checkpoints, result) is written as it goes, needing no model. When a game is won or lost --
  or quit after at least a pay day without a recent save; a quit right after a save is `suspended` and waits for the
  game to be continued -- the model is asked for a **debrief**: a summary plus rewritten lessons for the playbook (any
  level), the campaign, the level and each opponent it can identify (a human by keeper name, a computer by AI type, an
  agent by its seat name). Those lessons are shown in full (`EXPERIENCE`) at the start of the next game they apply to.

`experience_report.py` shows and curates the store: finished games, one game's attempts and checkpoints, lessons, every
version of a set of lessons, rollback, replace from a file, forget.

```bash
python3 scripts/llm_bridge/experience_report.py games
python3 scripts/llm_bridge/experience_report.py lessons general
python3 scripts/llm_bridge/experience_report.py history general
python3 scripts/llm_bridge/experience_report.py rollback general 12
```

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

## Reacting while the model thinks: the pump, alerts and flash decisions

The session's game connection is a `gamelink.GameLink` (docs/plans/llm-bridge-realtime.md, Phases 1 and 2). A pump thread
reads the game's pushes the moment they arrive and refreshes the view every 10 game turns (`DAIMONKEEPER_PUMP_TURNS`; at
once after an urgent push; `DAIMONKEEPER_PUMP=0` turns it off), so nothing piles up in the socket while the model thinks.
Every tool call holds the session's lock, and `wait_for_decision` lets go of it while it waits, so over HTTP (each request
on its own thread) `status`, `look` and the rest answer during an open wait.

Each decision has a **severity**, the game's (`DECISION_DUE` `severity`, and `where`, the subtile of its most severe
located cause; `seat.decision` in the view has both): 0 critical (heart attacked), 1 combat (a fight, an intruder, ground
or a room lost, an alarm), 2 problem (the event panel's, the standing orders' limits, Sight of Evil), 3 routine (quarters,
research, new kinds). `gamelink.reason_severity` mirrors the engine's table (`test_gamelink.py` checks they agree).

- An urgent decision (severity <= 1, not a start or a load) is a **flash decision** (`flash: true`; `prompt.render_flash`):
  the spot and a 9x9 map of it, the enemies and the seat's own creatures within 6 slabs, the imps at risk, the powers and
  their prices, the standing orders, the plan's first line -- about 1.5 KB, answered with at most 4 orders.
  `status(full: true)` gives the whole state. `bridge.py --policy anthropic` gets the same flash text.
- An urgent decision that arrives while the agent is busy with another one is announced on every other tool's reply
  (`ALERT ...`, once each), and `submit_orders` hands it back inline (`next_decision`); a wait before answering it hands
  it out again (`repeated`).
- `submit_orders` says what changed since the view the decision showed (`since_your_view`), and its orders expire counting
  from that view, however fresh the pump keeps the view since.
- In the engine, an urgent reason the last urgent wake-up did not have (an intruder, then the fight it starts) goes out at
  once instead of waiting out the 60-turn gap; the same reason again still waits.

`bridge.py`'s pump only reads the pushes (its policy reads the view on the main thread), and `agent_vs_agent.py` runs each
seat's policy on its own thread, so one seat's think no longer holds the others back.

## Urgent orders, rule banks and triggers

docs/plans/llm-bridge-realtime.md, Phases 3 and 4.

- **Two lanes.** An order with `priority: "now"` goes ahead of every routine order; a long routine build, dig, unmark,
  sell or rule change that is running is cut where it stands (the tool left, the button released) and goes back to the
  front of its lane, to be planned again from the live state once the urgent ones are done (`seat.results` says
  `suspended`). Hand orders, casts and orders to a creature run to their end. An urgent order expires after 200 turns
  unless it says otherwise (`max_age_turns`); the answer to a flash decision is urgent by default. `cancel` takes one
  order (`id`) or every order waiting in a lane (`lane`). The view's `seat.queue` shows what runs and what waits.
- **Rule banks.** `set_combat_rules(bank=1..3, label=...)` stores a whole set without making it live; `use_combat_bank(N)`
  makes it live in one urgent packet (`PckA_AssistUseBank`), rules the same in both keeping their counters. STANDING
  ORDERS lists the banks (`own.assist.banks`, `active_bank`).
- **Triggers** (`triggers.py`): `set_triggers` / `check_triggers` / `get_triggers` / `apply_trigger_preset`
  (`evacuate_imps`, `lift_wounded`, `reinforce_heart`) and `set_rally_points`. A trigger names the urgent reasons it is
  for, optionally a region or a rally point and up to 4 facts, and up to 4 orders with placeholders (`$where`,
  `$rally:NAME`, `$drop_near:...` -- the game's `find_drop_spot` --, `$imps`, `$fighters`, `$wounded`, `$reserve`,
  `$enemy`, `$heart`). The session's pump fires it the moment the game pushes a matching DECISION_DUE, with no tool call
  in flight, its orders urgent; two refusals in a row disarm it. Triggers, their counters, the firing log and the rally
  points are kept in the agent's memory (saved with the game); TRIGGERS in the state shows them and what they did. Only
  the MCP session fires triggers (`bridge.py`'s policies decide for themselves). Imps run back to defend an attacked
  heart, so a safe spot for imps is a pocket of floor they cannot walk out of.

**Hand rules** (Phase 5, `seat_reflexes.c`): combat assistant rules with `"hand"` instead of a power, carried out by
the External seat with the keeper's own hand, as urgent `pick_up_and_drop` orders, one trip at a time, re-checked every
assistant interval. `evacuate_diggers` (`to`: required) lifts the imps within `radius` slabs of a visible enemy fighter to
the safe spot; `lift_wounded` lifts fighters in a fight under 35% health (or the rule's `health_pct` condition) to `to`,
else a lair, only when no heal rule of the assistant could heal them now; `reinforce` drops healthy fighters from away
from a fight the seat is losing or matching (`force_ratio` 80, or the rule's condition), each at least `min_match`% (50)
of the strongest enemy's force, and only enough to turn it -- none at all if everything it may send would not. The view
shows each rule's reason for holding back (`state`: HEAL_READY, NO_MATCH, WOULD_NOT_TURN_IT, NO_DROP_SPOT, NO_LAIR) and
`own.assist.reflexes` what they did. An agent's only: a human's profile drops them, and none run in multiplayer.
`RAID_MODE=reflex` plays the raid with an evacuate_diggers rule.

**Time to think** (Phase 6, `api_urgent_hold.c`): `connect(on_urgent=auto|pause|slow|none, hold_seconds, slow_turns_per_second)`.
With pause (auto's choice where the agent may pause anyway: no human keeper, or the Skirmish page's LLM Pause; never
otherwise), each urgent decision stops the game until the agent answers it (`submit_orders`, which acks the decision:
`ack_decision`, and tags its orders with `decision_seq`), until the agent takes the pause or the speed itself (`set_pause`,
`advance_turns`, `set_game_speed`), or for at most `hold_seconds` (60). An advance the hold interrupted gets its turns
back. slow runs the game at a few turns a second instead. A second urgent decision during a hold keeps it until that one
is answered too; a trigger's orders answer none. The view's `seat.decision.hold` and the text (THE GAME IS PAUSED FOR YOUR
ANSWER) say so. `bridge.py --on-urgent pause|slow` opts in (it never pauses by default).

`RAID_MODE=triggers scripts/run_ftest_ai_bridge_raid.sh ...` plays the raid with the `evacuate_imps` trigger and an agent
that does nothing.

## Latency chain and the raid scenario

`latency.py` follows each decision from cause to the hand (docs/plans/llm-bridge-realtime.md, Phase 0), in game turns:
`hold` (a reason raised -> the game sends `DECISION_DUE`; the push's `raised` says when each reason was raised), `delivery`
(-> the agent has it), `think` (-> the game accepts its first order), `queue` (-> that order starts; `seat.results` carries
each order's `submitted` and `started` turns), `gesture` (-> its last step), `total` (raised -> started), plus the think in
wall-clock seconds. All three entry points keep it through `experience.Recorder`: the game's record gets the medians and
the last 16 chains (`final.latency`), `bridge.py`/`agent_vs_agent.py` logs carry each decision's finished chains
(`latency`, and a closing `{"latency": [...]}` line), and the MCP `get_combat_stats` tool adds one line of medians.
`combat_report.py --store ...` / `--decisions LOG` prints them as a second table.

`scripts/run_ftest_ai_bridge_raid.sh [ftest-build] [data]` is the model-free yardstick: two hero knights raid a seat's imps,
and `bridge.py --policy evacuate` (`policies.EvacuatePolicy`: lift the imps near an intruder to the heart) answers. It prints
`RAID_CHAIN` (raid -> told, -> the hand acting, -> done), the imps lost and what building a view costs the game
(`RAID_RESULT=FILE` keeps the chain as JSON). Each phase of the real-time plan should move these numbers.

NEW EVENTS and MAP CHANGES cover everything since the last decision's text (`ViewState.last_diff` is the news, every diff
since merged; `raw_diff` the latest reply), so a `look` or `status` in between, or the view taken while waiting, no longer
hides what happened while the model was thinking.

## Shared core

`api.py` (TCP client to the game, plus MCP-style pushed-event handling: `wait_event`/`drain_events`), `viewstate.py` (full
view + diff patching), `prompt.py` (view -> text, `render_for_agent` used by both entry points, the `submit_orders`/`look`
tool schemas), `orders.py` (turning a decided order into `submit_action`, shared by `bridge.py` and `mcp_session.py`),
`memory.py` (plan, notes, decision log; kept in the game), `experience.py` (game records and lessons across games),
`latency.py` (each decision's latency chain), `gamelink.py` (the pump, severity), `triggers.py` (contingency triggers),
`mcp_session.py` (the MCP-tools-driven step-by-step session), `policies.py`
(`ScriptedPolicy`, `EvacuatePolicy`), `anthropic_policy.py`, `bridge.py` (the autonomous loop), `agent_vs_agent.py` (the multi-seat
harness), `mcp_server.py` (the MCP transport, stdio), `mcp_http_server.py` (the same over HTTP), `mcp_http_client.py` (its tools from a shell), `cost_report.py` (decision-log summarizer), `experience_report.py`
(experience store viewer), `test_bridge.py` / `test_mcp_server.py` / `test_mcp_http_server.py` / `test_mcp_http_client.py` / `test_experience.py` / `test_cost_report.py` / `test_latency.py` / `test_gamelink.py` / `test_triggers.py`
(offline tests).
Environment (bridge.py --policy anthropic only): `ANTHROPIC_API_KEY`, `ANTHROPIC_MODEL` (default `claude-sonnet-5`),
`ANTHROPIC_BASE_URL`.

Notes: a second client, or any plain `get_player_view` without `since`, replaces the seat's diff baseline; the affected
entry point then gets one full view (a "resync") and carries on. The `anthropic` policy has been exercised only against a
local mock, not the live API.
