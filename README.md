# OCAS: Autonomous Orbital Collision Avoidance System

**Team ID:** DSCPP-III-2026-T018 · **Institution:** Graphic Era University, Department of CSE
**Faculty mentor:** Dr. Narayan Chaturvedi · **Language:** C++17 only · **Build:** CMake

OCAS is a headless, onboard **threat detection, classification and evasion advisory engine**.
It ingests Conjunction Data Messages (CDMs), scores collision risk with an explainable model,
ranks threats, and emits a platform-agnostic **Evasion Advisory Directive** (delta-v magnitude,
RTN direction, maneuver plane, execution deadline, urgency) for the spacecraft's own flight
control unit (FCU).

OCAS deliberately does **not** command thrusters. Propulsion hardware differs between buses
and manufacturers, so actuation belongs to the OEM FCU. This mirrors a fly-by-wire flight
computer or a missile approach warning system: OCAS decides *what* should happen and *by when*.

---

## 1. Contributors

| Roll No. | Name | Role |
| :-- | :-- | :-- |
| 2510011013 | Adamya Rawat | Team lead, system architecture |
| 2510381135 | Atiksh Sharma | Core backend engineer |
| 2510385597 | Agamya Saini | QA and validation |
| 2510370197 | Krishna Sehgal | Research and documentation |

---

## 2. Architecture

```
 CDM CSV ──► CircularBuffer<64> ──► RiskClassifier ──► OrbitalRegimeStrategy ──► MaxHeap<64>
 (ground      overwrite-oldest       │ LSTM (in-domain)        regime risk           ThreatComparator
  uplink)     overflow counted       └ Conservative (fallback)                          │
                                                                                        ▼
  FlightAuditLogger ◄── Observer ◄── MissionLedger ◄── StateCaretaker ◄── formulate_evasion_directive()
  TerminalTelemetry      events       (doubly linked,    (Memento LIFO:        LEO / MEO / GEO
                                       fixed pool)        checkpoint, unwind)
```

| Module | Files | Pattern / technique |
| :-- | :-- | :-- |
| 1. Ingestion buffer | `include/ingestion/circular_buffer.hpp` | Class template `CircularBuffer<T, N>` on `std::array`, 64-byte aligned, overwrite-oldest with overflow counter |
| 2. Risk classifier | `include/risk/risk_strategy.hpp`, `src/risk/risk_classifier.cpp` | Strategy: `IRiskStrategy` → `LstmStrategy`, `ConservativeThresholdStrategy`; `RiskClassifier` is the context and records *why* it chose a model |
| 3. Priority ranking | `include/priority/max_heap.hpp`, `src/priority/max_heap.cpp` | `MaxHeap<T, N, Compare>` over a fixed array with hole-based sift, `ThreatComparator` functor |
| 4. Regime and advisory | `include/regime/regime_strategy.hpp`, `src/regime/regime_factory.cpp` | Strategy + Factory: `OrbitalRegimeStrategy` → `LEORegime`, `MEORegime`, `GEORegime`; `RegimeFactory::create_regime(altitude_km)` |
| 5. Persistence | `include/persistence/*.hpp`, `src/persistence/*.cpp` | Memento (`Spacecraft`, `SpacecraftMemento`, `StateCaretaker`, `StateStack<T, N>`); doubly linked `MissionLedger` on a node pool; RAII `FlightAuditLogger` |
| 6. Orchestrator | `include/pipeline/ocas_pipeline.hpp`, `src/pipeline/ocas_pipeline.cpp`, `src/main.cpp` | Observer (`IPipelineObserver`, fixed `ObserverRegistry`), latency profiler, allocation counter |

Supporting code lives in `include/core/` (domain types, CSV/JSON I/O, observer, profiler) and
`include/telemetry/` (ANSI terminal output).

### Real-time guarantees

* **No heap allocation inside the cyclic loop.** Every container the loop touches is a fixed-capacity
  member. `src/core/alloc_tracker.cpp` replaces global `operator new` in the flight executable and the
  run prints the count of allocations between the first and last cycle. The executable exits with
  status 3 if that count is not zero. Allocation happens only at init (dataset load, strategy objects).
