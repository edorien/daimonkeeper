#!/usr/bin/env bash
#
# Compares the frame channel of two ftest runs (refactor pass 5, S10): the "FRAMEHASH <turn> <hash>" lines a game
# started with -framehash logs for every world frame it draws, test log by test log.
#
# Usage:
#   scripts/compare_frame_hashes.sh <log-dir-a> <log-dir-b>
#
# The log directories are run_ftest_coverage.sh's <build-dir>/ftest-coverage-logs, from two runs with
# FTEST_ARGS=-framehash: the same binary twice (is the drawing deterministic?) or before and after a renderer change
# (does it draw the same?). Prints a line per log that differs and a summary; exits 1 if any differs. Each half
# of the multiplayer pair draws when the other's packets arrive, so their frames can differ run to run.

set -uo pipefail
export LC_ALL=C

if [ $# -ne 2 ] || [ ! -d "$1" ] || [ ! -d "$2" ]; then
    echo "usage: $0 <log-dir-a> <log-dir-b>" >&2
    exit 2
fi

frames() { grep -ao 'FRAMEHASH [0-9]* [0-9a-f]*' "$1" 2> /dev/null | sort -u; }

same=0 total=0 differ=0 count=0
for log in "$1"/ftcov_*.log; do
    name=$(basename "$log")
    total=$((total + 1))
    a=$(frames "$log")
    b=$(frames "$2/$name")
    count=$((count + $(grep -c . <<< "$a")))
    if [ "$a" = "$b" ]; then
        same=$((same + 1))
    else
        differ=$((differ + 1))
        first=$(comm -3 <(echo "$a") <(echo "$b") | awk '{print $2}' | sort -n | head -n 1)
        echo "DIFF $name: $(grep -c . <<< "$a") vs $(grep -c . <<< "$b") frames, $(comm -23 <(echo "$a") <(echo "$b") | grep -c .) of the first not in the second, from turn ${first:-?}"
    fi
done
echo "$same of $total logs draw the same frames ($count frames)"
[ "$differ" -eq 0 ]
