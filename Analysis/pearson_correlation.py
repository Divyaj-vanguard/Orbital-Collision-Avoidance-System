"""Pearson correlation on the ESA Kelvins train and test files.

r = sum((x - mean_x) * (y - mean_y)) / sqrt(sum((x - mean_x)^2) * sum((y - mean_y)^2))

Part A: every numeric column against `risk` (log10 of the collision probability),
        on train and on test, to see which inputs carry a linear signal and
        whether it holds on unseen data.
Part B: the four inputs OCAS models use (miss distance, relative speed, time to
        TCA, covariance size) against each other and against risk, Pearson next
        to Spearman (Pearson on ranks), which also catches monotonic curves.

Run from the repo root:
    .venv_lstm/bin/python ml/analysis/pearson_correlation.py
Output is printed and saved to results/pearson_correlation.txt.
"""
import os
import sys

import numpy as np
import pandas as pd

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "results", "pearson_correlation.txt")
TOP = 15
# Columns computed from the collision probability itself: correlating them with risk is circular.
RISK_DERIVED = {"max_risk_estimate", "max_risk_scaling"}
SKIP = {"event_id", "mission_id", "risk"}


def pearson(x, y):
    """Pearson r from the definition, on rows where both values are present."""
    x, y = np.asarray(x, dtype=float), np.asarray(y, dtype=float)
    ok = np.isfinite(x) & np.isfinite(y)
    x, y = x[ok], y[ok]
    if len(x) < 3:
        return float("nan"), len(x)
    dx, dy = x - x.mean(), y - y.mean()
    den = np.sqrt((dx * dx).sum() * (dy * dy).sum())
    return (float((dx * dy).sum() / den) if den > 0 else float("nan")), len(x)


def ocas_inputs(df):
    """The four OCAS model inputs, log-scaled where the raw values are heavily skewed."""
    cov_trace = sum(df[f"{o}_sigma_{a}"] ** 2 for o in ("t", "c") for a in ("r", "t", "n"))
    return pd.DataFrame({
        "log10 miss distance": np.log10(df["miss_distance"] + 1),
        "relative speed": df["relative_speed"],
        "time to TCA": df["time_to_tca"],
        "log10 covariance": np.log10(np.sqrt(cov_trace) + 1),
        "risk (log10 Pc)": df["risk"],
    })


def main():
    paths = {"train": os.path.join(ROOT, "data", "esa", "train_data.csv"),
             "test": os.path.join(ROOT, "data", "esa", "test_data.csv")}
    for p in paths.values():
        if not os.path.exists(p):
            sys.exit(f"error: {p} missing (ESA Kelvins data, see ml/lstm_cpp/run_ml_eval.sh)")
    data = {k: pd.read_csv(p) for k, p in paths.items()}
    lines = []
    say = lambda s="": (print(s), lines.append(s))

    # Sanity check: the hand-written formula must match numpy's
    x = data["train"]["miss_distance"].to_numpy(float)
    y = data["train"]["risk"].to_numpy(float)
    ok = np.isfinite(x) & np.isfinite(y)
    assert abs(pearson(x, y)[0] - np.corrcoef(x[ok], y[ok])[0, 1]) < 1e-9

    say(f"PEARSON CORRELATION  train {len(data['train']):,} CDMs / {data['train'].event_id.nunique():,} events, "
        f"test {len(data['test']):,} CDMs / {data['test'].event_id.nunique():,} events")
    say("Target: risk = log10(collision probability). r in [-1, 1]; |r| near 0 means no straight-line link.")

    # ---- Part A ---------------------------------------------------------------------
    cols = [c for c in data["train"].columns if c not in SKIP and pd.api.types.is_numeric_dtype(data["train"][c])]
    rows = []
    for c in cols:
        r_tr, n_tr = pearson(data["train"][c], data["train"]["risk"])
        r_te, _ = pearson(data["test"][c], data["test"]["risk"]) if c in data["test"] else (float("nan"), 0)
        rows.append((c, r_tr, r_te, n_tr))
    rows = [r for r in rows if np.isfinite(r[1])]
    rows.sort(key=lambda r: -abs(r[1]))

    say(f"\nA. Top {TOP} columns by |r| with risk (ranked on train; test shows whether it holds)")
    say(f"  {'column':32} {'r train':>8} {'r test':>8} {'n train':>9}  note")
    for c, r_tr, r_te, n in rows[:TOP]:
        note = "derived from risk (circular)" if c in RISK_DERIVED else ""
        if np.isfinite(r_te) and abs(r_tr - r_te) > 0.15:
            note = (note + "; " if note else "") + "unstable between files"
        say(f"  {c:32} {r_tr:8.3f} {r_te:8.3f} {n:9,}  {note}")
    weak = sum(1 for r in rows if abs(r[1]) < 0.1)
    say(f"  {weak} of {len(rows)} numeric columns have |r| < 0.1 with risk on train.")

    # ---- Part B ---------------------------------------------------------------------
    for name in ("train", "test"):
        X = ocas_inputs(data[name])
        P = X.corr(method="pearson")
        S = X.corr(method="spearman")
        say(f"\nB. OCAS model inputs, {name} file: Pearson (Spearman in brackets)")
        say("  " + " " * 22 + "".join(f"{c[:16]:>18}" for c in X.columns))
        for a in X.columns:
            say(f"  {a:22}" + "".join(f"{P.loc[a, b]:9.3f} ({S.loc[a, b]:5.2f})" for b in X.columns))

    say("\nReading the numbers")
    say("  * Pearson only sees straight-line links. Where Spearman is much larger, the link is a curve.")
    say("  * With 160k rows almost any r is statistically 'significant'; judge by size, not p-value.")
    say("  * CDMs of one event are near-duplicates, so long events weigh more. Use train to choose")
    say("    features and test only to confirm, to avoid leaking test information into the model.")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w").write("\n".join(lines) + "\n")
    print(f"\nsaved {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
