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
    if (!on) return Ansi{"", "", "", "", "", "", ""};
    return Ansi{"\033[0m", "\033[1m", "\033[2m", "\033[31m", "\033[32m", "\033[33m", "\033[36m"};
}

void TerminalTelemetry::on_session_start(const SessionInfo& s) {
    std::printf("%sSatellite%s %s at %.0f km | fuel budget %.2f m/s | risk model: %s\n\n", a_.bold, a_.reset,
                to_string(s.regime), s.altitude_km, s.dv_budget_mps,
                s.regime == OrbitalRegime::LEO ? "LSTM" : "safety rule (the LSTM is trained on LEO only)");
}

void TerminalTelemetry::on_advisory(const LedgerRecord& r) {
    ++seen_;
    if (quiet_ || printed_ >= kRows) return;
    const EvasionDirective& d = r.directive;
    const char* ucol = d.urgency == Urgency::Immediate ? a_.red
                       : d.urgency == Urgency::Priority ? a_.yellow
                                                        : a_.green;
    const char* scol = r.status == AdvisoryStatus::Staged ? a_.green : a_.red;
    if (printed_ == 0) {
        std::printf("%sMANOEUVRES%s\n", a_.bold, a_.reset);
        std::printf("%s  %-8s %-10s %-12s %8s %10s %13s  %s%s\n", a_.dim, "THREAT", "URGENCY", "DIRECTION",
                    "DV m/s", "BURN IN h", "MISS AFTER km", "DECISION", a_.reset);
    }
    std::printf("  %-8u %s%-10s%s %-12s %8.3f %10.1f %13.1f  %s%s%s\n", d.alert_id, ucol, to_string(d.urgency),
                a_.reset, plane_words(d.plane), d.delta_v_mps, d.execute_no_later_than_s / 3600.0,
                d.predicted_separation_m / 1000.0, scol, status_words(r.status), a_.reset);
    ++printed_;
}

void TerminalTelemetry::on_rollback(std::uint32_t alert_id, std::size_t unwound, const SpacecraftState& s) {
    if (quiet_ || printed_ >= kRows) return;
    std::printf("  %s>> operator aborted threat %u: %zu manoeuvre(s) undone, fuel restored to %.3f m/s%s\n",
                a_.yellow, alert_id, unwound, s.dv_budget_mps, a_.reset);
}

}  // namespace ocas
