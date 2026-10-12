"""Train the basic OCAS risk LSTM (PyTorch) on the ESA Kelvins data.

    .venv_lstm/bin/python ml/lstm/train_lstm.py            # from the repo root

Task   : after every CDM of an event, classify that CDM into 4 risk levels
         (Nominal / Caution / High / Critical, see lstm_common.risk_level),
         using the event's CDM history so far. This is exactly how predict.py
         is used: one call per incoming threat, keyed by alert ID.
Data   : data/esa/train_data.csv and data/esa/test_data.csv (ESA Kelvins).
Split  : by EVENT 70 / 15 / 15 (train / validation / held-out test), seed 2026.
         CDMs of one event are near-duplicates, so a row split would leak.
Model  : LSTM(5 -> 32) + linear head, class-weighted cross-entropy, Adam,
         early stopping on validation macro-F1.
Report : accuracy, macro-F1, and per class precision, sensitivity (recall),
         specificity, F1; the confusion matrix; a majority-class baseline; and
         the same network without history (each CDM alone) to show what the
         sequence adds. Saved to results/python_lstm_metrics.txt.
Output : ml/models/lstm_risk.pt (weights) + ml/models/lstm_risk_meta.json
"""
import csv
import json
import math
import os
import random
import sys
import time

import numpy as np
import torch
from torch import nn

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lstm_common import LEVELS, RiskLSTM, raw_features, risk_level  # noqa: E402

SEED = 2026
HIDDEN = 32
BATCH = 64
MAX_EPOCHS = 30
PATIENCE = 5
LR = 2e-3
SIGMAS = ["t_sigma_r", "t_sigma_t", "t_sigma_n", "c_sigma_r", "c_sigma_t", "c_sigma_n"]


def load_events(path):
    """event_id -> list of (time_to_tca_s, miss_m, vel_km_s, cov_m2 or None, level), earliest CDM first."""
    events = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                sig = [float(row[k]) for k in SIGMAS]
                cov = sum(s * s for s in sig)
                cov = cov if math.isfinite(cov) and cov > 0 else None
            except ValueError:  # empty sigma field
                cov = None
            events.setdefault(row["event_id"], []).append((
                float(row["time_to_tca"]) * 86400.0,      # days -> s
                float(row["miss_distance"]),              # m
                float(row["relative_speed"]) / 1000.0,    # m/s -> km/s
                cov,
                risk_level(float(row["risk"])),
            ))
    for cdms in events.values():
        cdms.sort(key=lambda c: -c[0])  # largest time-to-TCA first = arrival order
    return events


def to_arrays(events, ids, cov_fill_log, mean, std):
    seqs = []
    for e in ids:
        x = np.array([raw_features(m, v, t, c, cov_fill_log) for t, m, v, c, _ in events[e]], dtype=np.float32)
        x[:, :4] = (x[:, :4] - mean) / std
        y = np.array([lvl for *_, lvl in events[e]], dtype=np.int64)
        seqs.append((torch.from_numpy(x), torch.from_numpy(y)))
    return seqs


def batches(seqs, shuffle, rng):
    order = list(range(len(seqs)))
    if shuffle:
        rng.shuffle(order)
    for i in range(0, len(order), BATCH):
        chunk = [seqs[j] for j in order[i:i + BATCH]]
        xs = nn.utils.rnn.pad_sequence([c[0] for c in chunk], batch_first=True)
        ys = nn.utils.rnn.pad_sequence([c[1] for c in chunk], batch_first=True, padding_value=-1)
        yield xs, ys


@torch.no_grad()
def predict(model, seqs, history=True):
    """Per-CDM predictions in arrival order. history=False feeds each CDM alone."""
    model.eval()
    preds, labels = [], []
    for x, y in seqs:
        inp = x.unsqueeze(0) if history else x.unsqueeze(1)
        logits, _ = model(inp)
        preds.append(logits.reshape(-1, len(LEVELS)).argmax(1))
        labels.append(y)
    return torch.cat(preds).numpy(), torch.cat(labels).numpy()


