# OCAS: Autonomous Orbital Collision Avoidance System
<<<<<<< HEAD
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
=======
**Project-Based Learning (PBL)**  
*Department of Computer Science & Engineering, Graphic Era (Deemed to be University), Dehradun*  
**Academic Session:** 2026–2027 | **Course Integration:** Data Structures in C & Object-Oriented Programming in C++

---

## 1. Executive Summary
The **Autonomous Orbital Collision Avoidance System (OCAS)** is a hard real-time, deterministic embedded software engine designed for Low-Earth Orbit (LEO) satellite platforms. As mega-constellations and orbital debris multiply, satellite operators face critical alert fatigue and narrow reaction windows. 

OCAS provides an onboard, zero-heap edge computing pipeline that ingests CCSDS Conjunction Data Messages (CDMs), evaluates collision probability through an explainable multi-parameter Logistic Regression classifier, ranks critical threats via a Binary Max-Heap, and autonomously executes thruster maneuvers using C++ OOP polymorphism and Tsiolkovsky’s rocket physics.

---

## 2. Core Project Contributors

| Roll Number | Name | Primary Role | Domain Responsibilities |
| :--- | :--- | :--- | :--- |
| **2510011013** | **Adamya Rawat** | **Team Lead & System Architect** | Core pipeline architecture, module interface contracts, edge ML risk scoring, and coordination. |
| **2510381135** | **Atiksh Sharma** | **Core Backend Engineer** | Low-level C zero-allocation buffers, FIFO alert queue, and Binary Max-Heap priority engine. |
| **2510385597** | **Agamya Saini** | **QA & Validation Engineer** | Test vector generation, fault-injection testbeds, logging validation, and problem analysis[cite: 1]. |
| **2510370197** | **Krishna Sehgal** | **Research & Documentation**[cite: 1] | CCSDS CDM dataset preprocessing, literature study, safety compliance, and documentation. |

* **Faculty Mentor:** Dr. Narayan Chaturvedi 
* **Group ID:** DSCPP-III-2026-T018

---

## 3. Key Features
* **Deterministic, Zero-Allocation Flight Memory:** Eliminates dynamic heap allocation (`malloc`/`free`) after initialization to completely avoid memory fragmentation and runtime crashes on flight computers[cite: 1].
* **Explainable Edge ML Risk Scoring:** Evaluates miss distance ($d_{\text{miss}}$), relative velocity ($v_{\text{rel}}$), time to closest approach ($t_{\text{TCA}}$), and positional covariance matrix uncertainty ($\sigma$) locally without ground station latency[cite: 1].
* **Priority Queue Ingestion:** Guarantees critical threats are immediately exposed at root in $\mathcal{O}(1)$ time using a Binary Max-Heap[cite: 1].
* **Polymorphic Thruster Arbitration:** Abstract C++ base class (`ThrusterEngine`) arbitrating Chemical, Ion, and Cold-Gas engines based on Tsiolkovsky's Rocket Equation and automatic fallback execution[cite: 1].
* **Dual-State Persistence:** In-memory LIFO stack for instantaneous abort/undo recovery alongside an in-memory Doubly Linked List for full bidirectional chronological mission tracking[cite: 1].

---

## 4. System Architecture & Data Flow

