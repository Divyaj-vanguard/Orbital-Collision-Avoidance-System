/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Persistence & Audit Layer Implementation
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 */

#include "persistence.hpp"
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ocas {

namespace {

std::string get_iso8601_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm gmt{};
#if defined(_WIN32)
    gmtime_s(&gmt, &tt);
#else
    gmtime_r(&tt, &gmt);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gmt);
    return std::string(buf);
}

} // anonymous namespace

/* ============================================================================
 * StateStack Implementation (LIFO Rollback)
 * ============================================================================ */

StateStack::StateStack() noexcept : top_(0) {}

bool StateStack::push_snapshot(const SatelliteState& state, uint32_t alert_id, const char* context) noexcept {
    if (top_ >= MAX_STACK_DEPTH) {
        return false; // Overflow protection
    }

    StateSnapshot& snap = stack_[top_];
    snap.state = state;
    snap.snapshot_epoch_s = static_cast<uint64_t>(state.epoch_s);
    snap.trigger_alert_id = alert_id;

    if (context) {
        std::snprintf(snap.context_tag, sizeof(snap.context_tag), "%s", context);
    } else {
        snap.context_tag[0] = '\0';
    }

    top_++;
    return true;
}

bool StateStack::pop_and_rollback(SatelliteState& out_state) noexcept {
    if (top_ == 0) {
        return false; // Underflow
    }

    top_--;
    out_state = stack_[top_].state;
    return true;
}

bool StateStack::peek_state(SatelliteState& out_state) const noexcept {
    if (top_ == 0) {
        return false;
    }
    out_state = stack_[top_ - 1].state;
    return true;
}

/* ============================================================================
 * ManeuverLedger Implementation (Doubly Linked List with RAII Destructor)
 * ============================================================================ */

ManeuverLedger::ManeuverLedger() noexcept
    : head_(nullptr), tail_(nullptr), count_(0), next_maneuver_id_(1) {}

ManeuverLedger::~ManeuverLedger() {
    clear();
}

ManeuverLedger::ManeuverLedger(ManeuverLedger&& other) noexcept
    : head_(other.head_), tail_(other.tail_), count_(other.count_), next_maneuver_id_(other.next_maneuver_id_) {
    other.head_ = nullptr;
    other.tail_ = nullptr;
    other.count_ = 0;
    other.next_maneuver_id_ = 1;
}

ManeuverLedger& ManeuverLedger::operator=(ManeuverLedger&& other) noexcept {
    if (this != &other) {
        clear();
        head_ = other.head_;
        tail_ = other.tail_;
        count_ = other.count_;
        next_maneuver_id_ = other.next_maneuver_id_;

        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.count_ = 0;
        other.next_maneuver_id_ = 1;
    }
    return *this;
}

void ManeuverLedger::clear() noexcept {
    ManeuverNode* curr = head_;
    while (curr) {
        ManeuverNode* next = curr->next;
        delete curr;
        curr = next;
    }
    head_ = nullptr;
    tail_ = nullptr;
    count_ = 0;
}

void ManeuverLedger::append_maneuver(const ManeuverRecord& record) {
    auto* node = new ManeuverNode{record, tail_, nullptr};
    if (node->data.maneuver_id == 0) {
        node->data.maneuver_id = next_maneuver_id_++;
    }

    if (tail_) {
        tail_->next = node;
    }
    tail_ = node;

    if (!head_) {
        head_ = node;
    }

    count_++;
}

bool ManeuverLedger::append(const ManeuverRecord& record) {
    append_maneuver(record);
    return true;
}

void ManeuverLedger::traverse_forward(const std::function<void(const ManeuverRecord&)>& callback) const {
    ManeuverNode* curr = head_;
    while (curr) {
        callback(curr->data);
        curr = curr->next;
    }
}

void ManeuverLedger::traverse_forward(const std::function<void(const ManeuverRecord&, size_t index)>& callback) const {
    ManeuverNode* curr = head_;
    size_t idx = 0;
    while (curr) {
        callback(curr->data, idx++);
        curr = curr->next;
    }
}

void ManeuverLedger::traverse_backward(const std::function<void(const ManeuverRecord&)>& callback) const {
    ManeuverNode* curr = tail_;
    while (curr) {
        callback(curr->data);
        curr = curr->prev;
    }
}

void ManeuverLedger::traverse_backward(const std::function<void(const ManeuverRecord&, size_t rev_index)>& callback) const {
    ManeuverNode* curr = tail_;
    size_t idx = 0;
    while (curr) {
        callback(curr->data, idx++);
        curr = curr->prev;
    }
}