def metrics(pred, true):
    k = len(LEVELS)
    cm = np.zeros((k, k), dtype=np.int64)
    np.add.at(cm, (true, pred), 1)
    n = cm.sum()
    per = []
    for c in range(k):
        tp = cm[c, c]
        fp = cm[:, c].sum() - tp
        fn = cm[c, :].sum() - tp
        tn = n - tp - fp - fn
        prec = tp / (tp + fp) if tp + fp else 0.0
        sens = tp / (tp + fn) if tp + fn else 0.0
        spec = tn / (tn + fp) if tn + fp else 0.0
        f1 = 2 * prec * sens / (prec + sens) if prec + sens else 0.0
        per.append(dict(level=LEVELS[c], support=int(cm[c].sum()), precision=prec, sensitivity=sens,
                        specificity=spec, f1=f1))
    return dict(accuracy=float(np.trace(cm) / n), macro_f1=float(np.mean([p["f1"] for p in per])),
                per_class=per, confusion=cm.tolist())


def binary(pred, true, min_level):
    """Collapse to 'level >= min_level' vs the rest: the safety-relevant yes/no question."""
    p, t = pred >= min_level, true >= min_level
    tp, fp, fn, tn = (p & t).sum(), (p & ~t).sum(), (~p & t).sum(), (~p & ~t).sum()
    return tp / (tp + fn) if tp + fn else 0.0, tn / (tn + fp) if tn + fp else 0.0