* **Bounded work per operation.** Buffer push/pop O(1), heap push/pop O(log n) and top O(1), risk
  scoring O(1), memento checkpoint and single rollback O(1), ledger append O(1).
* **No silent data loss.** Ring-buffer overwrites, heap rejections and ledger evictions are counted
  and reported.

---

## 3. Data

| File | Rows | Origin |
| :-- | --: | :-- |
| `data/esa/cleaned_cdm_dataset.csv` | 24,484 | **Real.** ESA Kelvins Collision Avoidance Challenge test set, 2,167 events, reduced by `tools/compact_cdm_dataset` |
| `data/synthetic/synthetic_meo_vectors.csv` | 48 | **Synthetic.** `tools/generate_synthetic_vectors`, seed 2026 |
| `data/synthetic/synthetic_geo_vectors.csv` | 48 | **Synthetic.** same tool |
| `ml/models/lstm_weights.txt` | – | Output of `ml/lstm_cpp/train_lstm`, same split and seed |
| `data/external/derived/socrates_{leo,meo,geo}.csv` | 187,878 / 16 / 33 | **Real, but no covariance.** CelesTrak SOCRATES snapshot of 2026-10-09, split by the primary's regime |
| `data/external/derived/nasa_cara_test_cdms.csv` | 3 | **Real format, test cases.** CCSDS CDMs published in NASA CARA_Analysis_Tools |

`tools/fetch_public_data.sh` downloads the external sources, converts them, and appends each file's
URL, time, size and SHA-256 to `data/external/fetch_manifest.txt`. SOCRATES is a live feed, so a later fetch
gives different data. SOCRATES screens TLEs and publishes no covariance, so every SOCRATES row goes to the
conservative fallback. Its `MAX_PROB` is an upper bound on Pc, not an operational Pc, so these files exercise
the MEO and GEO paths with real geometry but **cannot be used to score a classifier**.

Schema, enforced exactly by the loader:

```
alert_id,miss_distance_m,relative_speed_km_s,time_to_tca_sec,cov_trace_m2,collision_prob
```

* `cov_trace_m2` is the trace of the combined target and chaser RTN position covariance,
  the sum of the six squared sigmas. An empty field is legal. Six ESA rows have one.
* `collision_prob` is the provider's Pc, computed as 10^risk. It is used **only** as the label for offline
  evaluation and never as a model input, to avoid target leakage.
* The ESA data is LEO only, and every CDM in the test set is at least two days before TCA.
  The MEO and GEO files exist to exercise those code paths. They must not be used to report accuracy.

To regenerate the compact file from the raw ESA download (`data/esa/test_data.csv`):

```bash
build/bin/compact_cdm_dataset data/esa/test_data.csv data/esa/cleaned_cdm_dataset.csv
```

---

## 4. Models and physics

### 4.1 LSTM model (in-domain)

The LSTM runs in the same C++ as the flight loop, with no ML runtime. It is trained by
`ml/lstm_cpp/train_lstm` and loaded from a plain-text weight file at init.
Its inputs are miss distance, relative speed, time to TCA and covariance trace (log-scaled), standardised with the training-split mean and standard deviation.
It does not use Pc as an input.

| | LSTM |
| :-- | :-- |
| Shape | 4 → LSTM(16) → 1 |
| Task | per event: will the **final** CDM have log10 Pc ≥ −6? (ESA challenge definition) |
| Input | the event's CDM history so far, keyed by `alert_id` |
| Split | by event, 70 / 15 / 15, seed 2026, using only CDMs ≥ 2 days before TCA |
| Threshold | F1-optimal on validation, never on test |
| Cost | O(1) per CDM, with a fixed 2,048-event state table. A full probe window evicts its least recently updated event, and evictions are counted |

Splitting by event matters. CDMs of one event are near-duplicates, so a per-CDM random split would leak test
events into training. Training is deterministic: rerunning the trainer reproduces the committed weights.

The LSTM does not decompose into per-feature contributions, so the audit log records the standardised inputs
and the logit. It sits behind envelope checks and the conservative fallback. Its weights have no compiled-in
default: if `ml/models/lstm_weights.txt` fails to load, OCAS refuses to start.

