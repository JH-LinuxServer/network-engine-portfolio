#!/usr/bin/env bash
set -euo pipefail

LAST="${1:-5}"

for scen in s1 s2 s3; do
  echo "=== Running ${scen} x ${LAST} ==="
  for i in $(seq 1 "${LAST}"); do
    echo "--- ${scen} run ${i}/${LAST} ---"
    ./scripts/run_bench.sh "${scen}"
  done
done

echo "=== Summary (last ${LAST}) ==="
./scripts/summarize_bench.py --last "${LAST}"
