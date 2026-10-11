#!/usr/bin/env bash
# ============================================================================
# OCAS: train the LSTM risk model and report every metric.
# Team DSCPP-III-2026-T018 | GEU
#
#   tools/ml/run_ml_eval.sh            prepare data, build, train, evaluate
#   tools/ml/run_ml_eval.sh --no-train evaluate the committed weights only
#
# Data (ESA Kelvins Collision Avoidance Challenge, too large for git). Put
# these in the repo root, from either official source:
#   https://kelvins.esa.int/collision-avoidance-challenge/data/
#       train_data.zip (contains train_data.csv) and test_data.csv
#   https://zenodo.org/records/4463683   (archived copy, 221 MB zip)
#
# Output: printed, and saved to results/ml_metrics_<UTC time>.txt
# Training is deterministic (fixed seed): retraining reproduces the committed
# weights in data/models/ exactly.
# ============================================================================
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

TRAIN=1
[[ "${1:-}" == "--no-train" ]] && TRAIN=0
BUILD_DIR=build
BIN="$BUILD_DIR/bin"
step() { printf '\n\033[1;36m[%s]\033[0m %s\n' "$1" "$2"; }
die() { printf '\033[1;31merror:\033[0m %s\n' "$1" >&2; exit 1; }

# ---- 1. data -------------------------------------------------------------------
step "1/4" "Check data"
if [[ ! -f train_data.csv && -f train_data.zip ]]; then
    command -v unzip >/dev/null || die "unzip not found; extract train_data.zip by hand"
    unzip -o -q train_data.zip train_data.csv && echo "  extracted train_data.csv"
fi
[[ -f test_data.csv ]] || die "test_data.csv missing in repo root (see the download links at the top of this script)"
if [[ $TRAIN == 1 ]]; then
    [[ -f train_data.csv ]] || die "train_data.csv / train_data.zip missing in repo root (see the download links at the top of this script)"
fi
echo "  ok"

# ---- 2. build ------------------------------------------------------------------
step "2/4" "Build program and ML tools"
command -v cmake >/dev/null || die "cmake not found (e.g. 'sudo zypper install cmake ninja' or 'sudo apt install cmake ninja-build')"
GEN="Unix Makefiles"; command -v ninja >/dev/null && GEN="Ninja"
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && ! grep -q "CMAKE_GENERATOR:INTERNAL=$GEN" "$BUILD_DIR/CMakeCache.txt"; then
    rm -rf "$BUILD_DIR"   # generator changed; CMake refuses to switch in place
fi
cmake -S . -B "$BUILD_DIR" -G "$GEN" -DCMAKE_BUILD_TYPE=Release -DOCAS_BUILD_TOOLS=ON >/dev/null
cmake --build "$BUILD_DIR" >/dev/null
echo "  ok"

# Derived data files, created once from the raw ESA download.
if [[ $TRAIN == 1 && ! -f data/train_clean.csv ]]; then
    echo "  creating data/train_clean.csv and data/test_clean.csv"
    "$BIN/clean_esa_dataset" . >/dev/null
fi
if [[ ! -f data/cleaned_cdm_dataset.csv ]]; then
    echo "  creating data/cleaned_cdm_dataset.csv"
    "$BIN/compact_cdm_dataset" test_data.csv data/cleaned_cdm_dataset.csv >/dev/null
fi

step "3/4" "Train and evaluate (LSTM about 6 s)"
mkdir -p results
OUT="results/ml_metrics_$(date -u +%Y%m%dT%H%M%SZ).txt"
{
echo "OCAS ML metrics report | $(date -u +%Y-%m-%dT%H:%M:%SZ) | commit $(git rev-parse --short HEAD 2>/dev/null || echo n/a)"
echo "Label: high risk = final CDM log10 Pc >= -6 (LSTM, per event). Pc is never a model input."
echo "Columns: AP = average precision, ROCAUC = area under the ROC curve,"
echo "         accuracy = (TP+TN)/all, sens = sensitivity = recall = TP/(TP+FN),"
echo "         spec = specificity = TN/(TN+FP), prec = precision = TP/(TP+FP), F1."

# ---- 3. train ------------------------------------------------------------------
if [[ $TRAIN == 1 ]]; then
    printf '\n==================== LSTM (tools/ml/train_lstm) ====================\n'
    "$BIN/train_lstm" .
fi

# ---- 4. evaluate inside the flight program ------------------------------------
printf '\n==================== All models on the official ESA test file (ocas_flight_exec --evaluate) ====================\n'
"$BIN/ocas_flight_exec" --evaluate --no-color
} 2>&1 | tee "$OUT"

step "4/4" "Done"
echo "  report saved to $OUT"
