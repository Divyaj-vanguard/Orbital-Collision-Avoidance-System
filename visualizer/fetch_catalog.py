#!/usr/bin/env python3
"""Download the real tracked-object catalog and write catalog.js for worldview.html.

Plain run (no account):   python fetch_catalog.py
   CelesTrak: active satellites + all objects named DEB + all rocket bodies (R/B),
   classified with CelesTrak's SATCAT. Dead satellites are only included if they
   appear in those lists, so expect few of them.
Full catalog (recommended, free account at space-track.org):
   set SPACETRACK_USER=you@mail.com   and   SPACETRACK_PASS=yourpassword
   (PowerShell:  $env:SPACETRACK_USER="..." ; $env:SPACETRACK_PASS="..." ), then run again.
   This pulls every tracked object, including dead satellites.
Uses only the Python standard library."""
import csv, io, json, os, sys, time, urllib.parse, urllib.request, http.cookiejar
from datetime import datetime, timezone

CT = "https://celestrak.org/NORAD/elements/gp.php?{}&FORMAT=csv"
SATCAT = "https://celestrak.org/pub/satcat.csv"
ST = "https://www.space-track.org"
ACTIVE, DEAD, RB, DEB = 0, 1, 2, 3

def get(url, opener=None):
    req = urllib.request.Request(url, headers={"User-Agent": "OCAS-PBL-student-project"})
    with (opener.open(req, timeout=120) if opener else urllib.request.urlopen(req, timeout=120)) as r:
        return r.read().decode("utf-8", "replace")

def satcat_classes():
    out = {}
    for r in csv.DictReader(io.StringIO(get(SATCAT))):
        if r.get("DECAY_DATE"):
            continue
        t, s = r["OBJECT_TYPE"], r["OPS_STATUS_CODE"]
        out[int(r["NORAD_CAT_ID"])] = RB if t == "R/B" else DEB if t in ("DEB", "UNK") else DEAD if s == "-" else ACTIVE
    return out

def rows_celestrak():
    rows = []
    for q in ("GROUP=active", "NAME=DEB", "NAME=" + urllib.parse.quote("R/B")):
        print("  downloading", q, flush=True)
        rows += list(csv.DictReader(io.StringIO(get(CT.format(q)))))
        time.sleep(3)  # be polite
    return rows

def rows_spacetrack(user, pw):
    jar = http.cookiejar.CookieJar()
    op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
    op.open(ST + "/ajaxauth/login", urllib.parse.urlencode({"identity": user, "password": pw}).encode(), timeout=60)
    q = "/basicspacedata/query/class/gp/decay_date/null-val/epoch/%3Enow-30/orderby/norad_cat_id/format/json"
    return json.loads(get(ST + q, op))

def build(rows, cls):
    seen, objs = set(), []
    for r in rows:
        try:
            nid = int(r["NORAD_CAT_ID"])
            if nid in seen or nid not in cls:
                continue
            ep = r["EPOCH"] if ("+" in r["EPOCH"] or r["EPOCH"].endswith("Z")) else r["EPOCH"] + "+00:00"
            ms = int(datetime.fromisoformat(ep.replace("Z", "+00:00")).astimezone(timezone.utc).timestamp() * 1000)
            objs.append([cls[nid], round(float(r["INCLINATION"]), 3), round(float(r["RA_OF_ASC_NODE"]), 3),
                         round(float(r["ECCENTRICITY"]), 6), round(float(r["ARG_OF_PERICENTER"]), 3),
                         round(float(r["MEAN_ANOMALY"]), 3), round(float(r["MEAN_MOTION"]), 8), ms])
            seen.add(nid)
        except (KeyError, ValueError):
            continue
    return objs

if __name__ == "__main__":
    print("1/3 classifying objects with CelesTrak SATCAT ...")
    cls = satcat_classes()
    print("2/3 downloading orbits ...")
    u, p = os.environ.get("SPACETRACK_USER"), os.environ.get("SPACETRACK_PASS")
    src = "Space-Track GP + CelesTrak SATCAT" if u and p else "CelesTrak GP + SATCAT (partial dead-satellite coverage)"
    objs = build(rows_spacetrack(u, p) if u and p else rows_celestrak(), cls)
    counts = [sum(1 for o in objs if o[0] == k) for k in range(4)]
    print("3/3 writing catalog.js:", len(objs), "objects  [active, dead, rocket bodies, debris] =", counts)
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "catalog.js"), "w") as f:
        f.write("window.OCAS_CATALOG=" + json.dumps({"generated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
                "source": src, "objs": objs}, separators=(",", ":")) + ";\n")
    print("done. Open worldview.html")