```
+-------------------------------------------------------------------------------+
|                             DATA INGESTION (C11)                              |
|   +--------------------------+          +---------------------------------+   |
|   | Space-Track CDMs / State | -------> | Static Circular Ring Buffer     |   |
|   | Telemetry (Fixed Struct) |          | (Zero-Heap Ingestion Queue)     |   |
|   +--------------------------+          +---------------------------------+   |
+---------------------------------------------------|---------------------------+
                                                    v
+-------------------------------------------------------------------------------+
|                          CORE LOGIC & SCORING LAYER                           |
|   +---------------------------------------+     +-------------------------+   |
|   | In-Place Logistic Regression Model    | --> | Binary Max-Heap         |   |
|   | Inputs: d_miss, v_rel, t_TCA, sigma   |     | Root: Highest Threat    |   |
|   | Output: Threat Score P in [0.0, 1.0]  |     | O(1) Peek, O(log n) Ins |   |
|   +---------------------------------------+     +-------------------------+   |
+---------------------------------------------------|---------------------------+
                                                    v
+-------------------------------------------------------------------------------+
|                       PROPULSION DISPATCHER (C++17 OOP)                       |
|                 +-------------------------------------------+                 |
|                 |       Base Class: ThrusterEngine          |                 |
|                 +-------------------------------------------+                 |
|                        /              |              \                        |
|                       v               v               v                       |
|              +-------------+   +-------------+   +-------------+              |
|              |  Chemical   |   |     Ion     |   |  Cold-Gas   |              |
|              | (High Thrust|   | (High Isp / |   |  (Micro RCS |              |
|              |  Fast Burn) |   |  Low Thrust)|   |   Pulsing)  |              |
|              +-------------+   +-------------+   +-------------+              |
|                       |               |               |                       |
|                       +-------+-------+---------------+                       |
|                               v                                               |
|               Tsiolkovsky Delta-V & Time Check                                |
|             [Automatic Fallback on Feasibility Fail]                          |
+---------------------------------------|---------------------------------------+
                                        v
+-------------------------------------------------------------------------------+
|                         PERSISTENCE & AUDIT LAYER                             |
|    +-------------------+    +--------------------+    +--------------------+  |
|    | LIFO State Stack  |    | Doubly Linked List |    | Append-Only Log    |  |
|    | (Abort Rollbacks) |    | (Mission Timeline) |    | (flight_audit.log) |  |
|    +-------------------+    +--------------------+    +--------------------+  |
+-------------------------------------------------------------------------------+
```


## 5. Technology Stack & Engineering Rationale

| Layer | Component / Tech | Rationale & Trade-offs |
| :--- | :--- | :--- |
| **Ingestion** | Pure C (C11)[cite: 1] | Microsecond-level determinism; completely bypasses memory leaks and fragmentation[cite: 1]. |
| **Hardware Abstraction** | C++17[cite: 1] | Object-Oriented Polymorphism cleanly models swappable thruster engines without runtime overhead[cite: 1]. |
| **Threat Prioritization** | Custom Binary Max-Heap[cite: 1] | Eliminates $\mathcal{O}(n \log n)$ array sorting stalls during alert bursts; provides instant $\mathcal{O}(1)$ top-threat peek[cite: 1]. |
| **Persistence** | In-Memory Structs[cite: 1] | Replaces heavy relational SQL databases to eliminate daemon overhead, disk latency, and IPC socket delays[cite: 1]. |
| **Build & Standards** | GCC & CMake[cite: 1] | Strict compilation (`-Wall -Wextra -Werror -pedantic`) and automated cross-compilation for embedded ARM targets[cite: 1]. |


## 6. Directory Structure
```
ocas-orbit-guard/
├── CMakeLists.txt              # Production root CMake configuration
├── run_ocas.sh                 # End-to-end execution script
├── include/
│   ├── c_ingestion.h           # Ring buffer, structs, FIFO definitions
│   ├── risk_engine.h           # Logistic Regression & Max-Heap prototypes
│   ├── thruster_engine.hpp     # Abstract ThrusterEngine & derived classes
│   └── persistence.hpp         # LIFO stack & Doubly Linked List headers
├── src/
│   ├── c_ingestion/
│   │   ├── ring_buffer.c       # Zero-allocation circular queue logic
│   │   └── telemetry_parser.c  # CCSDS CDM packet parsing
│   ├── cpp_engine/
│   │   ├── risk_classifier.cpp # Edge ML scoring logic
│   │   ├── max_heap.cpp        # Binary priority queue implementation
│   │   ├── thruster_impl.cpp   # Chemical, Ion, Cold-Gas controllers
│   │   └── persistence.cpp     # Stack rollback & timeline traversal
│   ├── main.cpp                # System pipeline orchestrator
│   └── visualizer/
│       ├── index.html          # Real-time WebGL / Three.js 3D dashboard
│       ├── app.js              # Telemetry WebSocket listener & orbit rendering
│       └── styles.css          # Mission control UI styling
└── data/
    └── test_cdm_vectors.json   # Simulated benchmark conjunction scenarios
```
## 7. Build, Compilation & Execution

