#ifndef OCAS_CORE_TYPES_HPP
#define OCAS_CORE_TYPES_HPP

#include <cstdint>
#include <type_traits>

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
    InDomain,
    CovarianceMissing,
    CovarianceImplausible,
    VelocityOutOfEnvelope,
    RegimeOutOfDomain
};

enum class ConservativeRule : std::uint8_t { None, PcEstimate, ScreeningVolume, GeometricOnly };

enum class AdvisoryStatus : std::uint8_t { Staged, Committed, RolledBack, RejectedDvCap, RejectedBudget };

struct VectorRTN {
    double r = 0.0;
    double t = 0.0;
    double n = 0.0;
};

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

struct SpacecraftState {
    double altitude_km = 0.0;
    double dv_budget_mps = 0.0;
};

// Fixed-capacity containers store these by value, so none may own heap memory.
static_assert(std::is_trivially_copyable_v<ScoredThreat>);
static_assert(std::is_trivially_copyable_v<LedgerRecord>);
static_assert(std::is_trivially_copyable_v<SpacecraftState>);

// sqrt(trace) above 1000 km carries no information (ESA has traces up to 2.4e16 m^2).
inline constexpr double kMaxPlausibleSigmaM = 1.0e6;

[[nodiscard]] bool covariance_usable(double cov_trace_m2) noexcept;

const char* to_string(OrbitalRegime v) noexcept;
const char* to_string(ManeuverPlane v) noexcept;
const char* to_string(Urgency v) noexcept;
const char* to_string(RiskModel v) noexcept;
const char* to_string(ModelSelection v) noexcept;
const char* to_string(ConservativeRule v) noexcept;
const char* to_string(AdvisoryStatus v) noexcept;

}  // namespace ocas

#endif
