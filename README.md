# OCAS: Autonomous Orbital Collision Avoidance System
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
git clone [https://github.com/AdamyaRawat/OCAS-Orbital-Collision-Avoidance-System.git](https://github.com/AdamyaRawat/OCAS-Orbital-Collision-Avoidance-System.git)
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

