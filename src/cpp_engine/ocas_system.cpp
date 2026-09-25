/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Unified System Coordinator Implementation
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 */

#include "ocas_system.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace ocas {

OCASSystem::OCASSystem(const std::string& audit_log_path)
    : current_state_{},
      classifier_(),
      max_heap_(),
      dispatcher_(),
      rollback_stack_(),
      mission_ledger_(),
      logger_(audit_log_path) {
    ring_buffer_init(&ring_buffer_);
    cdm_fifo_init(&cdm_fifo_);
}

void OCASSystem::initialize(const SatelliteState& initial_state) {
    current_state_ = initial_state;
    ring_buffer_init(&ring_buffer_);
    cdm_fifo_init(&cdm_fifo_);
    max_heap_.clear();
    rollback_stack_.clear();
    mission_ledger_.clear();

    ring_buffer_push(&ring_buffer_, &current_state_);

    logger_.log_event("SYSTEM_BOOT_INITIALIZED",
                      &current_state_,
                      nullptr,
                      nullptr,
                      true,
                      "Zero-heap static buffers initialized. Flight state nominal.");
}

bool OCASSystem::ingest_telemetry(const SatelliteState& state) {
    current_state_ = state;
    bool fresh = ring_buffer_push(&ring_buffer_, &state);

    logger_.log_event("TELEMETRY_INGESTED",
                      &current_state_,
                      nullptr,
                      nullptr,
                      true,
                      fresh ? "Appended to circular ring buffer" : "Overwrote oldest frame in ring buffer");
    return fresh;
}

bool OCASSystem::ingest_cdm(const CDMAlert& alert) {
    bool ok = cdm_fifo_push(&cdm_fifo_, &alert);
    logger_.log_event("CDM_UPLINK_INGESTED",
                      &current_state_,
                      &alert,
                      nullptr,
                      ok,
                      ok ? "Enqueued to static CDM FIFO" : "CDM queue full - frame dropped");
    return ok;
}

size_t OCASSystem::process_and_prioritize_threats() {
    size_t count = 0;
    CDMAlert alert;

    while (cdm_fifo_pop(&cdm_fifo_, &alert)) {
        ThreatAlert scored = classifier_.evaluate(alert, static_cast<uint64_t>(current_state_.epoch_s));
        max_heap_.insert(scored);
        count++;

        std::ostringstream ss;
        ss << "Threat classified: P(col)=" << std::fixed << std::setprecision(4)
           << scored.risk_score << " ["
           << LogisticRiskClassifier::risk_level_to_string(scored.report.risk_level)
           << "] Logit=" << scored.report.logit_z;

        logger_.log_event("THREAT_CLASSIFIED_AND_HEAPED",
                          &current_state_,
                          &alert,
                          nullptr,
                          true,
                          ss.str());
    }

    return count;
}