LedgerAuditSummary ManeuverLedger::compute_audit_summary() const noexcept {
    LedgerAuditSummary summary{};
    summary.total_maneuvers = count_;

    traverse_backward([&summary](const ManeuverRecord& rec, size_t) {
        summary.cumulative_delta_v_ms += rec.delta_v_actual_ms;
        summary.cumulative_propellant_kg += rec.propellant_used_kg;
        summary.cumulative_burn_time_s += rec.burn_duration_s;

        if (rec.engine_used.find("CHEM") != std::string::npos) {
            summary.chemical_burn_count++;
        } else if (rec.engine_used.find("ION") != std::string::npos) {
            summary.ion_burn_count++;
        } else if (rec.engine_used.find("COLD") != std::string::npos ||
                   rec.engine_used.find("RCS") != std::string::npos) {
            summary.cold_gas_burn_count++;
        }

        if (rec.abort_rollback_occurred) {
            summary.abort_rollback_count++;
        }
    });

    return summary;
}

/* ============================================================================
 * Flight Audit JSON Logger Implementation
 * ============================================================================ */

FlightAuditLogger::FlightAuditLogger(const std::string& log_filepath)
    : filepath_(log_filepath), log_stream_(log_filepath, std::ios::app) {}

FlightAuditLogger::~FlightAuditLogger() {
    flush();
    if (log_stream_.is_open()) {
        log_stream_.close();
    }
}

void FlightAuditLogger::flush() {
    if (log_stream_.is_open()) {
        log_stream_.flush();
    }
}

void FlightAuditLogger::log_raw_json(std::string_view json_str) {
    if (log_stream_.is_open()) {
        log_stream_ << json_str << "\n";
        log_stream_.flush();
    }
}

void FlightAuditLogger::log_event(std::string_view event_type,
                                  const SatelliteState* state,
                                  const CDMAlert* alert,
                                  const BurnSolution* solution,
                                  bool execution_success,
                                  std::string_view extra_notes) {
    if (!log_stream_.is_open()) return;

    std::ostringstream ss;
    ss << "{"
       << "\"timestamp\":\"" << get_iso8601_timestamp() << "\","
       << "\"team_id\":\"DSCPP-III-2026-T018\","
       << "\"institution\":\"Graphic Era University\","
       << "\"event\":\"" << event_type << "\"";

    if (state) {
        ss << ",\"satellite\":{"
           << "\"epoch_s\":" << std::fixed << std::setprecision(3) << state->epoch_s << ","
           << "\"position_km\":[" << state->r[0] << "," << state->r[1] << "," << state->r[2] << "],"
           << "\"velocity_kms\":[" << state->v[0] << "," << state->v[1] << "," << state->v[2] << "],"
           << "\"dry_mass_kg\":" << state->dry_mass_kg << ","
           << "\"fuel_mass_kg\":" << state->fuel_mass_kg << ","
           << "\"status_flags\":" << state->status_flags
           << "}";
    }

    if (alert) {
        ss << ",\"threat\":{"
           << "\"object_id\":" << alert->object_id << ","
           << "\"object_name\":\"" << alert->object_name << "\","
           << "\"miss_distance_m\":" << alert->d_miss_m << ","
           << "\"rel_velocity_kms\":" << alert->v_rel_kms << ","
           << "\"time_to_tca_s\":" << alert->t_tca_s << ","
           << "\"cov_trace_m2\":" << (alert->covariance[0][0] + alert->covariance[1][1] + alert->covariance[2][2])
           << "}";
    }

    if (solution) {
        ss << ",\"burn_solution\":{"
           << "\"feasible\":" << (solution->feasible ? "true" : "false") << ","
           << "\"engine\":\"" << solution->engine_name << "\","
           << "\"delta_v_ms\":" << solution->delta_v_ms << ","
           << "\"propellant_kg\":" << solution->propellant_mass_kg << ","
           << "\"burn_duration_s\":" << solution->burn_duration_sec << ","
           << "\"executed\":" << (execution_success ? "true" : "false") << ","
           << "\"diagnostics\":\"" << solution->diagnostics << "\""
           << "}";
    }

    if (!extra_notes.empty()) {
        ss << ",\"notes\":\"" << extra_notes << "\"";
    }

    ss << "}";

    log_stream_ << ss.str() << "\n";
    log_stream_.flush();
}

} // namespace ocas
