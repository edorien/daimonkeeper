#!/usr/bin/env bash
# Runs the MCP bridge end-to-end test: the same game-side ai_bridge_reference ftest as
# run_ftest_ai_bridge_reference.sh (it does not care which client connects), but driven by
# scripts/ai_bridge_mcp_e2e.py through scripts/llm_bridge/mcp_server.py's stdio JSON-RPC transport instead of
# bridge.py's direct-API policy -- proving the path scripts/llm_bridge/README.md recommends actually works
# against a real running game, not just the offline tests in scripts/llm_bridge/test_mcp_server.py.
#   scripts/run_ftest_ai_bridge_mcp.sh [build-dir-with-ftest-keeperfx] [data-run-dir]
# Needs an ftest build (-DKFX_FUNCTESTING=ON) and a run tree with the original game data (e.g. out/run-editor).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/out/ft-editor}"
DATA="${2:-$ROOT/out/run-editor}"
PORT="${AI_MCP_PORT:-5796}"
BIN="$BUILD/keeperfx"
[ -x "$BIN" ] || { echo "no ftest keeperfx at $BIN" >&2; exit 2; }
[ -d "$DATA" ] || { echo "no data run dir at $DATA" >&2; exit 2; }

RUN="$(mktemp -d "$ROOT/out/run-ai-mcp.XXXXXX")"
trap 'rm -rf "$RUN"' EXIT
for f in "$DATA"/*; do
    case "$(basename "$f")" in keeperfx.cfg|keeperfx.log|keeperfx) ;; *) ln -s "$f" "$RUN/" ;; esac
done
cp "$BIN" "$RUN/keeperfx"
sed -e 's/^API_ENABLED=.*/API_ENABLED=TRUE/' -e '/^API_PORT=/d' "$DATA/keeperfx.cfg" > "$RUN/keeperfx.cfg"
echo "API_PORT=$PORT" >> "$RUN/keeperfx.cfg"

cd "$RUN"
timeout 400 ./keeperfx -headless -ftests ai_bridge_reference -includelongtests -exitonfailedtest > "$RUN/game.out" 2>&1 &
GAME=$!
python3 "$ROOT/scripts/ai_bridge_mcp_e2e.py" "$PORT"
CLIENT=$?
wait $GAME
GAMERC=$?
echo "client exit=$CLIENT, game exit=$GAMERC"
grep -a "FTest" "$RUN/keeperfx.log" | grep -a "passed\|failed\|Failing\|CHECK\|achieved" | tail -8
[ $CLIENT -eq 0 ] && [ $GAMERC -eq 0 ]
