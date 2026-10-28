#!/usr/bin/env bash
# Build OCAS, run the self-tests, then every scenario.  ./run_ocas.sh --no-build reuses build/.
set -euo pipefail
cd "$(dirname "$0")"
BIN=build/bin
step() { printf '\n\033[1;36m[%s]\033[0m %s\n' "$1" "$2"; }

if [[ "${1:-}" != "--no-build" ]]; then
    step "1/6" "Build"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build >/dev/null
    echo "  ok"
fi

step "2/6" "Self-tests"
"$BIN/test_ocas"

step "3/6" "Model check on real ESA data"
"$BIN/ocas_flight_exec" --evaluate

step "4/6" "LEO satellite, 24,484 real alerts"
"$BIN/ocas_flight_exec" --altitude-km 550

step "5/6" "MEO and GEO satellites (synthetic alerts; GEO with operator aborts)"
"$BIN/ocas_flight_exec" --input data/synthetic/synthetic_meo_vectors.csv --altitude-km 20200 --batch 8 --quiet
echo
"$BIN/ocas_flight_exec" --input data/synthetic/synthetic_geo_vectors.csv --altitude-km 35786 --batch 8 --abort-every 3

step "6/6" "Stress test: alert bursts larger than the input buffer"
"$BIN/ocas_flight_exec" --batch 80 --max-cdms 800 --quiet

printf '\n\033[1;32mAll stages completed.\033[0m Full decision log: logs/flight_audit.log\n'
