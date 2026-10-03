#!/usr/bin/env bash
# Runs the agent-vs-agent end-to-end test: a headless daimonkeeper running the ai_bridge_vs_agent ftest (game side,
# two External seats) and scripts/ai_bridge_vs_agent_e2e.py (client side, scripts/llm_bridge/agent_vs_agent.py
# driving both seats over one TCP connection). Neither half passes alone.
#   scripts/run_ftest_ai_bridge_vs_agent.sh [build-dir-with-ftest-daimonkeeper] [data-run-dir]
# Needs an ftest build (-DKFX_FUNCTESTING=ON) and a run tree with the original game data (e.g. out/run-editor).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/out/ft-editor}"
DATA="${2:-$ROOT/out/run-editor}"
PORT="${AI_VS_PORT:-5798}"
BIN="$BUILD/daimonkeeper"
[ -x "$BIN" ] || { echo "no ftest daimonkeeper at $BIN" >&2; exit 2; }
[ -d "$DATA" ] || { echo "no data run dir at $DATA" >&2; exit 2; }

RUN="$(mktemp -d "$ROOT/out/run-ai-vs.XXXXXX")"
trap 'rm -rf "$RUN"' EXIT
for f in "$DATA"/*; do
    case "$(basename "$f")" in keeperfx.cfg|keeperfx.log|keeperfx|daimonkeeper.cfg|daimonkeeper.log|daimonkeeper) ;; *) ln -s "$f" "$RUN/" ;; esac
done
cp "$BIN" "$RUN/daimonkeeper"
CFG_SRC="$DATA/daimonkeeper.cfg"; [ -f "$CFG_SRC" ] || CFG_SRC="$DATA/keeperfx.cfg"   # a KeeperFX data dir has only the latter
sed -e 's/^API_ENABLED=.*/API_ENABLED=TRUE/' -e '/^API_PORT=/d' "$CFG_SRC" > "$RUN/daimonkeeper.cfg"
echo "API_PORT=$PORT" >> "$RUN/daimonkeeper.cfg"

cd "$RUN"
timeout 400 ./daimonkeeper -headless -ftests ai_bridge_vs_agent -includelongtests -exitonfailedtest > "$RUN/game.out" 2>&1 &
GAME=$!
python3 "$ROOT/scripts/ai_bridge_vs_agent_e2e.py" "$PORT"
CLIENT=$?
wait $GAME
GAMERC=$?
echo "client exit=$CLIENT, game exit=$GAMERC"
grep -a "FTest" "$RUN/daimonkeeper.log" | grep -a "passed\|failed\|Failing\|CHECK\|achieved" | tail -12
[ $CLIENT -eq 0 ] && [ $GAMERC -eq 0 ]
