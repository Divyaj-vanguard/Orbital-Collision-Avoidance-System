#!/usr/bin/env bash
# ============================================================================
# OCAS: Autonomous Orbital Collision Avoidance System
# Build, verify and run every scenario.  Team DSCPP-III-2026-T018 | GEU
#   ./run_ocas.sh              full run
#   ./run_ocas.sh --no-build   reuse an existing build/
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
    step "1/6" "Configure + build (-std=c++17 -Wall -Wextra -Werror -pedantic -O3, generator: $GEN)"
    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && ! grep -q "CMAKE_GENERATOR:INTERNAL=$GEN" "$BUILD_DIR/CMakeCache.txt"; then
        rm -rf "$BUILD_DIR"   # generator changed; CMake refuses to switch in place
    fi
    cmake -S . -B "$BUILD_DIR" -G "$GEN" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD_DIR"
fi

step "2/6" "Verification suite"
"$BIN/test_ocas"

step "3/6" "Offline evaluation on ESA Kelvins test vectors (label: Pc > 1e-4)"
"$BIN/ocas_flight_exec" --evaluate

step "4/6" "LEO host (550 km): full ESA stream, 24,484 real CDMs"
"$BIN/ocas_flight_exec" --altitude-km 550 --quiet --summary-every 200

step "5/6" "GEO host (35,786 km) and MEO host (20,200 km): SYNTHETIC vectors, external aborts on GEO"
"$BIN/ocas_flight_exec" --input data/synthetic_geo_vectors.csv --altitude-km 35786 --batch 8 --abort-every 3 --summary-every 0
"$BIN/ocas_flight_exec" --input data/synthetic_meo_vectors.csv --altitude-km 20200 --batch 8 --summary-every 0

step "6/6" "Stress: 80-CDM bursts into the 64-slot ingest ring buffer (overwrite policy)"
"$BIN/ocas_flight_exec" --batch 80 --max-cdms 800 --quiet --summary-every 0

printf '\n\033[1;32mAll stages completed.\033[0m Audit trail: logs/flight_audit.log\n'
