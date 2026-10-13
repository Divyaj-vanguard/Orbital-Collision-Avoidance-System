"""Pick LSTM input features with correlation, then check the pick by training.

    .venv_lstm/bin/python ml/analysis/select_features_lstm.py    # from the repo root, ~2 min

1. Candidates: every numeric CDM field in the ESA data, except IDs, the label,
   fields computed from the label (max_risk_estimate, max_risk_scaling) and
   fields missing in more than 20% of CDMs.
2. Rank by |correlation with risk| on TRAINING events only (same 70/15/15 event
   split and seed as train_lstm.py). Skewed fields are log-scaled first
   (sign(x) * log10(1 + |x|)) so Pearson is not driven by a few huge values.
   Spearman (rank) is used for the ranking because it also catches monotonic
   curves; Pearson is reported next to it.
3. Redundancy: walk down the ranking, keep a field only if its |Pearson| with
   every field already kept is below 0.85, until K fields are kept.
4. Check: train the same LSTM on (a) the four inputs OCAS uses today and
   (b) the selected fields, and compare on validation, held-out test events and
   the official ESA test file. Correlation is a filter; this step decides.

Outputs (the production model ml/models/lstm_risk.pt is NOT touched):
  results/lstm_feature_selection.txt     ranking, selection, comparison
  results/feature_correlation.json       correlation matrix for the visualizer heatmap
  ml/models/lstm_risk_selected.pt        weights of model (b)
  ml/models/lstm_selected_meta.json      its feature list, transforms and scaler
"""
import json
import os
import random
import sys
import time

import numpy as np
import pandas as pd
import torch
from torch import nn

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "lstm"))
from lstm_common import LEVELS, risk_level  # noqa: E402
from train_lstm import BATCH, HIDDEN, LR, MAX_EPOCHS, PATIENCE, SEED, binary, metrics  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
K = 10                      # features to keep
REDUNDANT = 0.85            # |Pearson| above this with a kept field = redundant
MAX_MISSING = 0.20
EXCLUDE = {"event_id", "mission_id", "risk", "max_risk_estimate", "max_risk_scaling"}
SIGMAS = ["t_sigma_r", "t_sigma_t", "t_sigma_n", "c_sigma_r", "c_sigma_t", "c_sigma_n"]
BASELINE = ["miss_distance", "relative_speed", "time_to_tca", "cov_trace"]  # what OCAS uses today


def slog(x):
    return np.sign(x) * np.log10(1.0 + np.abs(x))


def load(path):
    df = pd.read_csv(path)
    df["cov_trace"] = sum(df[s] ** 2 for s in SIGMAS)
    df["level"] = df["risk"].map(risk_level)
    return df


class SeqLSTM(nn.Module):
    def __init__(self, n_in):
        super().__init__()
        self.lstm = nn.LSTM(n_in, HIDDEN, batch_first=True)
        self.head = nn.Linear(HIDDEN, len(LEVELS))

    def forward(self, x):
        return self.head(self.lstm(x)[0])


def sequences(df, ids, cols, logged, fill, mean, std):
    sub = df[df.event_id.isin(ids)].sort_values(["event_id", "time_to_tca"], ascending=[True, False])
    X = sub[cols].copy()
    for c in logged:
        X[c] = slog(X[c])
    X = ((X.fillna(fill) - mean) / std).to_numpy(np.float32)
    y = sub["level"].to_numpy(np.int64)
    out, start = [], 0
    for n in sub.groupby("event_id", sort=True).size().to_numpy():
        out.append((torch.from_numpy(X[start:start + n]), torch.from_numpy(y[start:start + n])))
        start += n
    return out


def batches(seqs, rng):
    order = list(range(len(seqs)))
    rng.shuffle(order)
    for i in range(0, len(order), BATCH):
        chunk = [seqs[j] for j in order[i:i + BATCH]]
        yield (nn.utils.rnn.pad_sequence([c[0] for c in chunk], batch_first=True),
               nn.utils.rnn.pad_sequence([c[1] for c in chunk], batch_first=True, padding_value=-1))


@torch.no_grad()
def predict(model, seqs):
    model.eval()
    p = [model(x.unsqueeze(0)).reshape(-1, len(LEVELS)).argmax(1) for x, _ in seqs]
    return torch.cat(p).numpy(), torch.cat([y for _, y in seqs]).numpy()


