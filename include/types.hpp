#ifndef OCAS_TYPES_HPP
#define OCAS_TYPES_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ocas {

struct ConjunctionTelemetry {
    std::uint32_t alert_id = 0;
    double miss_distance_m = 0.0;
    double relative_speed_km_s = 0.0;
    double time_to_tca_sec = 0.0;
    double cov_trace_m2 = 0.0;    // NaN if missing
    double collision_prob = 0.0;  // evaluation label only, never a model input
};

enum class OrbitalRegime : std::uint8_t { LEO, MEO, GEO };
enum class ManeuverPlane : std::uint8_t { AlongTrack, Radial, CrossTrack };
enum class Urgency : std::uint8_t { Routine, Priority, Immediate };
enum class RiskModel : std::uint8_t { ConservativeThreshold, Lstm };
enum class ModelSelection : std::uint8_t {
    InDomain, CovarianceMissing, CovarianceImplausible, VelocityOutOfEnvelope, RegimeOutOfDomain
};
enum class ConservativeRule : std::uint8_t { None, PcEstimate, ScreeningVolume, GeometricOnly };
enum class AdvisoryStatus : std::uint8_t { Staged, Committed, RolledBack, RejectedDvCap, RejectedBudget };
enum class InputRejectReason : std::uint8_t { NonFinite, NegativeDistance, PastTca };

inline const char* to_string(OrbitalRegime v) {
    static const char* const s[] = {"LEO", "MEO", "GEO"};
    return s[int(v)];
}
inline const char* to_string(ManeuverPlane v) {
    static const char* const s[] = {"ALONG_TRACK", "RADIAL", "CROSS_TRACK"};
    return s[int(v)];
}
inline const char* to_string(Urgency v) {
    static const char* const s[] = {"ROUTINE", "PRIORITY", "IMMEDIATE"};
    return s[int(v)];
}
inline const char* to_string(RiskModel v) {
    static const char* const s[] = {"CONSERVATIVE", "LSTM"};
    return s[int(v)];
}
inline const char* to_string(ModelSelection v) {
    static const char* const s[] = {"IN_DOMAIN", "COV_MISSING", "COV_IMPLAUSIBLE", "VEL_OUT_OF_ENVELOPE",
                                    "REGIME_OUT_OF_DOMAIN"};
    return s[int(v)];
}
inline const char* to_string(ConservativeRule v) {
    static const char* const s[] = {"NONE", "PC_ESTIMATE", "SCREENING_VOLUME", "GEOMETRIC_ONLY"};
    return s[int(v)];
}
inline const char* to_string(AdvisoryStatus v) {
    static const char* const s[] = {"STAGED", "COMMITTED", "ROLLED_BACK", "REJECTED_DV_CAP", "REJECTED_BUDGET"};
    return s[int(v)];
}
inline const char* to_string(InputRejectReason v) {
    static const char* const s[] = {"NON_FINITE", "NEGATIVE_DISTANCE", "PAST_TCA"};
    return s[int(v)];
}

struct RiskAssessment {
    double score = 0.0;
    bool actionable = false;
    RiskModel model = RiskModel::Lstm;
    ModelSelection selection = ModelSelection::InDomain;
    ConservativeRule rule = ConservativeRule::None;
    double logit = 0.0;
};

struct ScoredThreat {
    ConjunctionTelemetry telemetry{};
    RiskAssessment risk{};
    double regime_risk = 0.0;
    double priority = 0.0;
    std::uint64_t sequence = 0;
};

struct VectorRTN {
    double r = 0.0, t = 0.0, n = 0.0;
};

struct EvasionDirective {
    std::uint32_t alert_id = 0;
    OrbitalRegime regime = OrbitalRegime::LEO;
    ManeuverPlane plane = ManeuverPlane::AlongTrack;
    Urgency urgency = Urgency::Routine;
    double delta_v_mps = 0.0;
    VectorRTN direction{};
    double execute_no_later_than_s = 0.0;
    double lead_time_s = 0.0;
    double target_separation_m = 0.0;
    double predicted_separation_m = 0.0;
    bool within_regime_dv_cap = true;
};

struct LedgerRecord {
    std::uint64_t cycle = 0;
    EvasionDirective directive{};
    double risk_score = 0.0;
    double priority = 0.0;
    RiskModel model = RiskModel::Lstm;
    AdvisoryStatus status = AdvisoryStatus::Staged;
};

struct CycleStats {
    std::uint64_t cycle = 0;
    std::size_t ingested = 0;
    std::size_t rejected_input = 0;
    std::size_t scored = 0;
    std::size_t actionable = 0;
    std::size_t model_used = 0;
    std::size_t fallback_used = 0;
    std::size_t directives = 0;
    std::size_t committed = 0;
    std::size_t rolled_back = 0;
    std::size_t rejected_dv_cap = 0;
    std::size_t rejected_budget = 0;
    std::size_t suppressed_duplicates = 0;
    std::uint64_t ingest_overflow_total = 0;
    double cycle_latency_us = 0.0;
    double dv_budget_remaining_mps = 0.0;
};

// sqrt(trace) above 1000 km carries no information (ESA has traces up to 2.4e16 m^2).
inline constexpr double kMaxPlausibleSigmaM = 1.0e6;

inline bool covariance_usable(double cov_trace_m2) noexcept {
    return std::isfinite(cov_trace_m2) && cov_trace_m2 > 0.0 && std::sqrt(cov_trace_m2) <= kMaxPlausibleSigmaM;
}

}  // namespace ocas

#endif
