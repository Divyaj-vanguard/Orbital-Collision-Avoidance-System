"""Train the LSTM and the Random Forest on the Pearson-selected features and compare.

    .venv_lstm/bin/python ml/analysis/train_pearson_features.py      # from anywhere, ~5 min

Feature sets (same for both models):
  A. the 4 inputs OCAS uses today: miss distance, relative speed, time to TCA, covariance trace
  B. the 10 fields picked by Pearson in correlation_heatmaps.py (most correlated with risk,
     each |r| < 0.85 with the others), recomputed here on the training file
  C. A + B
Both models predict the 4 risk levels (Nominal / Caution / High / Critical) of every CDM.
The LSTM reads an event's CDMs in order (history); the Random Forest judges each CDM alone.

Same data for both: ESA training file split by EVENT 70 / 15 / 15 with seed 2026 (train /
validation / held-out test events), plus the official ESA test file, which nothing here
has seen. The Random Forest uses the teammate's settings (200 trees, balanced class
weights, no depth limit) plus at least 5 CDMs per leaf: fully grown trees on 113k rows
need several GB of memory and memorise noisy labels.

Outputs (production models are NOT replaced):
  results/pearson_models.txt
  ml/models/lstm_pearson.pt + lstm_pearson_meta.json     LSTM on set B
  ml/models/random_forest_pearson.pkl                     Random Forest on set B (git-ignored)
"""
import json
import os
import pickle
import random
import sys
import time

import numpy as np
import torch
from sklearn.ensemble import RandomForestClassifier

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "lstm"))
from correlation_heatmaps import ESA_EXCLUDE, K_ESA, prepare, select  # noqa: E402
from select_features_lstm import BASELINE, ROOT, load, slog, train_eval  # noqa: E402
from train_lstm import HIDDEN, SEED, binary, metrics  # noqa: E402
from lstm_common import LEVELS  # noqa: E402

BASE_LOGGED = {"miss_distance", "time_to_tca", "cov_trace"}


def rf_eval(name, cols, train_df, test_df, splits, logged):
    tr_ids, va_ids, te_ids = splits
    def X(df):
        M = df[cols].astype(float).copy()
        for c in logged:
            M[c] = slog(M[c])
        return M
    tr = train_df[train_df.event_id.isin(tr_ids)]
    fill = X(tr).median()
    model = RandomForestClassifier(n_estimators=200, class_weight="balanced", min_samples_leaf=5,
                                   n_jobs=2, random_state=SEED)
    model.fit(X(tr).fillna(fill).to_numpy(), tr["level"].to_numpy())
    row = {"name": name, "n": len(cols)}
    parts = (("val", train_df[train_df.event_id.isin(va_ids)]), ("test", train_df[train_df.event_id.isin(te_ids)]),
             ("official", test_df))
    for key, df in parts:
        pred, true = model.predict(X(df).fillna(fill).to_numpy()), df["level"].to_numpy()
        m = metrics(pred, true)
        sens, spec = binary(pred, true, 2)
        row[key] = dict(accuracy=m["accuracy"], macro_f1=m["macro_f1"], sens_high=sens, spec_high=spec)
    row["val_f1"] = row.pop("val")["macro_f1"]
    print(f"  {name}: validation macro-F1 {row['val_f1']:.3f}")
    return row, model, fill


def table(rows, out):
    out(f"  {'feature set':34} {'val F1':>7} | {'held-out test events':^31} | {'official ESA test file':^31}")
    out(f"  {'':34} {'':>7} | {'acc':>6} {'macF1':>6} {'sens':>8} {'spec':>8} | "
        f"{'acc':>6} {'macF1':>6} {'sens':>8} {'spec':>8}")
    for r in rows:
        t, o = r["test"], r["official"]
        out(f"  {r['name']:34} {r['val_f1']:7.3f} | {t['accuracy']:6.3f} {t['macro_f1']:6.3f} {t['sens_high']:8.3f} "
            f"{t['spec_high']:8.3f} | {o['accuracy']:6.3f} {o['macro_f1']:6.3f} {o['sens_high']:8.3f} "
            f"{o['spec_high']:8.3f}")