bool OCASSystem::execute_top_threat_avoidance(bool simulate_anomaly_on_primary) {
    if (max_heap_.is_empty()) {
        return true; // No threats queued
    }

    ThreatAlert threat;
    if (!max_heap_.extract_max(threat)) {
        return false;
    }

    if (threat.report.action == TacticalAction::NOMINAL_PASS) {
        logger_.log_event("NOMINAL_PASS_MONITORING",
                          &current_state_,
                          &threat.alert,
                          nullptr,
                          true,
                          "Collision risk below intervention threshold; no delta-v required.");
        return true;
    }

    // Target minimum safe clearance distance
    const double safe_miss_m = 1500.0;
    const double miss_deficit = std::max(0.0, safe_miss_m - threat.alert.d_miss_m);
    const double t_tca = std::max(1.0, threat.alert.t_tca_s);
    const double dv_req = std::max(0.5, (miss_deficit / t_tca) * 2.5);

    // Save pre-burn state to LIFO stack for instantaneous abort rollback
    rollback_stack_.push(current_state_, threat.alert.alert_id, "PRE_AVOIDANCE_BURN");

    if (simulate_anomaly_on_primary) {
        // Anomaly injection on primary engine (Ion) to test automated fallback
        dispatcher_.get_hierarchy()[0]->set_operational(false);
    }

    BurnSolution solution;
    ThrusterEngine* chosen_engine = nullptr;
    const double total_mass = current_state_.dry_mass_kg + current_state_.fuel_mass_kg;
    bool feasible = dispatcher_.arbitrate_and_dispatch(dv_req, threat.alert.t_tca_s, total_mass, solution, chosen_engine);

    if (!feasible || !chosen_engine) {
        logger_.log_event("PROPULSION_ARBITRATION_FAILED",
                          &current_state_,
                          &threat.alert,
                          &solution,
                          false,
                          "All thruster fallback options rejected. Shielding orientation initiated.");
        return false;
    }

    // Execute burn with polymorphically selected engine
    bool burn_ok = chosen_engine->execute_burn(solution.burn_duration_sec, current_state_.fuel_mass_kg);

    if (!burn_ok) {
        // Thruster flameout / mid-burn anomaly -> Instant LIFO Rollback
        logger_.log_event("BURN_ANOMALY_TRIGGERED",
                          &current_state_,
                          &threat.alert,
                          &solution,
                          false,
                          "Thruster failed during burn. Triggering emergency LIFO rollback.");
        trigger_abort_rollback();
        return false;
    }

    // Update spacecraft kinematics: velocity boost along velocity vector and altitude raise
    const double v_mag = std::sqrt(current_state_.v[0] * current_state_.v[0] +
                                   current_state_.v[1] * current_state_.v[1] +
                                   current_state_.v[2] * current_state_.v[2]);
    if (v_mag > 1e-6) {
        const double dv_kms = solution.delta_v_ms / 1000.0;
        current_state_.v[0] += dv_kms * (current_state_.v[0] / v_mag);
        current_state_.v[1] += dv_kms * (current_state_.v[1] / v_mag);
        current_state_.v[2] += dv_kms * (current_state_.v[2] / v_mag);
    }

    // Simplified orbital altitude adjustment (semi-major axis perturbation)
    const double r_mag = std::sqrt(current_state_.r[0] * current_state_.r[0] +
                                   current_state_.r[1] * current_state_.r[1] +
                                   current_state_.r[2] * current_state_.r[2]);
    if (r_mag > 1e-6) {
        const double dr_km = (solution.delta_v_ms * solution.burn_duration_sec) / 2000.0;
        current_state_.r[0] += dr_km * (current_state_.r[0] / r_mag);
        current_state_.r[1] += dr_km * (current_state_.r[1] / r_mag);
        current_state_.r[2] += dr_km * (current_state_.r[2] / r_mag);
    }

    current_state_.status_flags |= OCAS_STATUS_MANEUVERING;

    // Record completed maneuver into Doubly Linked List Mission Ledger
    ManeuverRecord record{};
    record.target_object_id = threat.alert.object_id;
    std::snprintf(record.target_name, sizeof(record.target_name), "%s", threat.alert.object_name);
    record.delta_v_target_ms = dv_req;
    record.delta_v_actual_ms = solution.delta_v_ms;
    record.propellant_used_kg = solution.propellant_mass_kg;
    record.burn_duration_s = solution.burn_duration_sec;
    record.initial_fuel_kg = current_state_.fuel_mass_kg + solution.propellant_mass_kg;
    record.final_fuel_kg = current_state_.fuel_mass_kg;
    record.engine_used = solution.engine_name;
    record.risk_score_mitigated = threat.risk_score;
    record.execution_epoch_s = static_cast<uint64_t>(current_state_.epoch_s);
    record.abort_rollback_occurred = false;

    mission_ledger_.append(record);

    logger_.log_event("BURN_COMPLETED_SUCCESSFULLY",
                      &current_state_,
                      &threat.alert,
                      &solution,
                      true,
                      "Collision avoidance maneuver executed and recorded in mission ledger.");

    return true;
}

bool OCASSystem::trigger_abort_rollback() {
    SatelliteState restored{};
    if (!rollback_stack_.pop(restored)) {
        return false;
    }

    current_state_ = restored;
    current_state_.status_flags |= OCAS_STATUS_ROLLBACK;

    logger_.log_event("ABORT_ROLLBACK_EXECUTED",
                      &current_state_,
                      nullptr,
                      nullptr,
                      true,
                      "Spacecraft state restored from LIFO rollback stack after abort event.");
    return true;
}