### Prerequisites
* **Compiler:** GCC/G++ 11+ with support for C11 and C++17 standards.
* **Build System:** CMake (v3.16 or higher).
* **Environment:** POSIX/Linux (Ubuntu 22.04 LTS / Debian) or macOS with standard POSIX toolchains.
* **Optional:** Python 3.9+ with `scikit-learn` and `numpy` (for offline logistic regression coefficient training).

### Step-by-Step Build Instructions

```bash
# 1. Clone the project repository
git clone [https://github.com/Divyaj_vanguard/Orbital-Collision-Avoidance-System.git](https://github.com/Divyaj_vanguard/Orbital-Collision-Avoidance-System.git)
cd OCAS-Orbital-Collision-Avoidance-System

# 2. Create and enter an isolated build directory
mkdir -p build && cd build

# 3. Configure the CMake target with strict aerospace compiler flags
# (-Wall -Wextra -Werror treats all warnings as fatal errors to enforce safety)
cmake -DCMAKE_BUILD_TYPE=Release ..

# 4. Compile the binaries
make -j$(nproc)

# Option A: Run the compiled binary directly with test vectors
./bin/ocas_core ../data/benchmark_cdm_scenarios.json

# Option B: Run the automated simulation script (launches telemetry engine & UI)
cd ..
chmod +x run_ocas.sh
./run_ocas.sh
```
## 8. Development Roadmap & Milestones

* [x] **Phase-I: System Design & Static Ingestion (Completed - Current Evaluation)**
  * Requirement analysis modeling orbital conjunctions and timing constraints.
  * Preprocessed benchmark LEO conjunction telemetry from Space-Track and CelesTrak.
  * Static, zero-heap circular buffer and FIFO queue specification in C.
  * End-to-end prototype pipeline verified on terminal.
    
* [ ] **Phase-II: Core Engine & ML Integration (Upcoming)**
  * In-place Logistic Regression model integration with pre-trained coefficients.
  * Complete implementation of Binary Max-Heap risk ranking engine.
  * Polymorphic `ThrusterEngine` dispatcher with automated fallback arbitration.
  * LIFO rollback stack and doubly linked list history ledger integration.
    
* [ ] **Phase-III: Validation, Benchmarking & Visualizer (Final Delivery)**
  * Fault-injection suite (simulating telemetry queue saturation and mid-burn thruster dropouts).
  * Valgrind and AddressSanitizer audit to verify zero dynamic heap leaks.
  * Benchmarking decision latency to achieve sub-millisecond execution.
  * Interactive WebGL / 3D orbital trajectory dashboard integration.

---

## 9. Evaluation Metrics & Verification Targets

| Metric | Target Specification | Validation Method |
| :--- | :--- | :--- |
| **High-Risk Prioritization Rate** | $\ge 90\%$ critical threats prioritized | Scored against CelesTrak historical collision near-misses. |
| **Ingestion Heap Allocation** | Exactly **0 bytes** post-boot | Memory profiling via Valgrind Massif tool. |
| **Decision Latency** | $\le 1.0\text{ ms}$ from alert ingest to burn vector | High-resolution CPU cycle benchmarking (`clock_gettime`). |
| **Thruster Fallback Reliability** | $100\%$ safe recovery during thruster failure | Automated fault-injection suite during burn dispatch. |
| **Rollback Integrity** | O(1) instant state restoration | LIFO stack pop verification upon maneuver abort flag. |

---

## 10. References & Standards

1. **CCSDS 508.0-B-1:** *Conjunction Data Message (CDM)* Blue Book Recommended Standard, Consultative Committee for Space Data Systems.
2. **MISRA C:2012:** *Guidelines for the use of the C language in critical systems* (Rules 11.4, 21.3 regarding dynamic allocation).
3. **ISO/IEC 14882:2017:** *Standard for Programming Language C++ (C++17)*.
4. **CelesTrak & Space-Track Archives:** *Public Conjunction Assessment and Two-Line Element (TLE) Data Sets*.
5. **Tsiolkovsky, K. E.:** *The Exploration of Cosmic Space by Means of Reaction Devices* (Ideal Rocket Equation formulation for propulsion arbitration).

>>>>>>> e49fb1a59fde1e04fee2ebc7a240a2933630578f
