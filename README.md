# OCAS: Autonomous Orbital Collision Avoidance System
**Team ID**: `DSCPP-III-2026-T018`  
**Institution**: Graphic Era University  
**Architecture**: Pure C11 (Data Structures) + C++17 (Object-Oriented Physics & Control Engine) + Three.js/WebGL (Interactive Ground Control Visualizer)

---

## 1. System Overview & Architecture

OCAS (Autonomous Orbital Collision Avoidance System) is a safety-critical aerospace flight software system designed to automate spacecraft conjunction assessment and evasion. It guarantees **zero dynamic heap allocation** during execution loops, **deterministic $\mathcal{O}(1)$ / $\mathcal{O}(\log n)$ runtime bounds**, **100% explainable edge AI**, and **aerospace fault tolerance**.

```
[CDMs & Satellite Telemetry Stream]
                 │
                 ▼
[C11 Static Circular Ring Buffer & Ingestion FIFO Queue]
├── 64-byte Cache-Aligned Structures (SatelliteState, CDMAlert)
├── Overwrite-Oldest Telemetry Policy (Zero Malloc/Free)
└── Drop-Newest FIFO Queue Protection
                 │
                 ▼
[Explainable Edge ML Risk Engine]
├── In-Place Logistic Regression Model (Features: d_miss, v_rel, t_TCA, Tr(Sigma))
├── Constant-time O(1) closed-form logit calculation
└── Discrete Risk Categorization (CRITICAL, HIGH, MODERATE, LOW)
                 │
                 ▼
[Custom Embedded Binary Max-Heap Priority Queue]
├── Zero STL container overhead (Fixed raw array implementation)
├── O(1) peek of highest-risk threat at root
└── O(log n) threat insertion and extraction
                 │
                 ▼
[C++17 Polymorphic Propulsion Dispatcher]
├── Base Class: ThrusterEngine
├── Concrete Derived Classes:
│   ├── ChemicalThruster   (400 N,  310 s Isp)
│   ├── IonThruster        (0.1 N, 3200 s Isp)
│   └── ColdGasThruster    (10 N,    70 s Isp)
└── Tsiolkovsky Gate & Automated Fallback Arbitration
    ├── Delta-m = m0 * (1 - e^(-dV / (Isp * g0)))
    ├── Burn Window Check: t_burn < t_TCA
    └── Automated Fallback on Subsystem Health Faults
                 │
                 ▼
[Persistence & Flight Audit Layer]
├── LIFO State Stack (Instant O(1) pre-maneuver abort rollback)
├── Static-Pool Doubly Linked List (Bidirectional chronological mission ledger)
└── Append-Only Structured JSON Logger (logs/flight_audit.log)
                 │
                 ▼
[Interactive 3D WebGL Dashboard & Operations Station]
├── Real-time 3D Earth, Orbit Trails, Conjunction Rays, and 3D Covariance Ellipsoids
├── Live Telemetry HUD, MET Clock, Fuel Gauge, and Max-Heap Tree Monitor
└── Interactive Fault Injection (Valve Fault, Debris Surge, LIFO Rollback)
```

---

## 2. Directory Structure

```
.
├── CMakeLists.txt              # Strict root CMake build configuration (-Wall -Wextra -Werror -pedantic)
├── Makefile                    # Native GNU Makefile with static archives
├── README.md                   # System documentation & architectural reference
├── run_ocas.sh                 # Unified master build, test, and visualizer runner
├── include/
│   ├── c_ingestion.h           # Pure C11 cache-aligned structs, ring buffer & FIFO queue APIs
│   ├── risk_engine.hpp         # In-place Logistic Regression model & custom Binary Max-Heap
│   ├── thruster_engine.hpp     # Polymorphic propulsion dispatcher, engines & Tsiolkovsky physics
│   ├── persistence.hpp         # LIFO State Stack, Doubly Linked List Ledger, JSON Logger
│   └── ocas_system.hpp         # Unified OCAS flight software coordinator
├── src/
│   ├── c_ingestion/
│   │   ├── ring_buffer.c       # C11 circular ring buffer (overwrite-oldest)
│   │   └── fifo_queue.c        # C11 static FIFO queue (bounded drop-on-full)
│   ├── cpp_engine/
│   │   ├── risk_engine.cpp     # Logistic regression evaluation & heap sift up/down
│   │   ├── thruster_engine.cpp # Tsiolkovsky propellant mass, burn time, fallback arbitration
│   │   ├── persistence.cpp     # Pre-maneuver snapshot stack, static node pool ledger, logger
│   │   └── ocas_system.cpp     # Autonomous avoidance cycle & JSON export
│   ├── main.cpp                # Flight Executive demonstration & CLI
│   └── tests/
│       └── test_ocas.cpp       # 100% pass verification test suite
├── data/
│   ├── generate_cdms.py        # CCSDS Conjunction Data Message synthesizer
│   ├── sample_cdms.json        # Generated test encounter callsets
│   └── sample_encounter.cdm    # CCSDS KVN formatted encounter file
├── logs/
│   └── flight_audit.log        # Append-only structured JSON flight audit log
├── scripts/
│   └── server.py               # Lightweight simulation HTTP server & visualizer bridge
├── visualizer/
│   ├── index.html              # Modern 3D WebGL Ground Control Dashboard
│   ├── css/
│   │   └── style.css           # Mission control styling & HUD components
│   └── js/
│       ├── orbit_3d.js         # Three.js 3D Earth, orbit, covariance ellipsoid visualizer
│       └── app.js              # Telemetry sync, heap table rendering, fault injection controls
└── Phase-1_report/
    └── PROJECT-BASED LEARNING.pptx # Preserved academic presentation
```

