"""Shared pieces of the OCAS Python LSTM: features, labels, and the network.

Used by both train_lstm.py (training) and predict.py (called from C++ through
PyBind11), so the two can never disagree on how an input is prepared.

Inputs use the units of the C++ Threat struct (cpp/Threat.h):
    miss distance [m], relative velocity [km/s], time to TCA [s],
    covariance trace [m^2] (0 or negative = unknown).
The collision probability is the LABEL only and is never an input.
"""
import math

import torch
from torch import nn

LEVELS = ["Nominal", "Caution", "High", "Critical"]
N_FEATURES = 5  # log miss, velocity, log TCA, log covariance, covariance-missing flag


def risk_level(log10_pc: float) -> int:
    """4 levels on the provider collision probability Pc (ESA 'risk' = log10 Pc)."""
    if log10_pc >= -4.0:
        return 3  # Critical: Pc >= 1e-4
    if log10_pc >= -5.0:
        return 2  # High:     1e-5 <= Pc < 1e-4
    if log10_pc >= -6.0:
        return 1  # Caution:  1e-6 <= Pc < 1e-5
    return 0      # Nominal:  Pc < 1e-6


def raw_features(miss_m, vel_km_s, tca_s, cov_m2, cov_fill_log):
    """Unscaled feature vector. Logs tame values that span many orders of magnitude."""
    cov_known = cov_m2 is not None and math.isfinite(cov_m2) and cov_m2 > 0.0
    return [
        math.log1p(max(miss_m, 0.0)),
        float(vel_km_s),
        math.log1p(max(tca_s, 0.0)),
        math.log10(cov_m2) if cov_known else cov_fill_log,
        0.0 if cov_known else 1.0,
    ]


class RiskLSTM(nn.Module):
    """One LSTM layer over an event's CDMs; a 4-way prediction after every CDM."""

    def __init__(self, hidden: int = 32):
        super().__init__()
        self.lstm = nn.LSTM(N_FEATURES, hidden, batch_first=True)
        self.head = nn.Linear(hidden, len(LEVELS))

    def forward(self, x, state=None):
        out, state = self.lstm(x, state)
        return self.head(out), state