### 4.2 Conservative fallback

`RiskClassifier` uses the fallback whenever the in-domain model is outside its training envelope:

| Reason | Condition |
| :-- | :-- |
| `COV_MISSING` | covariance empty, non-finite or not positive |
| `COV_IMPLAUSIBLE` | sqrt(trace) above 1,000 km. 668 ESA rows qualify, with traces up to 2.4e16 m² |
| `VEL_OUT_OF_ENVELOPE` | v_rel outside 0 to 17.5 km/s |
| `REGIME_OUT_OF_DOMAIN` | host is not in LEO. The model never saw MEO or GEO data |

The fallback has three rules:

* **Usable covariance:** it acts if the isotropic estimate Pc ≈ R²/(2σ²)·exp(−d²/2σ²) is at least 1e-4.
  It uses R = 20 m and σ² = trace/3.
* **Close pass:** it always acts if d < 1 km.
* **Unusable covariance:** it screens on geometry alone and acts if d < 5 km. A diluted covariance drives
  Pc toward zero exactly when knowledge is worst, so geometry is the safer signal.

### 4.4 Regime strategies and evasion sizing

The factory maps altitude to a regime. LEO is below 2,000 km. GEO starts at 35,586 km, the lower
edge of the IADC 35,786 ± 200 km protected band. MEO lies between them. Each regime sizes the impulse with
the Clohessy-Wiltshire (CW) solution for an impulse Δv applied a lead time L before TCA:

| Regime | Plane, direction | Displacement gain G(L) | Preferred lead | Δv cap |
| :-- | :-- | :-- | :-- | :-- |
| LEO | along-track, +T | \|4 sin(nL)/n − 3L\| | 8 orbits | 1.0 m/s |
| MEO | radial, +R | √(sin²(nL) + 4(1 − cos nL)²) / n | ½ orbit | 0.5 m/s |
| GEO | cross-track, +N | \|sin(nL)\| / n | ¼ orbit | 0.5 m/s |

The target separation is the largest of the regime floor (1, 3 or 5 km), 2σ per axis, and 1.5 times
the current miss distance. The required offset is √(target² − d²). This assumes the offset is
orthogonal to the unknown miss vector, which is the conservative choice. Then Δv = offset / G(L).
An advisory over the Δv cap, or over the remaining Δv budget, is rejected and escalated to ground
instead of being staged.

Regime risk is a proximity term times a time-pressure term. LEO adds a kinetic-severity factor and
GEO adds a slow-encounter dwell factor. These weights are engineering heuristics, not trained values.

### 4.5 Advisory lifecycle

Each staged advisory is preceded by a memento checkpoint and reserves Δv from the budget. The OEM
FCU acknowledgement is simulated with `--abort-every N`. An external abort of advisory *k* unwinds
the LIFO stack to *k*'s checkpoint, which also undoes every advisory staged after *k*, because those
were sized against a state that included *k*. Everything else is committed. Each event lands in the
mission ledger and the audit log.

---

## 5. Build and run

Prerequisites: a C++17 compiler (GCC ≥ 9 or Clang ≥ 10), CMake ≥ 3.16, and Ninja or Make.

```bash
./run_ocas.sh                 # build, test, evaluate, run LEO / GEO / MEO / stress scenarios
```

Manual steps:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/bin/test_ocas
build/bin/ocas_flight_exec --evaluate
build/bin/ocas_flight_exec --altitude-km 550 --quiet     # result summary only
build/bin/ocas_flight_exec --altitude-km 550 --verbose   # every manoeuvre + latency, memory, ledger
build/bin/ocas_flight_exec --input data/synthetic/synthetic_geo_vectors.csv --altitude-km 35786 --batch 8 --abort-every 3
build/bin/ocas_flight_exec --help
```

Ground tools (trainers, dataset converters) need `-DOCAS_BUILD_TOOLS=ON`:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOCAS_BUILD_TOOLS=ON && cmake --build build
build/bin/train_lstm           # needs the raw ESA data/esa/train_data.csv
tools/fetch_public_data.sh
```

