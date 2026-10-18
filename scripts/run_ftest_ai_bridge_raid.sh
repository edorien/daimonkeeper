#!/usr/bin/env bash
# Runs the raid scenario (docs/plans/llm-bridge-realtime.md, Phase 0): a headless daimonkeeper running the ai_bridge_raid ftest and
# scripts/ai_bridge_raid_e2e.py (bridge.py with the evacuate policy, no model) over the in-game TCP API. Prints the latency
# chain from the raid to the hand acting (RAID_CHAIN) and the imps lost (raid result); RAID_RESULT=FILE keeps the chain as JSON.
# RAID_MODE=triggers plays it with the evacuate_imps trigger and no policy (the session's pump fires it).
#   scripts/run_ftest_ai_bridge_raid.sh [build-dir-with-ftest-daimonkeeper] [data-run-dir]
# Needs an ftest build (-DKFX_FUNCTESTING=ON) and a run tree with the original game data (e.g. out/run-editor).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/out/ft-editor}"
DATA="${2:-$ROOT/out/run-editor}"
PORT="${AI_RAID_PORT:-5798}"
BIN="$BUILD/daimonkeeper"
[ -x "$BIN" ] || { echo "no ftest daimonkeeper at $BIN" >&2; exit 2; }
[ -d "$DATA" ] || { echo "no data run dir at $DATA" >&2; exit 2; }
DATA="$(cd "$DATA" && pwd)"; BIN="$(cd "$(dirname "$BIN")" && pwd)/daimonkeeper"   # the run dir links to them

RUN="$(mktemp -d "$ROOT/out/run-ai-raid.XXXXXX")"
trap 'rm -rf "$RUN"' EXIT
for f in "$DATA"/*; do
    case "$(basename "$f")" in keeperfx.cfg|keeperfx.log|keeperfx|daimonkeeper.cfg|daimonkeeper.log|daimonkeeper) ;; *) ln -s "$f" "$RUN/" ;; esac
done
cp "$BIN" "$RUN/daimonkeeper"
# fxdata files this tree added that the data dir predates (the combat assistant's presets): overlay them.
if [ ! -e "$DATA/fxdata/combat_assist_presets.toml" ] && [ -L "$RUN/fxdata" ]; then
    rm "$RUN/fxdata"; mkdir "$RUN/fxdata"
    for f in "$DATA"/fxdata/*; do ln -s "$f" "$RUN/fxdata/"; done
    ln -s "$ROOT/config/fxdata/combat_assist_presets.toml" "$RUN/fxdata/"
fi
CFG_SRC="$DATA/daimonkeeper.cfg"; [ -f "$CFG_SRC" ] || CFG_SRC="$DATA/keeperfx.cfg"   # a KeeperFX data dir has only the latter
sed -e 's/^API_ENABLED=.*/API_ENABLED=TRUE/' -e '/^API_PORT=/d' "$CFG_SRC" > "$RUN/daimonkeeper.cfg"
echo "API_PORT=$PORT" >> "$RUN/daimonkeeper.cfg"

cd "$RUN"
timeout 400 ./daimonkeeper -headless -ftests ai_bridge_raid -includelongtests -exitonfailedtest > "$RUN/game.out" 2>&1 &
GAME=$!
python3 "$ROOT/scripts/ai_bridge_raid_e2e.py" "$PORT" ${RAID_RESULT:+--result "$RAID_RESULT"} ${RAID_MODE:+--mode "$RAID_MODE"}
CLIENT=$?
wait $GAME
GAMERC=$?
echo "client exit=$CLIENT, game exit=$GAMERC"
[ $CLIENT -eq 0 ] && [ $GAMERC -eq 0 ] || { echo "--- game output (tail)"; tail -20 "$RUN/game.out"; grep -a "Error\|error" "$RUN/daimonkeeper.log" | tail -10; }
grep -a "FTest" "$RUN/daimonkeeper.log" | grep -a "passed\|failed\|Failing\|CHECK\|raid\|view timing" | tail -8
[ $CLIENT -eq 0 ] && [ $GAMERC -eq 0 ]
