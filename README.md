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
 CDM CSV ──► inbox (64, drops oldest) ──► assess_risk() ──► regime_risk() ──► priority heap
 (ground                                   │ LSTM (in-domain)                    (std::push_heap,
  uplink)                                  └ conservative rule (fallback)         ranks_below)
                                                                                      │
  Recorder: audit log + terminal  ◄──  stage / commit / abort  ◄──  plan_evasion()  ◄─┘
                                       (budget restored on abort)    LEO / MEO / GEO
```

| Step | Files | How |
| :-- | :-- | :-- |
| Types and CSV I/O | `include/types.hpp`, `include/cdm_io.hpp`, `src/cdm_io.cpp` | Plain structs and enums; strict six-field CSV parser |
| Risk | `include/risk.hpp`, `src/risk.cpp`, `include/neural.hpp`, `src/model_io.cpp` | `select_model()` decides LSTM or fallback and records *why*; `LstmModel` keeps per-event state in a fixed open-addressing table; `conservative_assess()` is the rule-based fallback |
| Regime and advisory | `include/regime.hpp`, `src/regime.cpp` | One `Regime` parameter table per LEO / MEO / GEO; `regime_risk()` and `plan_evasion()` switch on the regime |
| Pipeline | `include/pipeline.hpp`, `src/pipeline.cpp` | Inbox and heap are `std::vector`s reserved at init; the heap uses `std::push_heap` / `std::pop_heap`; each staged advisory remembers the budget before it, so an abort restores it directly |
| Output | `include/recorder.hpp`, `src/recorder.cpp` | One `Recorder` writes the JSON-lines audit log and the terminal table |
| Executive | `src/main.cpp`, `src/alloc_tracker.cpp` | CLI, offline evaluation, cycle loop, summary, allocation counter |

### Real-time guarantees

* **No heap allocation inside the cyclic loop.** The inbox and heap are reserved at init and every other
  container the loop touches has a fixed size. `src/alloc_tracker.cpp` replaces global `operator new` in the
  flight executable and the run prints the count of allocations between the first and last cycle. The
  executable exits with status 3 if that count is not zero. Allocation happens only at init.
* **Bounded work per operation.** Inbox push O(64) worst case, heap push/pop O(log n), risk scoring O(1),
  staging and abort O(16).
* **No silent data loss.** Dropped inbox CDMs are counted and reported, and every advisory is in the audit log.

---

## 3. Data

| File | Rows | Origin |
| :-- | --: | :-- |
| `data/esa/cleaned_cdm_dataset.csv` | 24,484 | **Real.** ESA Kelvins Collision Avoidance Challenge test set, 2,167 events, reduced by `tools/compact_cdm_dataset` |
| `data/synthetic/synthetic_meo_vectors.csv` | 48 | **Synthetic.** `tools/generate_synthetic_vectors`, seed 2026 |
| `data/synthetic/synthetic_geo_vectors.csv` | 48 | **Synthetic.** same tool |
| `models/lstm_weights.txt` | – | Output of `tools/train_lstm`, same split and seed |

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

To regenerate the inputs from the raw ESA download (`data/esa/test_data.csv`) and the synthetic files:

```bash
build/bin/compact_cdm_dataset data/esa/test_data.csv data/esa/cleaned_cdm_dataset.csv
build/bin/generate_synthetic_vectors data/synthetic
```

The regenerated CSV holds full-precision numbers, while the committed one was written rounded
(for example `591157.0` instead of `591156.99000000011`). The engine's results are identical on both.

---

## 4. Models and physics

### 4.1 LSTM model (in-domain)

The LSTM runs in the same C++ as the flight loop, with no ML runtime. It is trained by
`tools/train_lstm` and loaded from a plain-text weight file at init.
Its inputs are miss distance, relative speed, time to TCA and covariance trace (log-scaled), standardised with the training-split mean and standard deviation.
It does not use Pc as an input.

| | LSTM |
| :-- | :-- |
| Shape | 4 → LSTM(16) → 1 |
| Task | per event: will the **final** CDM have log10 Pc ≥ −6? (ESA challenge definition) |
| Input | the event's CDM history so far, keyed by `alert_id` |
| Split | by event, 70 / 15 / 15, seed 2026, using only CDMs ≥ 2 days before TCA |
| Threshold | F1-optimal on validation, never on test |
| Cost | O(1) per CDM, with a fixed 2,048-event state table. A full probe window evicts its least recently updated event |

Splitting by event matters. CDMs of one event are near-duplicates, so a per-CDM random split would leak test
events into training. Training is deterministic: rerunning the trainer reproduces the committed weights.

The LSTM does not decompose into per-feature contributions, so the audit log records its logit. It sits behind envelope checks and the conservative fallback. Its weights have no compiled-in
default: if `models/lstm_weights.txt` fails to load, OCAS refuses to start.

### 4.2 Conservative fallback

OCAS uses the fallback whenever the in-domain model is outside its training envelope:

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

### 4.3 Regimes and evasion sizing

`make_regime()` maps altitude to a regime. LEO is below 2,000 km. GEO starts at 35,586 km, the lower
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

### 4.4 Advisory lifecycle

Each staged advisory records the Δv budget before it and reserves Δv from the budget. The OEM
FCU acknowledgement is simulated with `--abort-every N`. An external abort of advisory *k* restores
the budget from before *k*, which also undoes every advisory staged after *k*, because those
were sized against a budget that included *k*. Everything else is committed. Each event lands in the
audit log.

---

## 5. Build and run

Prerequisites: a C++17 compiler (GCC ≥ 9 or Clang ≥ 10) and CMake ≥ 3.16.

```bash
./run_ocas.sh                 # build, test, evaluate, run LEO / GEO / MEO / stress scenarios
```

Manual steps:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/bin/test_ocas
build/bin/ocas_flight_exec --evaluate
build/bin/ocas_flight_exec --altitude-km 550 --quiet     # result summary only (fuel, latency, allocations)
build/bin/ocas_flight_exec --input data/synthetic/synthetic_geo_vectors.csv --altitude-km 35786 --batch 8 --abort-every 3
build/bin/ocas_flight_exec --help
```