def train_eval(name, cols, train_df, test_df, splits, logged, out):
    torch.manual_seed(SEED)
    tr_ids, va_ids, te_ids = splits
    tr = train_df[train_df.event_id.isin(tr_ids)]
    T = tr[cols].copy()
    for c in logged:
        T[c] = slog(T[c])
    fill, mean, std = T.median(), T.mean(), T.std().replace(0, 1)
    args = (cols, logged, fill, mean, std)
    train = sequences(train_df, tr_ids, *args)
    val = sequences(train_df, va_ids, *args)
    test = sequences(train_df, te_ids, *args)
    official = sequences(test_df, sorted(test_df.event_id.unique()), *args)

    counts = np.bincount(torch.cat([y for _, y in train]).numpy(), minlength=len(LEVELS))
    w = torch.tensor((counts.sum() / counts) ** 0.5, dtype=torch.float32)
    model = SeqLSTM(len(cols))
    opt = torch.optim.Adam(model.parameters(), lr=LR)
    loss_fn = nn.CrossEntropyLoss(weight=w / w.mean(), ignore_index=-1)
    rng = random.Random(SEED)
    best, best_state, best_epoch = -1.0, None, 0
    for epoch in range(1, MAX_EPOCHS + 1):
        model.train()
        for xs, ys in batches(train, rng):
            opt.zero_grad()
            loss = loss_fn(model(xs).reshape(-1, len(LEVELS)), ys.reshape(-1))
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            opt.step()
        f1 = metrics(*predict(model, val))["macro_f1"]
        if f1 > best:
            best, best_epoch, best_state = f1, epoch, {k: v.clone() for k, v in model.state_dict().items()}
        elif epoch - best_epoch >= PATIENCE:
            break
    model.load_state_dict(best_state)

    row = {"name": name, "n": len(cols), "val_f1": best, "epoch": best_epoch}
    for key, seqs in (("test", test), ("official", official)):
        pred, true = predict(model, seqs)
        m = metrics(pred, true)
        sens, spec = binary(pred, true, 2)
        row[key] = dict(accuracy=m["accuracy"], macro_f1=m["macro_f1"], sens_high=sens, spec_high=spec)
    print(f"  {name}: best epoch {best_epoch}, validation macro-F1 {best:.3f}")
    return row, model, dict(columns=cols, log_scaled=sorted(logged), fill=fill.tolist(), mean=mean.tolist(),
                            std=std.tolist())