def report(title, pred, true, out):
    m = metrics(pred, true)
    out(f"\n{title}  ({len(true)} CDMs)")
    out(f"  accuracy {m['accuracy']:.3f} | macro-F1 {m['macro_f1']:.3f}")
    out(f"  {'level':9s} {'support':>8s} {'precision':>9s} {'sensitivity':>11s} {'specificity':>11s} {'F1':>6s}")
    for p in m["per_class"]:
        out(f"  {p['level']:9s} {p['support']:8d} {p['precision']:9.3f} {p['sensitivity']:11.3f} "
            f"{p['specificity']:11.3f} {p['f1']:6.3f}")
    out("  confusion matrix (rows = true level, columns = predicted level)")
    out("  " + " " * 10 + "".join(f"{lv:>10s}" for lv in LEVELS))
    for lv, row in zip(LEVELS, m["confusion"]):
        out(f"  {lv:10s}" + "".join(f"{v:10d}" for v in row))
    for lvl in (2, 3):
        s, sp = binary(pred, true, lvl)
        out(f"  '{LEVELS[lvl]} or worse' vs rest: sensitivity {s:.3f}, specificity {sp:.3f}")
    return m


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    random.seed(SEED)
    np.random.seed(SEED)
    torch.manual_seed(SEED)
    torch.use_deterministic_algorithms(True)
    torch.set_num_threads(1)  # bit-identical results run to run

    t0 = time.time()
    train_file = load_events(os.path.join(root, "data", "esa", "train_data.csv"))
    test_file = load_events(os.path.join(root, "data", "esa", "test_data.csv"))
    ids = sorted(train_file, key=int)
    random.Random(SEED).shuffle(ids)
    n_tr, n_va = int(0.70 * len(ids)), int(0.15 * len(ids))
    tr_ids, va_ids, te_ids = ids[:n_tr], ids[n_tr:n_tr + n_va], ids[n_tr + n_va:]

    # Scaler and covariance fill value from TRAINING events only.
    known = [math.log10(c) for e in tr_ids for _, _, _, c, _ in train_file[e] if c is not None]
    cov_fill_log = float(np.median(known))
    raw = np.array([raw_features(m, v, t, c, cov_fill_log) for e in tr_ids for t, m, v, c, _ in train_file[e]])
    mean, std = raw[:, :4].mean(0), raw[:, :4].std(0)

    train = to_arrays(train_file, tr_ids, cov_fill_log, mean, std)
    val = to_arrays(train_file, va_ids, cov_fill_log, mean, std)
    test = to_arrays(train_file, te_ids, cov_fill_log, mean, std)
    official = to_arrays(test_file, sorted(test_file, key=int), cov_fill_log, mean, std)

    counts = np.bincount(torch.cat([y for _, y in train]).numpy(), minlength=len(LEVELS))
    weights = torch.tensor((counts.sum() / counts) ** 0.5, dtype=torch.float32)  # inverse-sqrt frequency
    weights /= weights.mean()

    lines = []
    def out(s=""):
        print(s)
        lines.append(s)

    out("OCAS Python LSTM risk model | 4 levels on provider Pc: Nominal <1e-6 <= Caution <1e-5 <= High <1e-4 <= Critical")
    out(f"events: train {len(tr_ids)} | validation {len(va_ids)} | held-out test {len(te_ids)} | official test file {len(official)}")
    out("training CDMs per level: " + ", ".join(f"{lv} {c}" for lv, c in zip(LEVELS, counts)))
    out("class weights (inverse sqrt frequency): " + ", ".join(f"{w:.2f}" for w in weights.tolist()))

    model = RiskLSTM(HIDDEN)
    opt = torch.optim.Adam(model.parameters(), lr=LR)
    loss_fn = nn.CrossEntropyLoss(weight=weights, ignore_index=-1)
    rng = random.Random(SEED)
    best, best_state, best_epoch = -1.0, None, 0
    for epoch in range(1, MAX_EPOCHS + 1):
        model.train()
        total = 0.0
        for xs, ys in batches(train, True, rng):
            opt.zero_grad()
            logits, _ = model(xs)
            loss = loss_fn(logits.reshape(-1, len(LEVELS)), ys.reshape(-1))
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 5.0)
            opt.step()
            total += loss.item()
        f1 = metrics(*predict(model, val))["macro_f1"]
        print(f"  epoch {epoch:2d}  loss {total:8.2f}  validation macro-F1 {f1:.4f}")
        if f1 > best:
            best, best_epoch = f1, epoch
            best_state = {k: v.clone() for k, v in model.state_dict().items()}
        elif epoch - best_epoch >= PATIENCE:
            print(f"  early stop at epoch {epoch}")
            break
    model.load_state_dict(best_state)
    out(f"best epoch {best_epoch} (validation macro-F1 {best:.4f})")

    true = predict(model, test)[1]
    report("BASELINE: always 'Nominal' (held-out test events)", np.zeros_like(true), true, out)
    report("LSTM WITHOUT HISTORY: each CDM alone (held-out test events)", *predict(model, test, history=False), out)
    m_test = report("LSTM WITH HISTORY (held-out test events, unseen during training)", *predict(model, test), out)
    m_off = report("LSTM WITH HISTORY (official ESA test file, unseen during training)", *predict(model, official), out)
    print(f"\ntraining + evaluation time {time.time() - t0:.0f} s")  # console only: keeps the report reproducible

    os.makedirs(os.path.join(root, "ml", "models"), exist_ok=True)
    torch.save(model.state_dict(), os.path.join(root, "ml", "models", "lstm_risk.pt"))
    meta = dict(levels=LEVELS, hidden=HIDDEN, seed=SEED, best_epoch=best_epoch, cov_fill_log10=cov_fill_log,
                feature_mean=mean.tolist(), feature_std=std.tolist(),
                held_out_test=dict(accuracy=m_test["accuracy"], macro_f1=m_test["macro_f1"]),
                official_test=dict(accuracy=m_off["accuracy"], macro_f1=m_off["macro_f1"]))
    with open(os.path.join(root, "ml", "models", "lstm_risk_meta.json"), "w") as f:
        json.dump(meta, f, indent=2)
    os.makedirs(os.path.join(root, "results"), exist_ok=True)
    with open(os.path.join(root, "results", "python_lstm_metrics.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\nsaved ml/models/lstm_risk.pt, ml/models/lstm_risk_meta.json, results/python_lstm_metrics.txt")


if __name__ == "__main__":
    main()