def main():
    torch.use_deterministic_algorithms(True)
    torch.set_num_threads(1)
    t0 = time.time()
    lines = []
    out = lambda s="": (print(s), lines.append(s))

    train_df = load(os.path.join(ROOT, "data", "esa", "train_data.csv"))
    test_df = load(os.path.join(ROOT, "data", "esa", "test_data.csv"))
    ids = sorted(train_df.event_id.unique())
    random.Random(SEED).shuffle(ids)
    n_tr, n_va = int(0.70 * len(ids)), int(0.15 * len(ids))
    splits = (set(ids[:n_tr]), set(ids[n_tr:n_tr + n_va]), set(ids[n_tr + n_va:]))

    # Same selection as correlation_heatmaps.py (cov_trace is added by load(), not a raw field)
    Xsel, logged = prepare(train_df.drop(columns=["cov_trace", "level"]), "risk", ESA_EXCLUDE)
    _, _, pearson, _ = select(Xsel, train_df["risk"], K_ESA)
    combo = BASELINE + [c for c in pearson if c not in BASELINE]
    sets = [("A. OCAS inputs today", BASELINE, BASE_LOGGED),
            (f"B. Pearson-selected", pearson, logged & set(pearson)),
            (f"C. OCAS inputs + Pearson", combo, (logged | BASE_LOGGED) & set(combo))]

    out("LSTM AND RANDOM FOREST ON PEARSON-SELECTED FEATURES")
    out(f"ESA train {len(train_df):,} CDMs split by event {len(splits[0]):,} / {len(splits[1]):,} / "
        f"{len(splits[2]):,} (seed {SEED}); official test {len(test_df):,} CDMs. 4 levels: {', '.join(LEVELS)}.")
    for name, cols, _ in sets:
        out(f"  {name} ({len(cols)}): {', '.join(cols)}")

    out("\nLSTM (event history, hidden 32, early stopping on validation macro-F1)")
    lstm_rows, lstm_b = [], None
    for name, cols, lg in sets:
        r, model, meta = train_eval(f"{name} ({len(cols)})", cols, train_df, test_df, splits, lg, out)
        lstm_rows.append(r)
        if name.startswith("B"):
            lstm_b = (model, meta)
    table(lstm_rows, out)

    out("\nRandom Forest (each CDM alone, 200 trees, balanced classes, min 5 CDMs per leaf)")
    rf_rows, rf_b = [], None
    for name, cols, lg in sets:
        r, model, fill = rf_eval(f"{name} ({len(cols)})", cols, train_df, test_df, splits, lg)
        rf_rows.append(r)
        if name.startswith("B"):
            rf_b = (model, fill, cols, lg)
        del model  # one forest in memory at a time
    table(rf_rows, out)

    out("\nsens / spec: share of High-or-Critical CDMs caught / share of lower CDMs correctly left alone.")
    out("macF1: macro-F1 over the 4 levels. One seed; differences under ~0.02 may be noise.")
    out("The Pearson ranking used the whole training file, so the held-out-events column has a small")
    out("selection leak; the official ESA test file was not used for anything before this evaluation.")

    models = os.path.join(ROOT, "ml", "models")
    model, meta = lstm_b
    torch.save(model.state_dict(), os.path.join(models, "lstm_pearson.pt"))
    with open(os.path.join(models, "lstm_pearson_meta.json"), "w") as f:
        json.dump(dict(meta, hidden=HIDDEN, seed=SEED, levels=LEVELS), f, indent=1)
    model, fill, cols, lg = rf_b
    with open(os.path.join(models, "random_forest_pearson.pkl"), "wb") as f:
        pickle.dump(dict(model=model, columns=cols, log_scaled=sorted(lg), fill=fill.to_dict(), levels=LEVELS), f)
    with open(os.path.join(ROOT, "results", "pearson_models.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"\nsaved results/pearson_models.txt, ml/models/lstm_pearson.pt, ml/models/random_forest_pearson.pkl "
          f"({time.time() - t0:.0f} s)")


if __name__ == "__main__":
    main()
