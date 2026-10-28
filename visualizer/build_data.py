#!/usr/bin/env python3
"""Collect real OCAS data into visualizer/data.js for the dashboard.

Sources (all real, nothing simulated):
  1. The OCAS engine run on the ESA Kelvins test set (24,484 real CDMs):
     build/bin/ocas_flight_exec --quiet, read back from its audit log.
  2. The engine's model check (--evaluate) on the same data.
  3. CelesTrak SOCRATES: real predicted close approaches between catalogued
     objects (data/external/celestrak/socrates/*.csv).
  4. CelesTrak GP orbital elements for the objects in the closest approaches,
     downloaded once and cached in visualizer/data/gp_cache.json, propagated
     with SGP4 (the same model CelesTrak uses) around each closest approach.

Needs the sgp4 package (in .venv_lstm). Run from the repo root:
    .venv_lstm/bin/python visualizer/build_data.py            # uses cached orbits
    .venv_lstm/bin/python visualizer/build_data.py --refresh  # download orbits again
"""
import csv
import glob
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
from datetime import datetime, timedelta, timezone

try:
    from sgp4 import omm
    from sgp4.api import Satrec, jday
except ImportError:
    sys.exit("error: sgp4 missing. Use .venv_lstm/bin/python, or: pip install sgp4")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIS = os.path.join(ROOT, "visualizer")
DATA_DIR = os.path.join(VIS, "data")
ENGINE = os.path.join(ROOT, "build", "bin", "ocas_flight_exec")
RUN_LOG = os.path.join(DATA_DIR, "engine_run.log")
GP_CACHE = os.path.join(DATA_DIR, "gp_cache.json")
GP_URL = "https://celestrak.org/NORAD/elements/gp.php?CATNR={}&FORMAT=json"
TOP_CONJUNCTIONS = 12
WINDOW_MIN, STEP_MIN = 30, 1 / 12  # track: closest approach +/- 30 min, every 5 s


