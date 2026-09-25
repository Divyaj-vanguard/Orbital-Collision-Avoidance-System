/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Persistence & Audit Layer (C++17)
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Features:
 *  - Encapsulated StateStack (LIFO rollback stack) for instant O(1) state recovery
 *  - Encapsulated ManeuverLedger: Doubly Linked List with RAII destructor deallocating nodes
 *  - Append-Only structured JSON Logger for flight telemetry and audits
 * ============================================================================
 */

#ifndef OCAS_PERSISTENCE_HPP
#define OCAS_PERSISTENCE_HPP

#include "c_ingestion.h"
#include "thruster_engine.hpp"
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

namespace ocas {

/**
 * @brief Pre-maneuver state snapshot for instant rollback.
 */
struct StateSnapshot {
    SatelliteState state{};
    uint64_t snapshot_epoch_s = 0;
    uint32_t trigger_alert_id = 0;
    char context_tag[32] = {};
};

/**
 * @brief Encapsulated LIFO State Stack for flight maneuver aborts and rollback.
 * Statically allocated array, O(1) operations, zero heap footprint.
 */
class StateStack {
public:
    static constexpr size_t MAX_STACK_DEPTH = 32;

    StateStack() noexcept;

    // Disallow copies for state safety
    StateStack(const StateStack&) = delete;
    StateStack& operator=(const StateStack&) = delete;
    StateStack(StateStack&&) noexcept = default;
    StateStack& operator=(StateStack&&) noexcept = default;

    /**
     * @brief Pushes current satellite state onto rollback stack. Complexity: O(1).
     */
    bool push_snapshot(const SatelliteState& state, uint32_t alert_id = 0, const char* context = "PRE_MANEUVER") noexcept;
    bool push(const SatelliteState& state, uint32_t alert_id = 0, const char* context = "PRE_MANEUVER") noexcept {
        return push_snapshot(state, alert_id, context);
    }

    /**
     * @brief Restores pre-maneuver state from the stack. Complexity: O(1).
     */
    bool pop_and_rollback(SatelliteState& out_state) noexcept;
    bool pop(SatelliteState& out_state) noexcept {
        return pop_and_rollback(out_state);
    }

    /**
     * @brief Peeks at topmost snapshot without popping. Complexity: O(1).
     */
    bool peek_state(SatelliteState& out_state) const noexcept;
    bool peek(SatelliteState& out_state) const noexcept {
        return peek_state(out_state);
    }

    [[nodiscard]] size_t size() const noexcept { return top_; }
    [[nodiscard]] static constexpr size_t capacity() noexcept { return MAX_STACK_DEPTH; }
    [[nodiscard]] bool is_empty() const noexcept { return top_ == 0; }
    [[nodiscard]] bool is_full() const noexcept { return top_ >= MAX_STACK_DEPTH; }
    void clear() noexcept { top_ = 0; }

private:
    StateSnapshot stack_[MAX_STACK_DEPTH];
    size_t top_;
};

// Backward-compatible alias
using LIFOStateStack = StateStack;

/**
 * @brief Record of a completed or aborted collision avoidance maneuver.
 */
struct ManeuverRecord {
    uint32_t maneuver_id = 0;
    uint32_t target_object_id = 0;
    char target_name[OCAS_NAME_LEN] = {};
    double delta_v_target_ms = 0.0;
    double delta_v_actual_ms = 0.0;
    double propellant_used_kg = 0.0;
    double burn_duration_s = 0.0;
    double initial_fuel_kg = 0.0;
    double final_fuel_kg = 0.0;
    std::string engine_used = "UNKNOWN";
    double risk_score_mitigated = 0.0;
    uint64_t execution_epoch_s = 0;
    bool abort_rollback_occurred = false;
};

/**
 * @brief Doubly Linked List Node for Maneuver Records.
 */
struct ManeuverNode {
    ManeuverRecord data;
    ManeuverNode* prev = nullptr;
    ManeuverNode* next = nullptr;
};

/**
 * @brief Audit statistics aggregated from the mission ledger.
 */
struct LedgerAuditSummary {
    size_t total_maneuvers = 0;
    double cumulative_delta_v_ms = 0.0;
    double cumulative_propellant_kg = 0.0;
    double cumulative_burn_time_s = 0.0;
    size_t chemical_burn_count = 0;
    size_t ion_burn_count = 0;
    size_t cold_gas_burn_count = 0;
    size_t abort_rollback_count = 0;
};

/**
 * @brief Encapsulated Doubly Linked List Mission Ledger with RAII memory management.
 * Provides chronological forward tracking and reverse mission auditing.
 */
class ManeuverLedger {
public:
    ManeuverLedger() noexcept;
    ~ManeuverLedger();

    // RAII Rule of 5: Disallow copy, allow move
    ManeuverLedger(const ManeuverLedger&) = delete;
    ManeuverLedger& operator=(const ManeuverLedger&) = delete;
    ManeuverLedger(ManeuverLedger&& other) noexcept;
    ManeuverLedger& operator=(ManeuverLedger&& other) noexcept;

    /**
     * @brief Appends maneuver record to tail. Complexity: O(1).
     */
    void append_maneuver(const ManeuverRecord& record);
    bool append(const ManeuverRecord& record);

    /**
     * @brief Forward chronological traversal.
     */
    void traverse_forward(const std::function<void(const ManeuverRecord&)>& callback) const;
    void traverse_forward(const std::function<void(const ManeuverRecord&, size_t index)>& callback) const;

    /**
     * @brief Backward reverse chronological traversal (auditing).
     */
    void traverse_backward(const std::function<void(const ManeuverRecord&)>& callback) const;
    void traverse_backward(const std::function<void(const ManeuverRecord&, size_t rev_index)>& callback) const;

    /**
     * @brief Aggregates cumulative audit metrics.
     */
    [[nodiscard]] LedgerAuditSummary compute_audit_summary() const noexcept;

    [[nodiscard]] size_t size() const noexcept { return count_; }
    [[nodiscard]] bool is_empty() const noexcept { return count_ == 0; }
    void clear() noexcept;

private:
    ManeuverNode* head_;
    ManeuverNode* tail_;
    size_t count_;
    uint32_t next_maneuver_id_;
};

// Backward-compatible alias
using MissionLedger = ManeuverLedger;

/**
 * @brief Append-Only Flight Audit Logger.
 * Writes structured JSON log records to logs/flight_audit.log.
 */
class FlightAuditLogger {
public:
    explicit FlightAuditLogger(const std::string& log_filepath = "logs/flight_audit.log");
    ~FlightAuditLogger();

    // Disallow copies
    FlightAuditLogger(const FlightAuditLogger&) = delete;
    FlightAuditLogger& operator=(const FlightAuditLogger&) = delete;

    void log_event(std::string_view event_type,
                   const SatelliteState* state,
                   const CDMAlert* alert,
                   const BurnSolution* solution,
                   bool execution_success,
                   std::string_view extra_notes);

    void log_event(std::string_view event_type,
                   const SatelliteState* state,
                   const CDMAlert* alert,
                   const BurnSolution* solution,
                   std::string_view extra_notes) {
        log_event(event_type, state, alert, solution, solution != nullptr && solution->feasible, extra_notes);
    }

    void log_raw_json(std::string_view json_str);

    [[nodiscard]] const std::string& get_filepath() const noexcept { return filepath_; }
    void flush();

private:
    std::string filepath_;
    std::ofstream log_stream_;
};

} // namespace ocas

#endif /* OCAS_PERSISTENCE_HPP */