All targets compile with `-std=c++17 -Wall -Wextra -Werror -pedantic -O3`.

### 5.1 Python risk models

Two Python models give a risk level (*Nominal*, *Caution*, *High* or *Critical*) for one threat through
`predict_risk(miss, velocity, TCA, covariance)`:

| Model | Files | Notes |
| :-- | :-- | :-- |
| Random Forest (scikit-learn, 200 trees) | `ml/random_forest/predict.py` + `ml/models/random_forest.pkl` | The model file (110 MB) is not in git; get it from the team and save it as `ml/models/random_forest.pkl` |
| LSTM (PyTorch) | `ml/lstm/predict.py`, `ml/models/lstm_risk.pt` | Trained by `ml/lstm/train_lstm.py`; also accepts the alert ID to use the event's history |

The levels are defined on the provider Pc:

| Level | Pc |
| :-- | :-- |
| Nominal | < 1e-6 |
| Caution | 1e-6 to < 1e-5 |
| High | 1e-5 to < 1e-4 |
| Critical | ≥ 1e-4 |

Pc is the label only, never an input.

```bash
uv venv .venv_lstm --python 3.11
uv pip install --python .venv_lstm/bin/python -r ml/requirements.txt --extra-index-url https://download.pytorch.org/whl/cpu
.venv_lstm/bin/python ml/lstm/train_lstm.py     # ~30 s; needs data/esa/train_data.csv and test_data.csv
.venv_lstm/bin/python ml/random_forest/predict.py   # Random Forest test prediction
.venv_lstm/bin/python ml/analysis/pearson_correlation.py
.venv_lstm/bin/python ml/analysis/select_features_lstm.py   # ~2 min
```

Training is deterministic (seed 2026): rerunning it reproduces `ml/models/lstm_risk.pt` byte for byte. The
metrics are written to `results/python_lstm_metrics.txt`.

---

## 6. Results

These figures were measured on an AMD Ryzen 3 3250U with GCC 16.2 at `-O3`. Latency varies between runs.

**Offline evaluation.** The label is collision_prob > 1e-4, scored per CDM. There are 24,484 CDMs and 233 positives.

| Classifier | Precision | Recall | F1 |
| :-- | --: | --: | --: |
| Baseline: miss distance < 1 km | 0.066 | 0.609 | 0.120 |
| LSTM (trained for a different label, see below) | 0.053 | 0.408 | 0.094 |
| Conservative threshold | 0.063 | 0.609 | 0.114 |
| OCAS hybrid (LSTM + fallback) | 0.051 | 0.408 | 0.091 |

The LSTM catches 95 of 233 high-Pc CDMs (recall 0.408), while the 1 km baseline catches 142. This per-CDM label
is not the LSTM's training target, so the table understates it. See its own evaluation below.

The LSTM is scored on its own task by `train_lstm`. On 1,710 held-out events (49 high-risk), it uses each event's
history up to 2 days before TCA:

| Forecast of final-CDM risk | AP | Precision | Recall | F1 |
| :-- | --: | --: | --: | --: |
| Baseline: last miss distance < 1 km | 0.060 | 0.083 | 0.204 | 0.118 |
| LSTM over CDM history | 0.111 | 0.101 | 0.143 | 0.119 |
| Reference: latest log10 Pc ≥ −6 (**uses provider Pc**) | 0.479 | 0.500 | 0.898 | 0.642 |

The LSTM has the best AP of the models that do not see Pc, but its F1 matches the baseline. It is far below the
latest-Pc reference. The ESA challenge results (Uriot et al., 2022) found the same: the latest risk value is a
very strong predictor. OCAS keeps Pc out of its inputs on purpose, so that it can act when Pc is missing or degraded.

**Pipeline, full ESA stream, LEO host** (766 cycles, 24,484 CDMs, 10 m/s budget):

| In-domain model | Actionable CDMs | Directives | Committed | Rejected: Δv cap / budget | Duplicates suppressed | Score + heap insert, mean / p99 | Full cycle, p50 / p99 |
| :-- | --: | --: | --: | --: | --: | --: | --: |
| LSTM | 1,867 | 394 | 76 | 13 / 305 | 1,473 | 2.3–3.3 / 3.5–7.8 µs | 82 / 180 µs |

