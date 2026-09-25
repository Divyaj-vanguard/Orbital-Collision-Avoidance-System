#!/usr/bin/env bash
# ============================================================================
# OCAS: Autonomous Orbital Collision Avoidance System
# Ground Station Data Fetch Script — Pre-Mission Data Acquisition
# Team ID: DSCPP-III-2026-T018 | Graphic Era University
# ============================================================================
#
# PURPOSE:
#   Downloads live conjunction and debris data from authoritative sources
#   BEFORE mission execution. The FCC (ocas_flight_exec) NEVER touches the
#   network — this is the Fly-by-Wire philosophy: acquire on ground, deploy
#   to the embedded flight computer.
#
# USAGE:
#   ./fetch_data.sh                        # uses stored credentials
#   ./fetch_data.sh --spacetrack-only      # only Space-Track CDMs
#   ./fetch_data.sh --celestrak-only       # only CelesTrak TLEs
#   ./fetch_data.sh --offline              # skip fetch, use cached data
#
# DATA SOURCES:
#   1. Space-Track.org  — US DoD authoritative CDM feed (requires account)
#   2. CelesTrak        — Free TLE + SOCRATES conjunction data (no account)
#
# OUTPUT FILES:
#   data/spacetrack_live.json     — today's Space-Track CDMs
#   data/celestrak_live.json      — CelesTrak TLE debris catalog
#   data/fetch_manifest.txt       — download log with timestamps
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ----------------------------------------------------------------------------
# Colours for terminal output
# ----------------------------------------------------------------------------
RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

echo ""
echo -e "${BOLD}================================================================${RESET}"
echo -e "${BOLD}  OCAS Ground Station — Pre-Mission Data Acquisition${RESET}"
echo -e "${BOLD}  Team ID: DSCPP-III-2026-T018 | Graphic Era University${RESET}"
echo -e "${BOLD}================================================================${RESET}"
echo ""

# ----------------------------------------------------------------------------
# Parse arguments
# ----------------------------------------------------------------------------
SPACETRACK=true
CELESTRAK=true
OFFLINE=false

for arg in "$@"; do
    case "$arg" in
        --spacetrack-only) CELESTRAK=false ;;
        --celestrak-only)  SPACETRACK=false ;;
        --offline)         OFFLINE=true; SPACETRACK=false; CELESTRAK=false ;;
    esac
done

# ----------------------------------------------------------------------------
# Check dependencies
# ----------------------------------------------------------------------------
if [ "$OFFLINE" = false ]; then
    if ! command -v curl &>/dev/null; then
        echo -e "${RED}[ERROR] curl is not installed.${RESET}"
        echo "  Install with: sudo apt install curl"
        exit 1
    fi
    echo -e "${GREEN}[OK]${RESET} curl found: $(curl --version | head -1)"
fi

mkdir -p data logs
MANIFEST="data/fetch_manifest.txt"
echo "OCAS Data Fetch — $(date -u '+%Y-%m-%dT%H:%M:%SZ')" > "$MANIFEST"

