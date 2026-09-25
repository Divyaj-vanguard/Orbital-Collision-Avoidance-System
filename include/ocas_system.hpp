/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Unified System Coordinator (C++17)
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 */

#ifndef OCAS_SYSTEM_HPP
#define OCAS_SYSTEM_HPP

#include "c_ingestion.h"
#include "risk_engine.hpp"
#include "thruster_engine.hpp"
#include "persistence.hpp"
#include <string>

namespace ocas {

/**
 * @brief Autonomous Orbital Collision Avoidance System Coordinator.
 * Bridges C11 deterministic ingestion buffers with C++17 risk scoring,
 * polymorphic propulsion dispatching, and audit persistence.
 */
class OCASSystem {
public:
    explicit OCASSystem(const std::string& audit_log_path = "logs/flight_audit.log");
    ~OCASSystem() = default;

    // Disallow copies for safety
    OCASSystem(const OCASSystem&) = delete;
    OCASSystem& operator=(const OCASSystem&) = delete;

    /**
     * @brief Boots system: zero-alloc initialization of C11 buffers and subsystems.
     */
    void initialize(const SatelliteState& initial_state);

    /**
     * @brief Ingests continuous telemetry frame into C11 ring buffer.
     */
    bool ingest_telemetry(const SatelliteState& state);

    /**
     * @brief Ingests new CDM alert into C11 FIFO queue.
     */
    bool ingest_cdm(const CDMAlert& alert);

    /**
     * @brief Evaluates all queued CDMs with edge ML and populates the Max-Heap.
     * @return Number of threats processed and inserted.
     */
    size_t process_and_prioritize_threats();

    /**
     * @brief Autonomous avoidance cycle for the highest-priority threat in Max-Heap.
     * Performs pre-burn LIFO snapshot, Tsiolkovsky fallback arbitration,
     * burn execution, and ledger persistence.
     * @return true if threat mitigated or nominal pass; false if abort/infeasible.
     */
    bool execute_top_threat_avoidance(bool simulate_anomaly_on_primary = false);

    /**
     * @brief Triggers an emergency abort and rolls back spacecraft state to the
     * last pre-maneuver snapshot on the LIFO stack.
     */
    bool trigger_abort_rollback();

    // Telemetry and subsystem inspectors
    [[nodiscard]] const SatelliteState& get_current_state() const noexcept { return current_state_; }
    [[nodiscard]] const TelemetryRingBuffer& get_ring_buffer() const noexcept { return ring_buffer_; }
    [[nodiscard]] const CDMFifoQueue& get_cdm_fifo() const noexcept { return cdm_fifo_; }
    [[nodiscard]] const MaxHeapPriorityQueue& get_max_heap() const noexcept { return max_heap_; }
    [[nodiscard]] const StateStack& get_rollback_stack() const noexcept { return rollback_stack_; }
    [[nodiscard]] const ManeuverLedger& get_mission_ledger() const noexcept { return mission_ledger_; }
    [[nodiscard]] ThrusterDispatcher& get_dispatcher() noexcept { return dispatcher_; }

    // Aliases for compatibility
    [[nodiscard]] const BinaryMaxHeap& get_heap() const noexcept { return max_heap_; }
    [[nodiscard]] const LIFOStateStack& get_stack() const noexcept { return rollback_stack_; }
    [[nodiscard]] const MissionLedger& get_ledger() const noexcept { return mission_ledger_; }

    /**
     * @brief Serializes current mission telemetry, heap items, and ledger to JSON.
     */
    [[nodiscard]] std::string export_telemetry_json() const;

private:
    SatelliteState current_state_;
    TelemetryRingBuffer ring_buffer_;
    CDMFifoQueue cdm_fifo_;
    LogisticRiskClassifier classifier_;
    MaxHeapPriorityQueue max_heap_;
    ThrusterDispatcher dispatcher_;
    StateStack rollback_stack_;
    ManeuverLedger mission_ledger_;
    FlightAuditLogger logger_;
};

} // namespace ocas

#endif /* OCAS_SYSTEM_HPP */