def run_engine():
    """Run OCAS on the real ESA alerts and return the parsed audit log."""
    if not os.path.exists(ENGINE):
        sys.exit("error: build/bin/ocas_flight_exec not found; build first (./run_ocas.sh)")
    if os.path.exists(RUN_LOG):
        os.remove(RUN_LOG)  # the logger appends; start clean
    subprocess.run([ENGINE, "--quiet", "--no-color", "--log", RUN_LOG], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    events = [json.loads(line) for line in open(RUN_LOG)]
    start = next(e for e in events if e["event"] == "SESSION_START")
    end = next(e for e in events if e["event"] == "SESSION_END")

    fuel = []  # cumulative committed dv per cycle
    used = 0.0
    for e in events:
        if e["event"] == "ADVISORY_RESOLVED" and e["status"] == "COMMITTED":
            used += e["dv_mps"]
            fuel.append([e["cycle"], round(used, 4)])

    final_status = {}
    for e in events:
        if e["event"] in ("ADVISORY_ISSUED", "ADVISORY_RESOLVED"):
            final_status[e["alert_id"]] = e
    manoeuvres = [{
        "alert": e["alert_id"], "cycle": e["cycle"], "status": e["status"], "urgency": e["urgency"],
        "plane": e["plane"], "dv": e["dv_mps"], "burn_h": round(e["execute_at_s"] / 3600, 1),
        "sep_km": round(e["predicted_sep_m"] / 1000, 1), "score": e["score"],
    } for e in final_status.values()]

    scores = [e["score"] for e in events if e["event"] == "THREAT_SCORED"]
    scored = sum(1 for e in events if e["event"] == "THREAT_SCORED")
    return {
        "regime": start["regime"], "altitude_km": start["altitude_km"], "budget": start["dv_budget_mps"],
        "model": start["model"], "input": start["input"], "cycles": end["cycles"], "alerts": end["ingested"],
        "dangerous": end["actionable"], "planned": end["directives"], "committed": end["committed"],
        "rolled_back": end["rolled_back"], "too_large": end["rejected_dv_cap"], "no_fuel": end["rejected_budget"],
        "fuel_left": end["dv_budget_remaining_mps"], "fuel": fuel, "manoeuvres": manoeuvres,
        "dangerous_scores": scores, "dangerous_logged": scored,
    }


def run_model_check():
    out = subprocess.run([ENGINE, "--evaluate", "--no-color"], cwd=ROOT, check=True, capture_output=True,
                         text=True).stdout
    rows = []
    for line in out.splitlines():
        m = re.match(r"\s{2}(\S.*?)\s{2,}([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s*$", line)
        if m:
            rows.append({"name": m.group(1), "accuracy": float(m.group(2)), "sensitivity": float(m.group(3)),
                         "specificity": float(m.group(4)), "precision": float(m.group(5)),
                         "f1": float(m.group(6))})
    head = re.search(r"([\d,]+) real ESA alerts, ([\d,]+) truly dangerous", out)
    return {"alerts": head.group(1), "positives": head.group(2), "rows": rows} if head else {"rows": rows}


def read_socrates():
    files = sorted(glob.glob(os.path.join(ROOT, "data/external/celestrak/socrates/*.csv")))
    if not files:
        sys.exit("error: no SOCRATES file; run tools/fetch_public_data.sh first")
    path = files[-1]
    rows = [r for r in csv.DictReader(open(path)) if float(r["TCA_RELATIVE_SPEED"]) > 0.5]
    # Pairs with ~0 relative speed are docked or formation-flying objects, not collisions.
    total = len(rows)
    under_1km = sum(1 for r in rows if float(r["TCA_RANGE"]) < 1.0)
    under_100m = sum(1 for r in rows if float(r["TCA_RANGE"]) < 0.1)
    rows.sort(key=lambda r: float(r["TCA_RANGE"]))
    top = [{
        "id1": int(r["NORAD_CAT_ID_1"]), "name1": r["OBJECT_NAME_1"],
        "id2": int(r["NORAD_CAT_ID_2"]), "name2": r["OBJECT_NAME_2"],
        "tca": r["TCA"].replace(" ", "T") + "Z", "range_m": round(float(r["TCA_RANGE"]) * 1000),
        "speed_kms": float(r["TCA_RELATIVE_SPEED"]), "max_prob": float(r["MAX_PROB"]),
    } for r in rows[:TOP_CONJUNCTIONS]]
    return {"file": os.path.basename(path), "pairs": total, "under_1km": under_1km, "under_100m": under_100m,
            "top": top}


def fetch_orbits(ids, refresh):
    cache = json.load(open(GP_CACHE)) if os.path.exists(GP_CACHE) and not refresh else {}
    for norad in ids:
        key = str(norad)
        if key in cache:
            continue
        try:
            with urllib.request.urlopen(GP_URL.format(norad), timeout=20) as resp:
                body = resp.read().decode()
            data = json.loads(body) if body.strip().startswith("[") else []
            cache[key] = data[0] if data else None
            print(f"  orbit {norad}: {'ok' if data else 'not available'}")
        except Exception as exc:  # offline or CelesTrak busy: keep going without this orbit
            print(f"  orbit {norad}: download failed ({exc})")
            cache[key] = None
        time.sleep(0.5)  # be polite to CelesTrak
    json.dump(cache, open(GP_CACHE, "w"), indent=1)
    return {int(k): v for k, v in cache.items() if int(k) in ids}


def satrec(gp):
    sat = Satrec()
    omm.initialize(sat, {k: str(v) for k, v in gp.items()})
    return sat


def eci_km(sat, when):
    """SGP4 position (TEME frame, km) at a UTC datetime, or None on error."""
    jd, fr = jday(when.year, when.month, when.day, when.hour, when.minute,
                  when.second + when.microsecond / 1e6)
    err, r, _ = sat.sgp4(jd, fr)
    return [round(c, 2) for c in r] if err == 0 else None


def add_tracks(top, gps):
    """Positions of both objects every STEP_MIN around closest approach, plus one orbit path each."""
    for p in top:
        g1, g2 = gps.get(p["id1"]), gps.get(p["id2"])
        if not g1 or not g2:
            p["track"] = None
            continue
        s1, s2 = satrec(g1), satrec(g2)
        tca = datetime.fromisoformat(p["tca"].replace("Z", "+00:00")).astimezone(timezone.utc)
        steps = int(2 * WINDOW_MIN / STEP_MIN) + 1
        times = [tca + timedelta(minutes=-WINDOW_MIN + k * STEP_MIN) for k in range(steps)]
        a = [eci_km(s1, t) for t in times]
        b = [eci_km(s2, t) for t in times]
        if None in a or None in b:
            p["track"] = None
            continue
        # Closest approach with today's elements: fine search +/- 2 min at 0.1 s
        best = (float("inf"), 0.0)
        for k in range(-1200, 1201):
            t = tca + timedelta(seconds=k * 0.1)
            x, y = eci_km(s1, t), eci_km(s2, t)
            if x and y:
                d = ((x[0] - y[0]) ** 2 + (x[1] - y[1]) ** 2 + (x[2] - y[2]) ** 2) ** 0.5
                best = min(best, (d, k * 0.1))

        def path(sat, gp):
            period_min = 1440.0 / float(gp["MEAN_MOTION"])
            return [eci_km(sat, tca + timedelta(minutes=period_min * k / 180)) for k in range(181)]

        def orbit_info(gp):
            n = float(gp["MEAN_MOTION"]) * 2 * 3.141592653589793 / 86400
            a_km = (398600.4418 / n ** 2) ** (1 / 3)
            return {"alt_km": round(a_km - 6378.137), "incl": float(gp["INCLINATION"]), "epoch": gp["EPOCH"]}

        p["track"] = {"step_min": STEP_MIN, "window_min": WINDOW_MIN, "a": a, "b": b,
                      "path_a": path(s1, g1), "path_b": path(s2, g2),
                      "min_sep_km": round(best[0], 3), "min_sep_at_s": round(best[1], 1)}
        p["orbit1"], p["orbit2"] = orbit_info(g1), orbit_info(g2)


def main():
    os.makedirs(DATA_DIR, exist_ok=True)
    print("1/4 running OCAS on the real ESA alerts ...")
    engine = run_engine()
    print("2/4 model check ...")
    check = run_model_check()
    print("3/4 reading CelesTrak SOCRATES close approaches ...")
    soc = read_socrates()
    print("4/4 orbital elements for the closest pairs (CelesTrak GP) ...")
    ids = sorted({p["id1"] for p in soc["top"]} | {p["id2"] for p in soc["top"]})
    gps = fetch_orbits(ids, "--refresh" in sys.argv)
    add_tracks(soc["top"], gps)
    corr_path = os.path.join(ROOT, "results", "feature_correlation.json")
    corr = json.load(open(corr_path)) if os.path.exists(corr_path) else None
    if corr is None:
        print("  (no results/feature_correlation.json: run ml/select_features_lstm.py for the heatmap)")
    out = {"generated": time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime()), "engine": engine,
           "model_check": check, "socrates": soc, "correlation": corr}
    with open(os.path.join(VIS, "data.js"), "w") as f:
        f.write("// Generated by visualizer/build_data.py. Do not edit.\nwindow.OCAS_DATA = ")
        json.dump(out, f, separators=(",", ":"))
        f.write(";\n")
    for p in soc["top"]:
        t = p["track"]
        print(f"  {p['name1'][:24]:24} x {p['name2'][:24]:24} SOCRATES {p['range_m']:>4} m | "
              + (f"latest elements + SGP4: {t['min_sep_km']:.2f} km ({t['min_sep_at_s']:+.1f} s)" if t else "no orbit"))
    print("done: visualizer/data.js. Open visualizer/index.html in a browser.")


if __name__ == "__main__":
    main()
