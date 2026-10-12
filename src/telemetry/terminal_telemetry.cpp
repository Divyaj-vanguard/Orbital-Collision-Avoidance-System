// OCAS terminal telemetry implementation.
#include "telemetry/terminal_telemetry.hpp"

#include <cstdio>

namespace ocas {

namespace {

const char* plane_words(ManeuverPlane p) noexcept {
    switch (p) {
        case ManeuverPlane::AlongTrack: return "along-track";
        case ManeuverPlane::Radial:     return "radial";
        case ManeuverPlane::CrossTrack: return "cross-track";
    }
    return "?";
}

const char* status_words(AdvisoryStatus s) noexcept {
    switch (s) {
        case AdvisoryStatus::Staged:
        case AdvisoryStatus::Committed:      return "planned, sent to OEM";
        case AdvisoryStatus::RolledBack:     return "aborted";
        case AdvisoryStatus::RejectedDvCap:  return "too large -> ground";
        case AdvisoryStatus::RejectedBudget: return "no fuel -> ground";
    }
    return "?";
}

}  // namespace

Ansi make_ansi(bool on) noexcept {
    if (!on) return Ansi{"", "", "", "", "", "", "", ""};
    return Ansi{"\033[0m", "\033[1m", "\033[2m", "\033[31m", "\033[32m", "\033[33m", "\033[36m", "\033[35m"};
}

TerminalTelemetry::TerminalTelemetry(bool color, Detail detail, std::uint64_t summary_every_cycles) noexcept
    : a_(make_ansi(color)), detail_(detail), summary_every_(summary_every_cycles) {}

void TerminalTelemetry::on_session_start(const SessionInfo& s) {
    if (detail_ == Detail::Verbose) {
        std::printf("%s%s[SESSION]%s host regime %s%s%s | altitude %.0f km | period %.1f min | "
                    "dv budget %.2f m/s | model %s, decision line %.4f | abort every %u\n",
                    a_.bold, a_.cyan, a_.reset, a_.bold, to_string(s.regime), a_.reset, s.altitude_km,
                    s.orbital_period_s / 60.0, s.dv_budget_mps, to_string(s.primary_model), s.decision_threshold,
                    s.abort_every_n);
        std::printf("%s[SESSION]%s input: %s\n\n", a_.cyan, a_.reset, s.input_label);
        return;
    }
    std::printf("%sSatellite%s %s at %.0f km | fuel budget %.2f m/s | risk model: %s\n\n", a_.bold, a_.reset,
                to_string(s.regime), s.altitude_km, s.dv_budget_mps,
                s.regime == OrbitalRegime::LEO ? "LSTM" : "safety rule (the LSTM is trained on LEO only)");
}

void TerminalTelemetry::print_verbose_row(const LedgerRecord& r) {
    const EvasionDirective& d = r.directive;
    const char* ucol = d.urgency == Urgency::Immediate ? a_.red
                       : d.urgency == Urgency::Priority ? a_.yellow
                                                        : a_.green;
    const char* scol = r.status == AdvisoryStatus::Staged ? a_.green : a_.red;
    if (advisories_printed_ % 20 == 0) {
        std::printf("%s  CYCLE  ALERT  MODEL        SCORE   REGIME PLANE        DV[m/s]   DIR(R,T,N)  "
                    "BURN AT[h]  LEAD[h]  SEP->[km]  URGENCY    STATUS%s\n",
                    a_.dim, a_.reset);
    }
    std::printf("  %5llu  %5u  %-12s %6.4f  %-6s %-12s %8.4f   (%+.0f,%+.0f,%+.0f)  %10.2f  %7.2f  %9.2f  "
                "%s%-9s%s  %s%s%s\n",
                static_cast<unsigned long long>(r.cycle), d.alert_id, to_string(r.model), r.risk_score,
                to_string(d.regime), to_string(d.plane), d.delta_v_mps, d.direction.r, d.direction.t, d.direction.n,
                d.execute_no_later_than_s / 3600.0, d.lead_time_s / 3600.0, d.predicted_separation_m / 1000.0, ucol,
                to_string(d.urgency), a_.reset, scol, to_string(r.status), a_.reset);
}

void TerminalTelemetry::print_normal_row(const LedgerRecord& r) {
    const EvasionDirective& d = r.directive;
    const char* ucol = d.urgency == Urgency::Immediate ? a_.red
                       : d.urgency == Urgency::Priority ? a_.yellow
                                                        : a_.green;
    const char* scol = r.status == AdvisoryStatus::Staged ? a_.green : a_.red;
    if (advisories_printed_ == 0) {
        std::printf("%sMANOEUVRES%s\n", a_.bold, a_.reset);
        std::printf("%s  %-8s %-10s %-12s %8s %10s %13s  %s%s\n", a_.dim, "THREAT", "URGENCY", "DIRECTION",
                    "DV m/s", "BURN IN h", "MISS AFTER km", "DECISION", a_.reset);
    }
    std::printf("  %-8u %s%-10s%s %-12s %8.3f %10.1f %13.1f  %s%s%s\n", d.alert_id, ucol, to_string(d.urgency),
                a_.reset, plane_words(d.plane), d.delta_v_mps, d.execute_no_later_than_s / 3600.0,
                d.predicted_separation_m / 1000.0, scol, status_words(r.status), a_.reset);
}

void TerminalTelemetry::on_advisory(const LedgerRecord& r) {
    ++advisories_seen_;
    if (detail_ == Detail::Quiet) return;
    if (detail_ == Detail::Normal && advisories_printed_ >= kNormalRows) return;
    if (detail_ == Detail::Verbose) print_verbose_row(r);
    else print_normal_row(r);
    ++advisories_printed_;
}

void TerminalTelemetry::on_rollback(std::uint32_t alert_id, std::size_t unwound, const SpacecraftState& s) {
    if (detail_ == Detail::Quiet) return;
    if (detail_ == Detail::Normal) {
        if (advisories_printed_ >= kNormalRows) return;
        std::printf("  %s>> operator aborted threat %u: %zu manoeuvre(s) undone, fuel restored to %.3f m/s%s\n",
                    a_.yellow, alert_id, unwound, s.dv_budget_mps, a_.reset);
        return;
    }
    std::printf("  %s%s>> EXTERNAL ABORT%s alert %u: LIFO unwound %zu checkpoint(s); dv budget restored to "
                "%.4f m/s\n",
                a_.bold, a_.yellow, a_.reset, alert_id, unwound, s.dv_budget_mps);
}

void TerminalTelemetry::on_cycle_end(const CycleStats& c) {
    if (detail_ != Detail::Verbose || summary_every_ == 0 || c.cycle % summary_every_ != 0) return;
    std::printf("%s  [cycle %llu] ingested %zu | actionable %zu | directives %zu | cycle %.1f us | "
                "dv left %.3f m/s%s\n",
                a_.dim, static_cast<unsigned long long>(c.cycle), c.ingested, c.actionable, c.directives,
                c.cycle_latency_us, c.dv_budget_remaining_mps, a_.reset);
    std::fflush(stdout);
}

}  // namespace ocas
