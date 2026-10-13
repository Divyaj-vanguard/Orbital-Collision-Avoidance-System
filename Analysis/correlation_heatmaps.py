"""Pearson correlation heatmaps and feature selection: ESA train, ESA test, live CelesTrak.

    .venv_lstm/bin/python ml/analysis/correlation_heatmaps.py             # downloads live CelesTrak data
    .venv_lstm/bin/python ml/analysis/correlation_heatmaps.py --offline   # newest local CelesTrak files

r = sum((x - mean_x)(y - mean_y)) / sqrt(sum((x - mean_x)^2) * sum((y - mean_y)^2)),  r in [-1, 1]

1. Candidates: every numeric field except IDs, the target, fields computed from the
   target, fields missing in more than 20% of rows, and constant fields. Fields with
   |skew| > 2 are log-scaled first (sign(x) * log10(1 + |x|)), otherwise a few huge
   values decide r.
2. Heatmap of every candidate against every other, ordered by |r| with the target.
3. Selection: rank by |r| with the target (most important first), then keep a field
   only if its |r| with every field already kept is below 0.85 (minimum redundancy).
   ESA fields are ranked on the training file only; the test file just confirms.

Targets: ESA `risk` = log10(Pc). CelesTrak SOCRATES has no operational Pc, only
MAX_PROB, the highest Pc over all possible covariance sizes; target = log10(MAX_PROB).

Outputs:
  results/heatmaps/esa_train_all.png      all ESA candidates, training file
  results/heatmaps/esa_test_all.png       same fields and order, test file
  results/heatmaps/esa_selected.png       selected fields + risk, train next to test
  results/heatmaps/celestrak_live.png     live SOCRATES + SATCAT fields
  results/correlation_selection.txt       rankings, selections, train/test stability
"""
import argparse
import glob
import hashlib
import os
import sys
import urllib.request
from datetime import datetime, timezone

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ESA = os.path.join(ROOT, "data", "esa")
CT = os.path.join(ROOT, "data", "external", "celestrak")
MANIFEST = os.path.join(ROOT, "data", "external", "fetch_manifest.txt")
OUT_DIR = os.path.join(ROOT, "results", "heatmaps")
REPORT = os.path.join(ROOT, "results", "correlation_selection.txt")
SOCRATES_URL = "https://celestrak.org/SOCRATES/sort-minRange.csv"
SATCAT_URL = "https://celestrak.org/pub/satcat.csv"

K_ESA, K_CT = 10, 6
REDUNDANT = 0.85
MAX_MISSING = 0.20
FLOOR = -30.0  # ESA risk floor: Pc reported as ~0
# IDs, the target, and fields computed from the target (correlating them with it is circular)
ESA_EXCLUDE = {"event_id", "mission_id", "risk", "max_risk_estimate", "max_risk_scaling"}
CT_EXCLUDE = {"log10 max_prob", "dilution"}  # DILUTION is an output of the same Pc calculation


def slog(x):
    return np.sign(x) * np.log10(1.0 + np.abs(x))


def prepare(df, target, exclude):
    """Candidate columns, log-scaled where skewed. Returns (X, logged)."""
    cands = [c for c in df.columns if c not in exclude and c != target
             and pd.api.types.is_numeric_dtype(df[c])
             and df[c].isna().mean() <= MAX_MISSING and df[c].nunique() > 1]
    X = df[cands].astype(float).copy()
    logged = {c for c in cands if abs(X[c].skew()) > 2}
    for c in logged:
        X[c] = slog(X[c])
    return X, logged


def select(X, y, k):
    """Most correlated with the target first; skip any field |r| >= REDUNDANT with a kept one."""
    r_target = X.corrwith(y)
    rank = r_target.abs().sort_values(ascending=False).dropna()
    P = X.corr()
    kept, skipped = [], []
    for c in rank.index:
        clash = next((s for s in kept if abs(P.loc[c, s]) >= REDUNDANT), None)
        if clash:
            skipped.append((c, clash, P.loc[c, clash]))
        else:
            kept.append(c)
        if len(kept) == k:
            break
    return r_target, rank, kept, skipped


def heatmap(ax, M, title, annotate, mark=()):
    im = ax.imshow(M.values, cmap="RdBu_r", vmin=-1, vmax=1, interpolation="nearest")
    n = len(M)
    fs = 9 if n <= 16 else max(3.2, 9 - n / 12)
    ax.set_xticks(range(n), M.columns, rotation=90, fontsize=fs)
    ax.set_yticks(range(n), M.index, fontsize=fs)
    for lab in ax.get_xticklabels() + ax.get_yticklabels():
        if lab.get_text() in mark:
            lab.set_fontweight("bold")
    if annotate:
        for i in range(n):
            for j in range(n):
                v = M.values[i, j]
                if np.isfinite(v):
                    ax.text(j, i, f"{v:.2f}", ha="center", va="center", fontsize=7,
                            color="white" if abs(v) > 0.55 else "black")
    ax.set_title(title, fontsize=11)
    return im


