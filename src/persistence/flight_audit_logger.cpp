// OCAS Module 5c implementation: append-only JSON-lines flight audit logger.
#include "persistence/flight_audit_logger.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace ocas {

FlightAuditLogger::FlightAuditLogger(const std::string& path, bool log_every_threat)
    : path_(path), log_every_threat_(log_every_threat) {
    const std::filesystem::path p(path_);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    out_.open(path_, std::ios::out | std::ios::app);
}

FlightAuditLogger::~FlightAuditLogger() {
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

void FlightAuditLogger::flush() noexcept {
    if (out_.is_open()) {
        out_.flush();
    }
}

void FlightAuditLogger::log(const char* event, const char* fmt, ...) noexcept {
    if (!out_.is_open()) {
        return;
    }
    // Prefix: wall-clock UTC timestamp and event name.
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char* buf = line_.data();
    const std::size_t cap = line_.size() - 2;  // room for '}' and '\n'
    std::size_t len = std::strftime(buf, cap, "{\"utc\":\"%Y-%m-%dT%H:%M:%SZ\",", &utc);
    int n = std::snprintf(buf + len, cap - len, "\"event\":\"%s\"", event);
    len += static_cast<std::size_t>(n > 0 ? n : 0);
    if (len >= cap) {
        len = cap - 1;
    }

    va_list args;
    va_start(args, fmt);
    n = std::vsnprintf(buf + len, cap - len, fmt, args);
    va_end(args);
    if (n < 0 || static_cast<std::size_t>(n) >= cap - len) {
        ++truncated_;
        len = cap - 1;
    } else {
        len += static_cast<std::size_t>(n);
    }
    buf[len++] = '}';
    buf[len++] = '\n';
    out_.write(buf, static_cast<std::streamsize>(len));
    ++records_;
}

void FlightAuditLogger::on_session_start(const SessionInfo& s) {
    log("SESSION_START",
        ",\"regime\":\"%s\",\"altitude_km\":%.1f,\"period_s\":%.1f,\"dv_budget_mps\":%.3f,"
        "\"decision_threshold\":%.4f,\"abort_every_n\":%u,\"input\":\"%s\"",
        to_string(s.regime), s.altitude_km, s.orbital_period_s, s.dv_budget_mps, s.decision_threshold,
        s.abort_every_n, s.input_label);
}

void FlightAuditLogger::on_input_rejected(const ConjunctionTelemetry& t, InputRejectReason r) {
    log("INPUT_REJECTED", ",\"alert_id\":%u,\"reason\":\"%s\"", t.alert_id, to_string(r));
}

void FlightAuditLogger::on_threat_scored(const ScoredThreat& s) {
    if (!log_every_threat_ && !s.risk.actionable) {
        return;
    }
    const ConjunctionTelemetry& t = s.telemetry;
    // JSON has no NaN literal; a missing covariance is written as null.
    char cov[32];
    if (std::isfinite(t.cov_trace_m2)) {
        std::snprintf(cov, sizeof cov, "%.6g", t.cov_trace_m2);
    } else {
        std::snprintf(cov, sizeof cov, "null");
    }
    log("THREAT_SCORED",
        ",\"alert_id\":%u,\"seq\":%llu,\"miss_m\":%.1f,\"v_km_s\":%.3f,\"tca_s\":%.1f,\"cov_trace_m2\":%s,"
        "\"model\":\"%s\",\"selection\":\"%s\",\"rule\":\"%s\",\"score\":%.6f,\"logit\":%.4f,"
        "\"contrib\":[%.4f,%.4f,%.4f,%.4f],\"regime_risk\":%.6f,\"priority\":%.6f,\"actionable\":%s",
        t.alert_id, static_cast<unsigned long long>(s.sequence), t.miss_distance_m, t.relative_speed_km_s,
        t.time_to_tca_sec, cov, to_string(s.risk.model), to_string(s.risk.selection), to_string(s.risk.rule),
        s.risk.score, s.risk.logit, s.risk.contributions[0], s.risk.contributions[1], s.risk.contributions[2],
        s.risk.contributions[3], s.regime_risk, s.priority, s.risk.actionable ? "true" : "false");
}

void FlightAuditLogger::log_record(const char* event, const LedgerRecord& r) noexcept {
    const EvasionDirective& d = r.directive;
    log(event,
        ",\"cycle\":%llu,\"alert_id\":%u,\"status\":\"%s\",\"regime\":\"%s\",\"plane\":\"%s\","
        "\"urgency\":\"%s\",\"dv_mps\":%.5f,\"dir_rtn\":[%.0f,%.0f,%.0f],\"execute_at_s\":%.1f,"
        "\"lead_s\":%.1f,\"target_sep_m\":%.1f,\"predicted_sep_m\":%.1f,\"model\":\"%s\",\"score\":%.6f",
        static_cast<unsigned long long>(r.cycle), d.alert_id, to_string(r.status), to_string(d.regime),
        to_string(d.plane), to_string(d.urgency), d.delta_v_mps, d.direction.r, d.direction.t, d.direction.n,
        d.execute_no_later_than_s, d.lead_time_s, d.target_separation_m, d.predicted_separation_m,
        to_string(r.model), r.risk_score);
}

void FlightAuditLogger::on_advisory(const LedgerRecord& r) { log_record("ADVISORY_ISSUED", r); }

void FlightAuditLogger::on_advisory_resolved(const LedgerRecord& r) { log_record("ADVISORY_RESOLVED", r); }

void FlightAuditLogger::on_rollback(std::uint32_t alert_id, std::size_t unwound, const SpacecraftState& s) {
    log("ABORT_ROLLBACK", ",\"abort_alert_id\":%u,\"checkpoints_unwound\":%zu,\"dv_budget_mps\":%.5f", alert_id,
        unwound, s.dv_budget_mps);
}

void FlightAuditLogger::on_cycle_end(const CycleStats& c) {
    if (c.directives == 0 && c.rejected_input == 0) {
        flush();
        return;  // keep the ledger focused on cycles where something happened
    }
    log("CYCLE_END",
        ",\"cycle\":%llu,\"ingested\":%zu,\"actionable\":%zu,\"directives\":%zu,\"committed\":%zu,"
        "\"rolled_back\":%zu,\"rejected\":%zu,\"suppressed\":%zu,\"latency_us\":%.2f",
        static_cast<unsigned long long>(c.cycle), c.ingested, c.actionable, c.directives, c.committed,
        c.rolled_back, c.rejected_dv_cap + c.rejected_budget, c.suppressed_duplicates, c.cycle_latency_us);
    flush();
}

void FlightAuditLogger::on_session_end(const CycleStats& c) {
    log("SESSION_END",
        ",\"cycles\":%llu,\"ingested\":%zu,\"rejected_input\":%zu,\"actionable\":%zu,\"directives\":%zu,"
        "\"committed\":%zu,\"rolled_back\":%zu,\"rejected_dv_cap\":%zu,\"rejected_budget\":%zu,"
        "\"ingest_overflows\":%llu,\"dv_budget_remaining_mps\":%.5f",
        static_cast<unsigned long long>(c.cycle), c.ingested, c.rejected_input, c.actionable, c.directives,
        c.committed, c.rolled_back, c.rejected_dv_cap, c.rejected_budget,
        static_cast<unsigned long long>(c.ingest_overflow_total), c.dv_budget_remaining_mps);
    flush();
}

}  // namespace ocas