* Model routing: 23,810 CDMs used the LSTM and 674 used the fallback.
* With the LSTM, the budget runs out before the stream ends, so later threats are escalated
  rather than handled on board. Higher recall has a direct Δv cost. See limitation 4.
* Memory: zero `operator new` calls inside the loop. The fixed pipeline footprint is 875 KB, and the LSTM
  event-state table accounts for most of it.
* Latency ranges come from repeated runs. Full-cycle figures include audit-log flushes. Full-cycle p99 stays
  at or below 0.2 ms, and the worst single cycle seen was 0.8 ms, far inside any realistic CDM update rate.

---

## 7. Limitations and next steps

1. **Classifier recall.** The LSTM raises recall over a per-CDM linear model but its F1 only matches the 1 km
   baseline, so model capacity is not the bottleneck. The four inputs are. Promising directions are features
   the six-field schema drops (the individual covariance terms, RTN miss components), or a recall-targeted
   threshold chosen on validation data.
2. **Per-event evaluation.** The ESA challenge scores the *final* CDM of each event. Only `train_lstm` evaluates
   per event. The flight executable's `--evaluate` still scores per CDM.
3. **Physics scope.** CW assumes a circular orbit. J2, drag and third-body effects are not modeled.
   Burn sign is fixed positive because the six-field schema has no RTN miss components.
   The FCU must confirm direction against its own ephemeris.
4. **Single-host simulation.** The ESA events come from many different missions. Streaming them
   through one simulated spacecraft makes the Δv budget figures illustrative only. The higher-recall LSTM
   exhausts the budget in this setting, so the recall-versus-Δv trade-off is real, but its size is not.
5. **Duplicate handling.** A newer CDM for an already-advised event is suppressed rather than used to re-plan the advisory.
6. **Allocation check.** The check counts C++ `operator new` only. It does not see direct `malloc` calls inside the C library.

---

## 8. Repository layout

```
include/{core,ingestion,risk,priority,regime,persistence,pipeline,telemetry,ml}/   C++ engine headers
src/{core,risk,priority,regime,persistence,pipeline,telemetry,ml}/, src/main.cpp   C++ engine
tests/test_ocas.cpp      verification suite
ml/lstm/                 Python LSTM: train_lstm.py, predict.py, lstm_common.py
ml/lstm_cpp/             C++ LSTM trainer for the engine weights, run_ml_eval.sh
ml/random_forest/        Random Forest: predict.py
ml/analysis/             pearson_correlation.py, select_features_lstm.py
ml/models/               every trained model (lstm_weights.txt, lstm_risk*.pt, random_forest.pkl local only)
ml/requirements.txt      Python dependencies
tools/                   C++ dataset tools and fetch_public_data.sh
data/esa/                ESA Kelvins data (raw files git-ignored; cleaned_cdm_dataset.csv tracked)
data/synthetic/          synthetic MEO and GEO alerts
data/external/           CelesTrak, NASA CARA downloads, derived CSVs, fetch manifest
visualizer/              dashboard (index.html) and build_data.py
results/                 metric reports
docs/                    Phase-II report and slides
logs/                    runtime audit log (git-ignored)
```

---

## 9. References

1. CCSDS 508.0-B-1, *Conjunction Data Message*, Consultative Committee for Space Data Systems.
2. T. Uriot et al., "Spacecraft collision avoidance challenge: Design and results of a machine learning
   competition," *Astrodynamics*, 6, 121–140 (2022). This is the source of the ESA Kelvins dataset.
3. W. H. Clohessy and R. S. Wiltshire, "Terminal Guidance System for Satellite Rendezvous,"
   *Journal of the Aerospace Sciences*, 27(9), 653–658 (1960).
4. IADC Space Debris Mitigation Guidelines, Inter-Agency Space Debris Coordination Committee. This defines the GEO protected region.
5. ISO/IEC 14882:2017, *Programming Language C++*.