def save(fig, name):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name)
    fig.savefig(path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    return os.path.relpath(path, ROOT)


def fetch(url, dest):
    with urllib.request.urlopen(url, timeout=120) as r:
        body = r.read()
    with open(dest, "wb") as f:
        f.write(body)
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    with open(MANIFEST, "a") as f:
        f.write(f"{now}  {os.path.relpath(dest, ROOT)}  {len(body)} bytes  "
                f"sha256 {hashlib.sha256(body).hexdigest()}  {url}\n")


def celestrak_files(offline):
    if not offline:
        stamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H%MZ")
        soc = os.path.join(CT, "socrates", f"socrates_minrange_{stamp}.csv")
        sat = os.path.join(CT, "satcat", f"satcat_{stamp}.csv")
        print("downloading live CelesTrak SOCRATES and SATCAT ...")
        fetch(SOCRATES_URL, soc)
        fetch(SATCAT_URL, sat)
        return soc, sat, datetime.now(timezone.utc)
    soc = sorted(glob.glob(os.path.join(CT, "socrates", "*.csv")), key=os.path.getmtime)[-1]
    sat = sorted(glob.glob(os.path.join(CT, "satcat", "*.csv")), key=os.path.getmtime)[-1]
    return soc, sat, datetime.fromtimestamp(os.path.getmtime(soc), timezone.utc)


def celestrak_table(soc_path, sat_path, ref):
    """One row per SOCRATES pair: geometry of the encounter + catalogue data of both objects."""
    soc = pd.read_csv(soc_path)
    sat = pd.read_csv(sat_path, usecols=["NORAD_CAT_ID", "PERIOD", "INCLINATION", "APOGEE", "PERIGEE", "RCS"])
    sat = sat.drop_duplicates("NORAD_CAT_ID").set_index("NORAD_CAT_ID")
    tca = pd.to_datetime(soc["TCA"], utc=True)
    df = pd.DataFrame({
        "miss distance km": soc["TCA_RANGE"],
        "relative speed km/s": soc["TCA_RELATIVE_SPEED"],
        "hours to TCA": (tca - pd.Timestamp(ref)).dt.total_seconds() / 3600,
        "elset age 1 d": soc["DSE_1"],
        "elset age 2 d": soc["DSE_2"],
        "dilution": soc["DILUTION"],
        "log10 max_prob": np.log10(soc["MAX_PROB"].clip(lower=1e-30)),
    })
    for i in (1, 2):
        s = sat.reindex(soc[f"NORAD_CAT_ID_{i}"].values)
        for col, name in (("PERIOD", "period min"), ("INCLINATION", "inclination deg"),
                          ("APOGEE", "apogee km"), ("PERIGEE", "perigee km"), ("RCS", "radar size m2")):
            df[f"{name} {i}"] = s[col].values
    n0 = len(df)
    # Docked or co-located objects (speed ~0) are not encounters; past TCAs are not forecasts.
    df = df[(df["relative speed km/s"] > 0.01) & (df["hours to TCA"] >= 0)]
    return df, n0 - len(df)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--offline", action="store_true", help="use the newest local CelesTrak files")
    args = ap.parse_args()
    lines = []
    say = lambda s="": (print(s), lines.append(s))

    # ---- ESA train / test --------------------------------------------------------------------
    for f in ("train_data.csv", "test_data.csv"):
        if not os.path.exists(os.path.join(ESA, f)):
            sys.exit(f"error: data/esa/{f} missing (ESA Kelvins download, see ml/lstm_cpp/run_ml_eval.sh)")
    train = pd.read_csv(os.path.join(ESA, "train_data.csv"))
    test = pd.read_csv(os.path.join(ESA, "test_data.csv"))
    Xtr, logged = prepare(train, "risk", ESA_EXCLUDE)
    Xte = test[Xtr.columns].astype(float).copy()
    for c in logged:
        Xte[c] = slog(Xte[c])
    r_tr, rank, kept, skipped = select(Xtr, train["risk"], K_ESA)
    r_te = Xte.corrwith(test["risk"])
    above = train["risk"] > FLOOR
    r_nofloor = Xtr[above].corrwith(train.loc[above, "risk"])

    say("PEARSON CORRELATION AND FEATURE SELECTION")
    say(f"ESA train {len(train):,} CDMs / {train.event_id.nunique():,} events; "
        f"test {len(test):,} CDMs / {test.event_id.nunique():,} events. Target: risk = log10(Pc).")
    say(f"{Xtr.shape[1]} candidate fields, {len(logged)} log-scaled (|skew| > 2). "
        f"{(~above).mean():.1%} of training rows sit at the risk floor ({FLOOR:g}).")
    say("\n1. Ranking by |r| with risk (training file); 'no floor' drops rows at the floor; "
        "'test' is the official test file")
    say(f"  {'field':30} {'r train':>8} {'no floor':>9} {'r test':>8}")
    for c in rank.index[:20]:
        flag = "  <- unstable" if abs(r_tr[c] - r_te[c]) > 0.15 else ""
        say(f"  {c:30} {r_tr[c]:8.3f} {r_nofloor[c]:9.3f} {r_te[c]:8.3f}{flag}")
    say(f"  {int((r_tr.abs() < 0.1).sum())} of {len(r_tr)} fields have |r| < 0.1 with risk.")

    say(f"\n2. Selected {len(kept)} (most correlated with risk, each |r| < {REDUNDANT} with every other selected):")
    for c in kept:
        say(f"  {c:30} r train {r_tr[c]:+.3f}  test {r_te[c]:+.3f}{'  (log-scaled)' if c in logged else ''}")
    say("  Skipped as redundant:")
    for c, s, r in skipped:
        say(f"    {c:28} r = {r:+.2f} with {s}")
    sel = Xtr[kept]
    off = sel.corr().abs().values[np.triu_indices(len(kept), 1)]
    say(f"  Among the selected: max |r| = {off.max():.2f}, mean |r| = {off.mean():.2f}")

    order = list(rank.index) + [c for c in Xtr.columns if c not in rank.index]
    full_tr = pd.concat([Xtr[order], train["risk"]], axis=1).corr()
    full_te = pd.concat([Xte[order], test["risk"]], axis=1).corr()
    figs = []
    for M, name, title in ((full_tr, "esa_train_all.png", "ESA training file"),
                           (full_te, "esa_test_all.png", "ESA test file (same order)")):
        fig, ax = plt.subplots(figsize=(17, 15))
        im = heatmap(ax, M, f"Pearson r, {title}: {len(order)} fields + risk, ordered by |r| with risk "
                            f"(selected in bold)", annotate=False, mark=set(kept))
        fig.colorbar(im, ax=ax, shrink=0.6, label="Pearson r")
        figs.append(save(fig, name))
    fig, axes = plt.subplots(1, 2, figsize=(19, 7.5), gridspec_kw={"wspace": 0.5})
    for ax, X, y, t in ((axes[0], Xtr, train["risk"], "train"), (axes[1], Xte, test["risk"], "test")):
        M = pd.concat([X[kept], y], axis=1).corr()
        im = heatmap(ax, M, f"Selected fields + risk, {t}", annotate=True)
    fig.colorbar(im, ax=axes, shrink=0.8, label="Pearson r")
    figs.append(save(fig, "esa_selected.png"))

    # ---- live CelesTrak -------------------------------------------------------------------------
    soc_path, sat_path, ref = celestrak_files(args.offline)
    ct, dropped = celestrak_table(soc_path, sat_path, ref)
    target = ct["log10 max_prob"]
    Xct, ct_logged = prepare(ct, "log10 max_prob", CT_EXCLUDE)
    r_ct, rank_ct, kept_ct, skipped_ct = select(Xct, target, K_CT)

    say(f"\n3. Live CelesTrak: {os.path.relpath(soc_path, ROOT)} + {os.path.relpath(sat_path, ROOT)}")
    say(f"   {len(ct):,} conjunction pairs ({dropped:,} dropped: relative speed <= 0.01 km/s or TCA passed). "
        f"Target: log10(MAX_PROB). {len(ct_logged)} fields log-scaled.")
    say(f"  {'field':30} {'r':>8}")
    for c in rank_ct.index:
        say(f"  {c:30} {r_ct[c]:8.3f}{'  selected' if c in kept_ct else ''}")
    for c, s, r in skipped_ct:
        say(f"  skipped {c} (r = {r:+.2f} with {s})")
    say(f"  DILUTION excluded: it comes out of the same calculation as MAX_PROB.")
    M = pd.concat([Xct[list(rank_ct.index)], target], axis=1).corr()
    fig, ax = plt.subplots(figsize=(11, 9.5))
    im = heatmap(ax, M, f"Pearson r, live CelesTrak SOCRATES ({len(ct):,} pairs), selected in bold",
                 annotate=True, mark=set(kept_ct))
    fig.colorbar(im, ax=ax, shrink=0.8, label="Pearson r")
    figs.append(save(fig, "celestrak_live.png"))

    say("\nReading the numbers")
    say("  * Pearson sees straight-line links only; |r| near 0 does not prove a field is useless.")
    say("  * ESA CDMs of one event are near-duplicates, so long events weigh more.")
    say("  * MAX_PROB is computed from the miss distance and object sizes, so a strong r with miss")
    say("    distance is expected by construction; SOCRATES has no covariance, so its fields cannot")
    say("    replace the ESA inputs. It is an independent check on live data, not training data.")
    say("  * Correlation filters candidates; only training a model on them shows whether they help")
    say("    (ml/analysis/select_features_lstm.py).")
    say("\nFigures: " + ", ".join(figs))
    with open(REPORT, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"saved {os.path.relpath(REPORT, ROOT)}")


if __name__ == "__main__":
    main()