The same build makes the offline tools:

```bash
build/bin/train_lstm               # needs the raw ESA data/esa/train_data.csv; ~10 s
build/bin/train_lstm --gradcheck   # checks the backpropagation maths against finite differences
```

All targets compile with `-std=c++17 -Wall -Wextra -Werror -pedantic -O3`.

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
* Memory: zero `operator new` calls inside the loop. The pipeline uses about 580 KB of fixed memory; the LSTM event-state table (555 KB) is
  most of it.
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

## 8. Repository layout and reading order

Read the engine in this order; each file builds only on the ones above it.

```
include/types.hpp        every struct and enum the engine passes around
include/cdm_io.hpp       src/cdm_io.cpp      read and write the six-field CDM CSV
include/neural.hpp       src/model_io.cpp    LSTM features, one LSTM step, weight file load/save
include/risk.hpp         src/risk.cpp        conservative rule, model selection, LstmModel
include/regime.hpp       src/regime.cpp      LEO/MEO/GEO parameters, regime risk, burn sizing (CW)
include/recorder.hpp     src/recorder.cpp    audit log (JSON lines) and terminal table
include/pipeline.hpp     src/pipeline.cpp    one cycle: score -> rank -> stage -> commit or abort
src/main.cpp                                 command line, --evaluate, the cycle loop, summary
src/alloc_tracker.cpp                        counts heap allocations to prove the loop makes none
tests/test_ocas.cpp                          self-test
tools/train_lstm.cpp                         trains models/lstm_weights.txt (backpropagation through time)
tools/compact_cdm_dataset.cpp                raw ESA file -> data/esa/cleaned_cdm_dataset.csv
tools/generate_synthetic_vectors.cpp         data/synthetic/*.csv
models/lstm_weights.txt                      trained LSTM weights
data/esa/                                    ESA Kelvins data (raw files git-ignored)
data/synthetic/                              synthetic MEO and GEO alerts
run_ocas.sh                                  build, test and run every scenario
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