# ----------------------------------------------------------------------------
# STEP 1 — Space-Track.org CDM Download
# Documentation: https://www.space-track.org/documentation#api-basicSpaceData
#
# WHAT WE FETCH:
#   class=cdm_public     — public conjunction data messages
#   EMERGENCY_REPORTABLE/Y — only conjunctions flagged as emergency-level
#   format/json          — JSON response
#
# HOW AUTHENTICATION WORKS:
#   Space-Track uses session cookies. We POST credentials once,
#   store the cookie, then GET the data with that cookie.
#   This is the OFFICIAL method — NOT web scraping.
# ----------------------------------------------------------------------------
if [ "$SPACETRACK" = true ]; then
    echo ""
    echo -e "${CYAN}[1/2] Space-Track.org — Authoritative CDM Feed${RESET}"
    echo "      Source: US 18th Space Defense Squadron"
    echo "      Data  : CCSDS CDMs, emergency-reportable conjunctions only"
    echo ""

    # -------------------------------------------------------------------------
    # Load credentials from config file (never hardcode credentials in scripts)
    # -------------------------------------------------------------------------
    CRED_FILE="$HOME/.ocas_credentials"
    if [ ! -f "$CRED_FILE" ]; then
        echo -e "${YELLOW}[SETUP] Space-Track credentials not found.${RESET}"
        echo ""
        echo "  Create the file: $CRED_FILE"
        echo "  with this content (replace with your actual login):"
        echo ""
        echo "    ST_EMAIL=your_email@example.com"
        echo "    ST_PASSWORD=your_password"
        echo ""
        echo "  Then run: chmod 600 $CRED_FILE"
        echo "  Register free at: https://www.space-track.org/auth/createAccount"
        echo ""
        echo -e "${YELLOW}  Skipping Space-Track fetch. Using cached data.${RESET}"
        SPACETRACK=false
    else
        # Load credentials safely from file
        # shellcheck source=/dev/null
        source "$CRED_FILE"

        COOKIE_JAR="/tmp/ocas_st_cookies.txt"
        ST_BASE="https://www.space-track.org"

        echo "  Logging in to Space-Track..."
        LOGIN_STATUS=$(curl -s -o /dev/null -w "%{http_code}" \
            -c "$COOKIE_JAR" -b "$COOKIE_JAR" \
            --data "identity=${ST_EMAIL}&password=${ST_PASSWORD}" \
            "${ST_BASE}/ajaxauth/login")

        if [ "$LOGIN_STATUS" != "200" ]; then
            echo -e "${RED}  [ERROR] Space-Track login failed (HTTP $LOGIN_STATUS).${RESET}"
            echo "          Check credentials in $CRED_FILE"
            SPACETRACK=false
        else
            echo -e "${GREEN}  [OK]${RESET} Authenticated."

            # -----------------------------------------------------------------
            # Fetch 1A: Emergency CDMs (today's active conjunction alerts)
            # -----------------------------------------------------------------
            echo "  Fetching emergency CDMs..."
            CDM_URL="${ST_BASE}/basicspacedata/query/class/cdm_public/EMERGENCY_REPORTABLE/Y/format/json"
            HTTP_CODE=$(curl -s -o data/spacetrack_live.json -w "%{http_code}" \
                -c "$COOKIE_JAR" -b "$COOKIE_JAR" \
                --retry 3 --retry-delay 2 \
                "$CDM_URL")

            if [ "$HTTP_CODE" = "200" ]; then
                COUNT=$(python3 -c "import json; d=json.load(open('data/spacetrack_live.json')); print(len(d))" 2>/dev/null || echo "?")
                echo -e "${GREEN}  [OK]${RESET} Downloaded $COUNT CDM records → data/spacetrack_live.json"
                echo "ST_CDM fetch: $(date -u) | records=$COUNT | status=$HTTP_CODE" >> "$MANIFEST"
            else
                echo -e "${RED}  [ERROR] CDM fetch failed (HTTP $HTTP_CODE).${RESET}"
            fi

            # -----------------------------------------------------------------
            # Fetch 1B: GP (General Perturbations) catalog — tracked objects
            # WHAT: All tracked RSOs (Resident Space Objects) with TLE data
            # WHY:  Needed to propagate orbits and compute miss distance geometry
            # -----------------------------------------------------------------
            echo "  Fetching GP catalog (tracked object TLEs)..."
            GP_URL="${ST_BASE}/basicspacedata/query/class/gp/OBJECT_TYPE/DEBRIS/format/json/orderby/NORAD_CAT_ID%20asc/limit/500"
            HTTP_CODE=$(curl -s -o data/spacetrack_gp.json -w "%{http_code}" \
                -c "$COOKIE_JAR" -b "$COOKIE_JAR" \
                --retry 3 --retry-delay 2 \
                "$GP_URL")

            if [ "$HTTP_CODE" = "200" ]; then
                COUNT=$(python3 -c "import json; d=json.load(open('data/spacetrack_gp.json')); print(len(d))" 2>/dev/null || echo "?")
                echo -e "${GREEN}  [OK]${RESET} Downloaded $COUNT TLE records → data/spacetrack_gp.json"
                echo "ST_GP fetch: $(date -u) | records=$COUNT | status=$HTTP_CODE" >> "$MANIFEST"
            else
                echo -e "${YELLOW}  [WARN] GP catalog fetch returned HTTP $HTTP_CODE. Using cached data.${RESET}"
            fi

            # Clean up session cookie
            rm -f "$COOKIE_JAR"
        fi
    fi
fi