---

## 3. Mathematical & Algorithmic Formulation

### 3.1 Explainable Edge ML: Logistic Regression Risk Model
The model evaluates collision probability $P_{\text{col}} \in [0.0, 1.0]$ in constant time $\mathcal{O}(1)$ using 4 normalized aerospace features:
1. **Normalized Miss Distance Factor**:
   $$f_D = 1.0 - \frac{d_{\text{miss}}}{d_{\text{miss}} + d_{\text{ref}}} \quad (d_{\text{ref}} = 3000.0\text{ m})$$
2. **Normalized Relative Velocity Factor**:
   $$f_V = \frac{v_{\text{rel}}}{v_{\text{rel}} + v_{\text{ref}}} \quad (v_{\text{ref}} = 5.0\text{ km/s})$$
3. **Normalized Temporal Urgency Factor**:
   $$f_T = \exp\left(-\frac{t_{\text{TCA}}}{\tau_{\text{ref}}}\right) \quad (\tau_{\text{ref}} = 600.0\text{ s})$$
4. **Normalized Positional Uncertainty (Covariance Trace)**:
   $$\sigma_{\text{eff}} = \sqrt{\mathrm{Tr}(\sigma)} = \sqrt{\sigma_{xx} + \sigma_{yy} + \sigma_{zz}}, \quad f_\Sigma = \frac{\sigma_{\text{eff}}}{\sigma_{\text{eff}} + \sigma_{\text{ref}}} \quad (\sigma_{\text{ref}} = 200.0\text{ m})$$

**Logit Evaluation & Sigmoidal Transfer**:
$$z = w_0 + w_D f_D + w_V f_V + w_T f_T + w_\Sigma f_\Sigma$$
$$P_{\text{col}} = \frac{1}{1 + e^{-z}}$$

*Weights*: $w_0 = -4.20$, $w_D = +4.50$, $w_V = +2.50$, $w_T = +3.20$, $w_\Sigma = +1.60$.

---

### 3.2 Propulsion Physics & Tsiolkovsky Rocket Equation
- **Exhaust Velocity**:
  $$v_e = I_{\text{sp}} \cdot g_0 \quad (g_0 = 9.80665\text{ m/s}^2)$$
- **Propellant Mass Required**:
  $$\Delta m = m_0 \cdot \left(1 - \exp\left(-\frac{\Delta V}{I_{\text{sp}} \cdot g_0}\right)\right)$$
- **Propellant Mass Flow Rate**:
  $$\dot{m} = \frac{F_{\text{thrust}}}{I_{\text{sp}} \cdot g_0}$$
- **Required Burn Time**:
  $$t_{\text{burn}} = \frac{\Delta m}{\dot{m}} = \frac{m_0 \cdot I_{\text{sp}} \cdot g_0 \cdot \left(1 - \exp\left(-\frac{\Delta V}{I_{\text{sp}} \cdot g_0}\right)\right)}{F_{\text{thrust}}}$$

**Automated Fallback Rule**:
If an engine is faulted or $\Delta m > m_{\text{fuel}}$ or $t_{\text{burn}} \ge t_{\text{TCA}}$ (cannot finish burn before closest approach), the dispatcher automatically falls back sequentially to the next viable thruster in the chain:
$$\text{Chemical} \longrightarrow \text{Ion} \longrightarrow \text{Cold Gas}$$

---

## 4. Build & Execution Instructions

### Prerequisites
- Compilers: `gcc` (C11 support) and `g++` (C++17 support)
- Utilities: `make` (GNU Make), `python3` (v3.8+)

### One-Click Master Execution
```bash
./run_ocas.sh
```
This script will:
1. Validate compiler tools.
2. Compile both C11 static library and C++17 engine binaries with strict flags (`-Wall -Wextra -Werror -pedantic`).
3. Run the complete automated verification test suite (`./bin/test_ocas`).
4. Synthesize 15 realistic CCSDS Conjunction Data Messages.
5. Execute the end-to-end flight software simulation demonstration.
6. Launch the interactive 3D Web Visualizer at `http://localhost:8080`.

### Running Verification Tests Only
```bash
make test
```

### Running Flight Executive Simulation in Terminal
```bash
make run
```

### Starting 3D Web Visualizer Ground Control Station
```bash
python3 scripts/server.py 8080
```
Open `http://localhost:8080` in your web browser.
