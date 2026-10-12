"""Risk prediction with the Python LSTM, one threat at a time.

Call once per threat:
    predict_risk(miss_m, vel_km_s, tca_s, cov_m2, alert_id)  ->  "Nominal" | "Caution" | "High" | "Critical"

The LSTM keeps a memory (hidden state) per alert ID, so a newer CDM for the
same event is judged together with the earlier ones. Calling without alert_id
judges the threat on its own.
Train the model first:  .venv_lstm/bin/python ml/lstm/train_lstm.py
"""
import json
import os
import sys
from collections import OrderedDict

import torch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lstm_common import LEVELS, RiskLSTM, raw_features  # noqa: E402

_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "models")
_MAX_EVENTS = 4096  # memory bound: the least recently seen event is forgotten first

torch.set_num_threads(1)
with open(os.path.join(_DIR, "lstm_risk_meta.json")) as _f:
    _META = json.load(_f)
_MODEL = RiskLSTM(_META["hidden"])
_MODEL.load_state_dict(torch.load(os.path.join(_DIR, "lstm_risk.pt"), weights_only=True))
_MODEL.eval()
_MEAN = torch.tensor(_META["feature_mean"], dtype=torch.float32)
_STD = torch.tensor(_META["feature_std"], dtype=torch.float32)
_STATES = OrderedDict()  # alert_id -> LSTM (h, c)


def _features(miss_m, vel_km_s, tca_s, cov_m2):
    x = torch.tensor(raw_features(miss_m, vel_km_s, tca_s, cov_m2, _META["cov_fill_log10"]), dtype=torch.float32)
    x[:4] = (x[:4] - _MEAN) / _STD
    return x.view(1, 1, -1)


@torch.no_grad()
def predict_proba(miss_m, vel_km_s, tca_s, cov_m2, alert_id=None):
    """Probability of each level, advancing the event's memory by one CDM."""
    state = _STATES.get(alert_id) if alert_id is not None else None
    logits, state = _MODEL(_features(miss_m, vel_km_s, tca_s, cov_m2), state)
    if alert_id is not None:
        _STATES[alert_id] = state
        _STATES.move_to_end(alert_id)
        if len(_STATES) > _MAX_EVENTS:
            _STATES.popitem(last=False)
    return torch.softmax(logits.view(-1), 0).tolist()


def predict_risk(miss_m, vel_km_s, tca_s, cov_m2, alert_id=None):
    """Most likely of the 4 risk levels, as a string."""
    p = predict_proba(miss_m, vel_km_s, tca_s, cov_m2, alert_id)
    return LEVELS[max(range(len(p)), key=p.__getitem__)]


def reset():
    """Forget every event's history."""
    _STATES.clear()