std::string OCASSystem::export_telemetry_json() const {
    std::ostringstream ss;
    ss << "{"
       << "\"team_id\":\"DSCPP-III-2026-T018\","
       << "\"system_name\":\"OCAS: Autonomous Orbital Collision Avoidance System\","
       << "\"institution\":\"Graphic Era University\","
       << "\"satellite\":{"
       << "\"epoch_s\":" << std::fixed << std::setprecision(3) << current_state_.epoch_s << ","
       << "\"position_km\":[" << current_state_.r[0] << "," << current_state_.r[1] << "," << current_state_.r[2] << "],"
       << "\"velocity_kms\":[" << current_state_.v[0] << "," << current_state_.v[1] << "," << current_state_.v[2] << "],"
       << "\"dry_mass_kg\":" << current_state_.dry_mass_kg << ","
       << "\"fuel_mass_kg\":" << current_state_.fuel_mass_kg << ","
       << "\"total_mass_kg\":" << (current_state_.dry_mass_kg + current_state_.fuel_mass_kg) << ","
       << "\"status_flags\":" << current_state_.status_flags
       << "},"
       << "\"ring_buffer\":{"
       << "\"count\":" << ring_buffer_count(&ring_buffer_) << ","
       << "\"capacity\":" << OCAS_RING_BUFFER_CAPACITY << ","
       << "\"overflow\":" << (ring_buffer_.overflow_occurred ? "true" : "false") << ","
       << "\"overwritten\":" << ring_buffer_.total_overwritten
       << "},"
       << "\"cdm_fifo\":{"
       << "\"count\":" << cdm_fifo_count(&cdm_fifo_) << ","
       << "\"capacity\":" << OCAS_CDM_FIFO_CAPACITY << ","
       << "\"overflow\":" << (cdm_fifo_.overflow_occurred ? "true" : "false") << ","
       << "\"dropped\":" << cdm_fifo_.total_dropped
       << "},"
       << "\"max_heap\":{"
       << "\"count\":" << max_heap_.size() << ","
       << "\"capacity\":" << MaxHeapPriorityQueue::MAX_CAPACITY << ","
       << "\"items\":[";

    const ThreatAlert* threats = max_heap_.data();
    for (size_t i = 0; i < max_heap_.size(); ++i) {
        if (i > 0) ss << ",";
        const auto& t = threats[i];
        ss << "{"
           << "\"id\":" << t.alert.object_id << ","
           << "\"name\":\"" << t.alert.object_name << "\","
           << "\"d_miss_m\":" << t.alert.d_miss_m << ","
           << "\"v_rel_kms\":" << t.alert.v_rel_kms << ","
           << "\"t_tca_s\":" << t.alert.t_tca_s << ","
           << "\"risk_score\":" << std::setprecision(4) << t.risk_score << ","
           << "\"level\":\"" << LogisticRiskClassifier::risk_level_to_string(t.report.risk_level) << "\","
           << "\"action\":\"" << LogisticRiskClassifier::action_to_string(t.report.action) << "\","
           << "\"explain\":{"
           << "\"f_d\":" << t.report.f_miss_distance << ","
           << "\"f_v\":" << t.report.f_rel_velocity << ","
           << "\"f_t\":" << t.report.f_time_to_tca << ","
           << "\"f_sigma\":" << t.report.f_covariance_trace << ","
           << "\"sigma_eff_m\":" << t.report.sigma_eff_m << ","
           << "\"logit\":" << t.report.logit_z
           << "}"
           << "}";
    }

    ss << "]},"
       << "\"rollback_stack\":{"
       << "\"depth\":" << rollback_stack_.size() << ","
       << "\"capacity\":" << StateStack::capacity()
       << "},"
       << "\"ledger\":{"
       << "\"count\":" << mission_ledger_.size() << ","
       << "\"records\":[";

    bool first_rec = true;
    mission_ledger_.traverse_forward([&ss, &first_rec](const ManeuverRecord& r, size_t) {
        if (!first_rec) ss << ",";
        first_rec = false;
        ss << "{"
           << "\"id\":" << r.maneuver_id << ","
           << "\"target\":\"" << r.target_name << "\","
           << "\"delta_v_target\":" << r.delta_v_target_ms << ","
           << "\"delta_v_actual\":" << r.delta_v_actual_ms << ","
           << "\"propellant_spent\":" << r.propellant_used_kg << ","
           << "\"duration_s\":" << r.burn_duration_s << ","
           << "\"engine\":\"" << r.engine_used << "\","
           << "\"epoch\":" << r.execution_epoch_s
           << "}";
    });

    LedgerAuditSummary summary = mission_ledger_.compute_audit_summary();
    ss << "],"
       << "\"summary\":{"
       << "\"total_delta_v\":" << summary.cumulative_delta_v_ms << ","
       << "\"total_propellant\":" << summary.cumulative_propellant_kg << ","
       << "\"total_burn_time\":" << summary.cumulative_burn_time_s << ","
       << "\"chemical_burns\":" << summary.chemical_burn_count << ","
       << "\"ion_burns\":" << summary.ion_burn_count << ","
       << "\"cold_gas_burns\":" << summary.cold_gas_burn_count << ","
       << "\"abort_rollbacks\":" << summary.abort_rollback_count
       << "}"
       << "}"
       << "}";

    return ss.str();
}

} // namespace ocas
