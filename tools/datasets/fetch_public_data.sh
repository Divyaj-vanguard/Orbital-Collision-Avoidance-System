#!/usr/bin/env bash
# ============================================================================
# OCAS ground tool: fetch public conjunction data and convert it to the
# OCAS six-field schema.  Team DSCPP-III-2026-T018 | GEU
#
#   tools/datasets/fetch_public_data.sh [build_dir=build]
#
# Sources (no account needed):
#   CelesTrak SOCRATES  sort-minRange.csv   TLE-based screening, LIVE snapshot
#   CelesTrak SATCAT    satcat.csv          apogee/perigee -> regime split
#   NASA CARA_Analysis_Tools                published test-case CDMs (CCSDS KVN)
#
# SOCRATES changes every few hours, so a fetch is a snapshot that cannot be
# re-downloaded later. Every file's URL, fetch time, size and SHA-256 are
# appended to data/external/fetch_manifest.txt for provenance.
#
# Requires curl, sha256sum, and the converters built with -DOCAS_BUILD_TOOLS=ON.
# ============================================================================
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

BIN="${1:-build}/bin"
for t in convert_socrates convert_ccsds_cdm; do
    [[ -x "$BIN/$t" ]] || { echo "error: $BIN/$t missing; configure with -DOCAS_BUILD_TOOLS=ON and build"; exit 1; }
done

EXT=data/external
MANIFEST=$EXT/fetch_manifest.txt
NOW=$(date -u +%Y-%m-%dT%H:%M:%SZ)
DAY=${NOW%%T*}
mkdir -p "$EXT/celestrak/socrates" "$EXT/celestrak/satcat" "$EXT/nasa_cara" "$EXT/derived"

fetch() {  # fetch <url> <dest>
    curl -s -f -m 120 -o "$2" "$1" || { echo "error: download failed: $1"; exit 1; }
    printf '%s  %s  %s bytes  sha256 %s  %s\n' "$NOW" "$2" "$(stat -c %s "$2")" \
        "$(sha256sum "$2" | cut -d' ' -f1)" "$1" >> "$MANIFEST"
    echo "  fetched $2"
}

echo "[1/3] CelesTrak (snapshot $NOW)"
SOC=$EXT/celestrak/socrates/socrates_minrange_$DAY.csv
SAT=$EXT/celestrak/satcat/satcat_$DAY.csv
fetch https://celestrak.org/SOCRATES/sort-minRange.csv "$SOC"
fetch https://celestrak.org/pub/satcat.csv "$SAT"

echo "[2/3] NASA CARA test-case CDMs"
CARA=https://raw.githubusercontent.com/nasa/CARA_Analysis_Tools/master/DistributedMatlab
for p in ProbabilityOfCollision/Pc3D_Hall_Utils/Output_AlfanoTestCases_Pc3D/A09TestCases_01_12_Distributed.csv \
         ODQualityAssessment/UnitTests/InputFiles/ODQATestCaseCDM.txt \
         Utils/CovarianceDeCorrelationCode/UnitTests/InputFiles/DensityDecorrelationTestCaseCDM01.txt \
         Utils/CovarianceDeCorrelationCode/UnitTests/InputFiles/DensityDecorrelationTestCaseCDM02.txt; do
    fetch "$CARA/$p" "$EXT/nasa_cara/$(basename "$p")"
done

echo "[3/3] Convert to the OCAS schema"
# time_to_tca is measured from the download time: SOCRATES publishes absolute TCAs.
"$BIN/convert_socrates" "$SOC" "$SAT" "$NOW" "$EXT/derived"
"$BIN/convert_ccsds_cdm" "$EXT/derived/nasa_cara_test_cdms.csv" "$EXT"/nasa_cara/*CDM*.txt

echo "done. provenance: $MANIFEST"
