# Reference bridge

Plays one External seat of a running keeperfx, in real time, with a decision policy. Standard library only.
Design and protocol: `docs/refactor/AI/LLM/` (02 §4a is the loop).

```
python3 scripts/llm_bridge/bridge.py --port 5599 --policy anthropic --log run.jsonl     # needs ANTHROPIC_API_KEY
python3 scripts/llm_bridge/bridge.py --port 5599 --policy scripted                      # no model
```

1. `keeperfx.cfg`: `API_ENABLED=TRUE` (and `API_PORT` if not 5599).
2. Start a Skirmish game with a slot set to **External agent (API)** on the Slots & AI page, or pass `--claim PLAYER` to convert a
   computer keeper in a running local game.
3. Run the bridge. It never pauses the game. Every beat (`--beat quarter` = each quarter of a pay day, `turns:N`, `seconds:S`) it
   fetches the view (a diff after the first), asks the policy, and sends the answer as queued, expiring orders
   (`--max-age-turns`). A decision that takes longer than a beat does not pile up: the next one starts right after.
4. `--log FILE` gets one JSON line per decision: game turn, think time, orders sent/refused, view bytes, and (anthropic policy)
   model calls, input/output tokens and seconds. That is the latency/token budget data.

Files: `api.py` (TCP client), `viewstate.py` (full view + diff patching), `prompt.py` (view -> text, the `submit_orders` and
`look` tools), `policies.py` (`ScriptedPolicy`), `anthropic_policy.py`, `bridge.py`, `test_bridge.py` (offline tests).
Environment: `ANTHROPIC_API_KEY`, `ANTHROPIC_MODEL` (default `claude-sonnet-5`), `ANTHROPIC_BASE_URL`.

Notes: a second client, or any plain `get_player_view` without `since`, replaces the seat's diff baseline; the bridge then gets one
full view (a "resync") and carries on. The Anthropic policy has been exercised only against a local mock, not the live API.
