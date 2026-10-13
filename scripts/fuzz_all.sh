#!/usr/bin/env bash
#
# Runs every kfx_fuzz target for a while (security pass S08, fuzz/README.md).
#
#   scripts/fuzz_all.sh [seconds-per-target]  [target ...]
#
# Seeds are made first if the corpus folders are missing (scripts/fuzz_seeds.py). Corpora live in
# out/scratch/fuzz/corpus/<target>, crashes in out/scratch/fuzz/crashes/<target>, a log per target in
# out/scratch/fuzz/<target>.log; afterwards scripts/fuzz_triage.py groups the crashes.
# KFX_FUZZ_JOBS: workers per target (default: all cores minus 4).
set -uo pipefail
cd "$(dirname "$0")/.."
SECS="${1:-240}"
shift || true
BIN=out/fuzz/fuzz/kfx_fuzz
[ -x "$BIN" ] || { echo "build it first: scripts/build-fuzz.sh fuzz" >&2; exit 2; }
JOBS="${KFX_FUZZ_JOBS:-$(( $(nproc) > 8 ? $(nproc) - 4 : 4 ))}"
[ -d out/scratch/fuzz/corpus ] || ./scripts/fuzz_seeds.py
TARGETS=("$@")
[ ${#TARGETS[@]} -gt 0 ] || mapfile -t TARGETS < <("$BIN" list | awk '{print $1}')
export ASAN_OPTIONS=detect_leaks=0
for t in "${TARGETS[@]}"; do
  rm -rf /dev/shm/kfx_fuzz_rt
  mkdir -p "out/scratch/fuzz/corpus/$t"
  echo "== $t ($SECS s, $JOBS workers)"
  timeout $((SECS + 120)) "$BIN" "$t" fuzz "out/scratch/fuzz/corpus/$t" -max_total_time="$SECS" -jobs="$JOBS" -timeout=30 \
      -crashes="out/scratch/fuzz/crashes/$t" > "out/scratch/fuzz/$t.log" 2>&1
  grep -E " done:" "out/scratch/fuzz/$t.log" | head -1 | cut -c1-120
done
./scripts/fuzz_triage.py
