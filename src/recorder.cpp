#include "recorder.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace ocas {

Ansi make_ansi(bool on) noexcept {
    if (!on) return Ansi{"", "", "", "", "", "", ""};
    return Ansi{"\033[0m", "\033[1m", "\033[2m", "\033[31m", "\033[32m", "\033[33m", "\033[36m"};
}

Recorder::Recorder(const std::string& path, bool log_every_threat, bool quiet, bool color)
    : path_(path), log_every_threat_(log_every_threat), quiet_(quiet), a_(make_ansi(color)) {
    const std::filesystem::path p(path_);
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    out_.open(path_, std::ios::out | std::ios::app);
}

void Recorder::log(const char* event, const char* fmt, ...) noexcept {
    if (!out_.is_open()) return;
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char* buf = line_.data();
    const std::size_t cap = line_.size() - 2;  // room for '}' and '\n'
    std::size_t len = std::strftime(buf, cap, "{\"utc\":\"%Y-%m-%dT%H:%M:%SZ\",", &utc);
    int n = std::snprintf(buf + len, cap - len, "\"event\":\"%s\"", event);
    len += static_cast<std::size_t>(n > 0 ? n : 0);
    if (len >= cap) len = cap - 1;
    va_list args;
    va_start(args, fmt);
    n = std::vsnprintf(buf + len, cap - len, fmt, args);
    va_end(args);
    len = (n < 0 || static_cast<std::size_t>(n) >= cap - len) ? cap - 1 : len + static_cast<std::size_t>(n);
    buf[len++] = '}';
    buf[len++] = '\n';
    out_.write(buf, static_cast<std::streamsize>(len));
}

void Recorder::session_start(const Regime& r, double dv_budget_mps, double threshold, std::uint32_t abort_every,
                             const char* input) {
    const bool leo = r.kind == OrbitalRegime::LEO;
    log("SESSION_START",
        ",\"regime\":\"%s\",\"altitude_km\":%.1f,\"period_s\":%.1f,\"dv_budget_mps\":%.3f,"
        "\"model\":\"%s\",\"decision_threshold\":%.6f,\"abort_every_n\":%u,\"input\":\"%s\"",
        to_string(r.kind), r.altitude_km, r.period_s(), dv_budget_mps, leo ? "LSTM" : "CONSERVATIVE", threshold,
        abort_every, input);
    std::printf("%sSatellite%s %s at %.0f km | fuel budget %.2f m/s | risk model: %s\n\n", a_.bold, a_.reset,
                to_string(r.kind), r.altitude_km, dv_budget_mps,
                leo ? "LSTM" : "safety rule (the LSTM is trained on LEO only)");
}

void Recorder::input_rejected(const ConjunctionTelemetry& t, InputRejectReason why) {
    log("INPUT_REJECTED", ",\"alert_id\":%u,\"reason\":\"%s\"", t.alert_id, to_string(why));
}

void Recorder::threat_scored(const ScoredThreat& s) {
    if (!log_every_threat_ && !s.risk.actionable) return;
    const ConjunctionTelemetry& t = s.telemetry;
    char cov[32] = "null";  // JSON has no NaN
    if (std::isfinite(t.cov_trace_m2)) std::snprintf(cov, sizeof cov, "%.6g", t.cov_trace_m2);
    log("THREAT_SCORED",
        ",\"alert_id\":%u,\"seq\":%llu,\"miss_m\":%.1f,\"v_km_s\":%.3f,\"tca_s\":%.1f,\"cov_trace_m2\":%s,"
        "\"model\":\"%s\",\"selection\":\"%s\",\"rule\":\"%s\",\"score\":%.6f,\"logit\":%.4f,"
        "\"regime_risk\":%.6f,\"priority\":%.6f,\"actionable\":%s",
        t.alert_id, static_cast<unsigned long long>(s.sequence), t.miss_distance_m, t.relative_speed_km_s,
        t.time_to_tca_sec, cov, to_string(s.risk.model), to_string(s.risk.selection), to_string(s.risk.rule),
        s.risk.score, s.risk.logit, s.regime_risk, s.priority, s.risk.actionable ? "true" : "false");
}