def main():
    torch.use_deterministic_algorithms(True)
    torch.set_num_threads(1)
    t0 = time.time()
    lines = []
    out = lambda s="": (print(s), lines.append(s))

    train_df, test_df = load(os.path.join(ROOT, "data", "esa", "train_data.csv")), \
        load(os.path.join(ROOT, "data", "esa", "test_data.csv"))
    ids = sorted(train_df.event_id.unique())
    random.Random(SEED).shuffle(ids)
    n_tr, n_va = int(0.70 * len(ids)), int(0.15 * len(ids))
    splits = (set(ids[:n_tr]), set(ids[n_tr:n_tr + n_va]), set(ids[n_tr + n_va:]))
    tr = train_df[train_df.event_id.isin(splits[0])]

    # ---- 1-2. candidates and ranking (training events only) -------------------------------
    cands = [c for c in tr.columns if c not in EXCLUDE | {"level"} and pd.api.types.is_numeric_dtype(tr[c])
             and tr[c].isna().mean() <= MAX_MISSING and tr[c].nunique() > 1]
    logged = {c for c in cands if abs(tr[c].skew()) > 2}
    X = tr[cands].copy()
    for c in logged:
        X[c] = slog(X[c])
    spear = X.corrwith(tr["risk"], method="spearman")
    pear = X.corrwith(tr["risk"], method="pearson")
    rank = spear.abs().sort_values(ascending=False)

    out(f"LSTM FEATURE SELECTION  (ranking on {len(splits[0]):,} training events, {len(tr):,} CDMs; test data unused)")
    out(f"candidates {len(cands)} | log-scaled (skew > 2): {len(logged)} | excluded: IDs, label, label-derived, >20% missing")
    out(f"\nTop 20 by |Spearman| with risk")
    out(f"  {'field':30} {'Spearman':>9} {'Pearson':>8}")
    for c in rank.index[:20]:
        out(f"  {c:30} {spear[c]:9.3f} {pear[c]:8.3f}")

    # ---- 3. redundancy filter ---------------------------------------------------------------
    P = X.corr(method="pearson")
    kept, dropped = [], []
    for c in rank.index:
        clash = next((k for k in kept if abs(P.loc[c, k]) >= REDUNDANT), None)
        if clash:
            dropped.append((c, clash, P.loc[c, clash]))
        else:
            kept.append(c)
        if len(kept) == K:
            break
    out(f"\nSelected {K} (each |Pearson| < {REDUNDANT} with every other kept field):")
    for c in kept:
        out(f"  {c:30} Spearman {spear[c]:+.3f}{'  (log-scaled)' if c in logged else ''}")
    if dropped:
        out("Skipped as redundant:")
        for c, k, r in dropped:
            out(f"  {c:30} r = {r:+.2f} with {k}")

    # ---- 4. check by training ---------------------------------------------------------------
    out("\nTraining the same LSTM on each feature set (seed 2026, same event split) ...")
    base_logged = {"miss_distance", "time_to_tca", "cov_trace"}
    rows = []
    r, _, _ = train_eval("A. OCAS inputs today (4)", BASELINE, train_df, test_df, splits, base_logged, out)
    rows.append(r)
    r, model, meta = train_eval(f"B. Selected by correlation ({K})", kept, train_df, test_df, splits,
                                logged & set(kept), out)
    rows.append(r)
    combo = BASELINE + [c for c in kept if c not in BASELINE]
    r, _, _ = train_eval(f"C. OCAS inputs + selected ({len(combo)})", combo, train_df, test_df, splits,
                         (logged | base_logged) & set(combo), out)
    rows.append(r)

    out("\nResult (High-or-worse = Pc >= 1e-5, the safety-relevant yes/no question)")
    out(f"  {'feature set':32} {'val F1':>7} | {'held-out test events':^31} | {'official ESA test file':^31}")
    out(f"  {'':32} {'':>7} | {'acc':>6} {'macF1':>6} {'sens':>8} {'spec':>8} | {'acc':>6} {'macF1':>6} {'sens':>8} {'spec':>8}")
    for r in rows:
        t, o = r["test"], r["official"]
        out(f"  {r['name']:32} {r['val_f1']:7.3f} | {t['accuracy']:6.3f} {t['macro_f1']:6.3f} {t['sens_high']:8.3f} "
            f"{t['spec_high']:8.3f} | {o['accuracy']:6.3f} {o['macro_f1']:6.3f} {o['sens_high']:8.3f} {o['spec_high']:8.3f}")
    out("  sens/spec: share of High-or-Critical CDMs caught / share of lower CDMs left alone.")

    torch.save(model.state_dict(), os.path.join(ROOT, "ml", "models", "lstm_risk_selected.pt"))
    json.dump(dict(meta, hidden=HIDDEN, seed=SEED, levels=LEVELS), open(os.path.join(ROOT, "ml", "models",
              "lstm_selected_meta.json"), "w"), indent=1)

    heat = BASELINE + [c for c in kept if c not in BASELINE]
    H = tr[heat].copy()
    for c in (logged | base_logged) & set(heat):
        H[c] = slog(H[c])
    H["risk"] = tr["risk"]
    M = H.corr(method="pearson")
    json.dump(dict(fields=list(M.columns), matrix=[[round(float(v), 3) for v in row] for row in M.to_numpy()],
                   selected=kept, baseline=BASELINE, log_scaled=sorted((logged | base_logged) & set(heat)),
                   events=len(splits[0]), cdms=len(tr), comparison=rows),
              open(os.path.join(ROOT, "results", "feature_correlation.json"), "w"), indent=1)
    open(os.path.join(ROOT, "results", "lstm_feature_selection.txt"), "w").write("\n".join(lines) + "\n")
    print(f"\nsaved results/lstm_feature_selection.txt, results/feature_correlation.json, "
          f"ml/models/lstm_risk_selected.pt ({time.time() - t0:.0f} s)")


if __name__ == "__main__":
    main()
