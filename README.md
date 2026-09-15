# OCAS: Autonomous Orbital Collision Avoidance System
**Project-Based Learning (PBL) — Phase-I Evaluation**  
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
| **2510370197**[cite: 1] | **Krishna Sehgal**[cite: 1] | **Research & Documentation**[cite: 1] | CCSDS CDM dataset preprocessing, literature study, safety compliance, and documentation[cite: 1]. |

* **Faculty Mentor:** Dr. Narayan Chaturvedi[cite: 1]  
* **Group ID:** DSCPP-III-2026-T018[cite: 1]

---

## 3. Key Features
* **Deterministic, Zero-Allocation Flight Memory:** Eliminates dynamic heap allocation (`malloc`/`free`) after initialization to completely avoid memory fragmentation and runtime crashes on flight computers[cite: 1].
* **Explainable Edge ML Risk Scoring:** Evaluates miss distance ($d_{\text{miss}}$), relative velocity ($v_{\text{rel}}$), time to closest approach ($t_{\text{TCA}}$), and positional covariance matrix uncertainty ($\sigma$) locally without ground station latency[cite: 1].
* **Priority Queue Ingestion:** Guarantees critical threats are immediately exposed at root in $\mathcal{O}(1)$ time using a Binary Max-Heap[cite: 1].
* **Polymorphic Thruster Arbitration:** Abstract C++ base class (`ThrusterEngine`) arbitrating Chemical, Ion, and Cold-Gas engines based on Tsiolkovsky's Rocket Equation and automatic fallback execution[cite: 1].
* **Dual-State Persistence:** In-memory LIFO stack for instantaneous abort/undo recovery alongside an in-memory Doubly Linked List for full bidirectional chronological mission tracking[cite: 1].

---

## 4. System Architecture & Data Flow