void Recorder::log_record(const char* event, const LedgerRecord& r) noexcept {
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

void Recorder::advisory(const LedgerRecord& r) {
    log_record("ADVISORY_ISSUED", r);
    ++seen_;
    if (quiet_ || printed_ >= kRows) return;
    static const char* const plane[] = {"along-track", "radial", "cross-track"};
    static const char* const status[] = {"planned, sent to OEM", "planned, sent to OEM", "aborted",
                                         "too large -> ground", "no fuel -> ground"};
    const EvasionDirective& d = r.directive;
    const char* ucol = d.urgency == Urgency::Immediate ? a_.red : d.urgency == Urgency::Priority ? a_.yellow : a_.green;
    const char* scol = r.status == AdvisoryStatus::Staged ? a_.green : a_.red;
    if (printed_ == 0) {
        std::printf("%sMANOEUVRES%s\n", a_.bold, a_.reset);
        std::printf("%s  %-8s %-10s %-12s %8s %10s %13s  %s%s\n", a_.dim, "THREAT", "URGENCY", "DIRECTION",
                    "DV m/s", "BURN IN h", "MISS AFTER km", "DECISION", a_.reset);
    }
    std::printf("  %-8u %s%-10s%s %-12s %8.3f %10.1f %13.1f  %s%s%s\n", d.alert_id, ucol, to_string(d.urgency),
                a_.reset, plane[int(d.plane)], d.delta_v_mps, d.execute_no_later_than_s / 3600.0,
                d.predicted_separation_m / 1000.0, scol, status[int(r.status)], a_.reset);
    ++printed_;
}

void Recorder::advisory_resolved(const LedgerRecord& r) { log_record("ADVISORY_RESOLVED", r); }

void Recorder::rollback(std::uint32_t alert_id, std::size_t unwound, double dv_budget_mps) {
    log("ABORT_ROLLBACK", ",\"abort_alert_id\":%u,\"checkpoints_unwound\":%zu,\"dv_budget_mps\":%.5f", alert_id,
        unwound, dv_budget_mps);
    if (quiet_ || printed_ >= kRows) return;
    std::printf("  %s>> operator aborted threat %u: %zu manoeuvre(s) undone, fuel restored to %.3f m/s%s\n",
                a_.yellow, alert_id, unwound, dv_budget_mps, a_.reset);
}

void Recorder::cycle_end(const CycleStats& c) {
    if (c.directives > 0 || c.rejected_input > 0) {
        log("CYCLE_END",
            ",\"cycle\":%llu,\"ingested\":%zu,\"actionable\":%zu,\"directives\":%zu,\"committed\":%zu,"
            "\"rolled_back\":%zu,\"rejected\":%zu,\"suppressed\":%zu,\"latency_us\":%.2f",
            static_cast<unsigned long long>(c.cycle), c.ingested, c.actionable, c.directives, c.committed,
            c.rolled_back, c.rejected_dv_cap + c.rejected_budget, c.suppressed_duplicates, c.cycle_latency_us);
    }
    out_.flush();
}

void Recorder::session_end(const CycleStats& c) {
    log("SESSION_END",
        ",\"cycles\":%llu,\"ingested\":%zu,\"rejected_input\":%zu,\"actionable\":%zu,\"directives\":%zu,"
        "\"committed\":%zu,\"rolled_back\":%zu,\"rejected_dv_cap\":%zu,\"rejected_budget\":%zu,"
        "\"ingest_overflows\":%llu,\"dv_budget_remaining_mps\":%.5f",
        static_cast<unsigned long long>(c.cycle), c.ingested, c.rejected_input, c.actionable, c.directives,
        c.committed, c.rolled_back, c.rejected_dv_cap, c.rejected_budget,
        static_cast<unsigned long long>(c.ingest_overflow_total), c.dv_budget_remaining_mps);
    out_.flush();
}

}  // namespace ocas
