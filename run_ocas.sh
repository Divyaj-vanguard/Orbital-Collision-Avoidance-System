#!/usr/bin/env bash
# ============================================================================
# OCAS: Autonomous Orbital Collision Avoidance System
# Build, verify and run every scenario.  Team DSCPP-III-2026-T018 | GEU
#   ./run_ocas.sh              full run
#   ./run_ocas.sh --no-build   reuse an existing build/
# Add --verbose to any ocas_flight_exec call for full technical detail.
# Requires: a C++17 compiler, CMake >= 3.16, and Ninja or Make.
# ============================================================================
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_DIR=build
BIN="$BUILD_DIR/bin"
step() { printf '\n\033[1;36m[%s]\033[0m %s\n' "$1" "$2"; }

if [[ "${1:-}" != "--no-build" ]]; then
    command -v cmake >/dev/null || { echo "error: cmake not found (e.g. 'sudo zypper install cmake ninja' or 'sudo apt install cmake ninja-build')"; exit 1; }
    GEN="Unix Makefiles"
    command -v ninja >/dev/null && GEN="Ninja"
    if ! command -v ninja >/dev/null && ! command -v make >/dev/null; then
        echo "error: neither ninja nor make found"; exit 1
    fi
    step "1/6" "Build"
    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && ! grep -q "CMAKE_GENERATOR:INTERNAL=$GEN" "$BUILD_DIR/CMakeCache.txt"; then
        rm -rf "$BUILD_DIR"   # generator changed; CMake refuses to switch in place
    fi
    mkdir -p "$BUILD_DIR"
    if ! { cmake -S . -B "$BUILD_DIR" -G "$GEN" -DCMAKE_BUILD_TYPE=Release && cmake --build "$BUILD_DIR"; } \
        >"$BUILD_DIR/build.log" 2>&1; then
        tail -30 "$BUILD_DIR/build.log"; echo "error: build failed (full log: $BUILD_DIR/build.log)"; exit 1
    fi
    echo "  ok"
fi

step "2/6" "Self-tests"
if ! out=$("$BIN/test_ocas"); then echo "$out"; exit 1; fi
echo "  ${out##*$'\n'}"

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