# ----------------------------------------------------------------------------
# STEP 2 — CelesTrak (Free, No Account Required)
#
# WHAT WE FETCH:
#   GP catalog in JSON — all catalogued debris objects with TLE data
#
# WHY CELESTRAK:
#   Free, no rate limits for reasonable use, provides supplemental data
#   when Space-Track access isn't available (e.g., during demo/testing)
#
# API DOCS: https://celestrak.org/SOCRATES/
# ----------------------------------------------------------------------------
if [ "$CELESTRAK" = true ]; then
    echo ""
    echo -e "${CYAN}[2/2] CelesTrak — Free Debris TLE Catalog${RESET}"
    echo "      Source: Dr. T.S. Kelso / CelesTrak"
    echo "      Data  : GP element sets for DEBRIS object class"
    echo ""

    # -------------------------------------------------------------------------
    # Fetch 2A: All debris objects (GP format, JSON)
    # -------------------------------------------------------------------------
    CELESTRAK_URL="https://celestrak.org/SOCRATES/query.php?catalog=special&tca=7&rng=5&fmt=json"
    # Fallback: GP debris catalog
    CELESTRAK_GP_URL="https://celestrak.org/SOCRATES/query.php?CATNR=25544&tca=7&rng=5&fmt=json"

    echo "  Fetching CelesTrak debris catalog..."
    HTTP_CODE=$(curl -s -o data/celestrak_live.json -w "%{http_code}" \
        --retry 3 --retry-delay 2 \
        --max-time 30 \
        "https://celestrak.org/SOCRATES/query.php?catalog=stations&tca=7&rng=5&fmt=json" \
        2>/dev/null || echo "000")

    if [ "$HTTP_CODE" = "200" ] && [ -s data/celestrak_live.json ]; then
        echo -e "${GREEN}  [OK]${RESET} CelesTrak data → data/celestrak_live.json"
        echo "CT fetch: $(date -u) | status=$HTTP_CODE" >> "$MANIFEST"
    else
        # Fallback: use pre-downloaded catalog
        echo -e "${YELLOW}  [WARN] CelesTrak unreachable (HTTP $HTTP_CODE). Using cached catalog.${RESET}"
        if [ -f data/celestrak_debris_catalog.json ]; then
            cp data/celestrak_debris_catalog.json data/celestrak_live.json
            echo -e "${GREEN}  [OK]${RESET} Using cached data/celestrak_debris_catalog.json"
            echo "CT fetch: $(date -u) | CACHED" >> "$MANIFEST"
        fi
    fi
fi

# ----------------------------------------------------------------------------
# STEP 3 — Offline Mode / Cache Status
# ----------------------------------------------------------------------------
if [ "$OFFLINE" = true ]; then
    echo ""
    echo -e "${YELLOW}[OFFLINE MODE] Using cached data files:${RESET}"
    for f in data/spacetrack_live.json data/celestrak_live.json \
              data/celestrak_debris_catalog.json data/www.space-track.org.json; do
        if [ -f "$f" ]; then
            SIZE=$(du -sh "$f" 2>/dev/null | cut -f1)
            AGE=$(stat -c '%y' "$f" 2>/dev/null | cut -d'.' -f1)
            echo "  [CACHED] $f  ($SIZE, last modified: $AGE)"
        fi
    done
fi

# ----------------------------------------------------------------------------
# STEP 4 — Summary
# ----------------------------------------------------------------------------
echo ""
echo -e "${BOLD}================================================================${RESET}"
echo -e "${BOLD}  DATA ACQUISITION COMPLETE${RESET}"
echo -e "${BOLD}================================================================${RESET}"
echo ""
echo "  Available data files:"
for f in data/spacetrack_live.json data/spacetrack_gp.json \
          data/celestrak_live.json data/celestrak_debris_catalog.json \
          data/www.space-track.org.json; do
    if [ -f "$f" ]; then
        SIZE=$(du -sh "$f" 2>/dev/null | cut -f1)
        echo -e "  ${GREEN}✓${RESET} $f  ($SIZE)"
    fi
done

echo ""
echo "  Next step — run the OCAS flight pipeline:"
echo "    ./run_ocas.sh --batch"
echo ""
echo -e "${CYAN}  FBW NOTE: This script runs on the GROUND STATION only.${RESET}"
echo -e "${CYAN}  The FCC (ocas_flight_exec) reads local files — never touches network.${RESET}"
echo ""
