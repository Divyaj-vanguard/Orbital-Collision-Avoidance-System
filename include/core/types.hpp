// ============================================================================
// OCAS: Autonomous Orbital Collision Avoidance System
// Core domain types shared by every module.
// Team DSCPP-III-2026-T018 | Graphic Era University
// ============================================================================
// All types here are trivially copyable value types. They are stored by value
// inside fixed-capacity containers, so no type here may own heap memory.
// ============================================================================
#ifndef OCAS_CORE_TYPES_HPP
#define OCAS_CORE_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ocas {

/// One Conjunction Data Message reduced to the six-field OCAS schema.
/// Source: ESA Spacecraft Collision Avoidance Challenge (Kelvins) test set.
struct ConjunctionTelemetry {
    std::uint32_t alert_id = 0;        ///< ESA event_id; repeats across CDM updates
    double miss_distance_m = 0.0;      ///< d_miss at TCA [m]
    double relative_speed_km_s = 0.0;  ///< v_rel at TCA [km/s]
    double time_to_tca_sec = 0.0;      ///< t_TCA from CDM creation [s]
    double cov_trace_m2 = 0.0;         ///< Tr(C) of combined position covariance [m^2]; NaN if missing
    double collision_prob = 0.0;       ///< Provider Pc. Ground truth for evaluation ONLY, never a model input.
};

enum class OrbitalRegime : std::uint8_t { LEO, MEO, GEO };
enum class ManeuverPlane : std::uint8_t { AlongTrack, Radial, CrossTrack };
enum class Urgency : std::uint8_t { Routine, Priority, Immediate };
enum class RiskModel : std::uint8_t { LogisticRegression, ConservativeThreshold };

/// Why the risk classifier chose the model it used for a given CDM.
enum class ModelSelection : std::uint8_t {
    InDomain,               ///< Logistic model inside its training envelope
    CovarianceMissing,      ///< cov_trace absent / non-finite / non-positive
    CovarianceImplausible,  ///< sigma_eff beyond physical plausibility (dilution region)
    VelocityOutOfEnvelope,  ///< v_rel outside the training data range
    RegimeOutOfDomain       ///< Host not in LEO; model was trained on LEO data only
};

/// Which rule fired inside the conservative fallback strategy.
enum class ConservativeRule : std::uint8_t { None, PcEstimate, ScreeningVolume, GeometricOnly };

/// Lifecycle of an advisory inside the mission ledger.
enum class AdvisoryStatus : std::uint8_t {
    Staged,          ///< Handed to OEM flight bus, awaiting acknowledgement
    Committed,       ///< Acknowledged; delta-v reserved from budget
    RolledBack,      ///< External abort; spacecraft state restored from memento
    RejectedDvCap,   ///< Required delta-v exceeds regime cap; escalate to ground
    RejectedBudget   ///< Mission delta-v budget insufficient; escalate to ground
};

/// Unit vector in the Radial / Transverse (along-track) / Normal frame.
struct VectorRTN {
    double r = 0.0;
    double t = 0.0;
    double n = 0.0;
};

/// Explainable output of a risk strategy.
struct RiskAssessment {
    double score = 0.0;                    ///< Strategy score in [0, 1]
    bool actionable = false;               ///< Score crossed the strategy decision line
    RiskModel model = RiskModel::LogisticRegression;
    ModelSelection selection = ModelSelection::InDomain;
    ConservativeRule rule = ConservativeRule::None;
    std::array<double, 4> features{};      ///< Normalised f_d, f_v, f_t, f_sigma
    std::array<double, 4> contributions{}; ///< w_i * f_i (logistic model only)
    double logit = 0.0;                    ///< Logistic log-odds (logistic model only)
    double pc_estimate = 0.0;              ///< Isotropic Pc estimate (conservative model only)
};

/// A CDM after scoring, as stored in the priority heap.
struct ScoredThreat {
    ConjunctionTelemetry telemetry{};
    RiskAssessment risk{};
    double regime_risk = 0.0;  ///< Regime-specific criticality in [0, 1]
    double priority = 0.0;     ///< Composite ordering key in [0, 1]
    std::uint64_t sequence = 0;
};

/// Platform-agnostic evasion advisory handed to the OEM flight control unit.
/// OCAS never commands thrusters; the FCU maps this onto its own propulsion.
struct EvasionDirective {
    std::uint32_t alert_id = 0;
    OrbitalRegime regime = OrbitalRegime::LEO;
    ManeuverPlane plane = ManeuverPlane::AlongTrack;
    Urgency urgency = Urgency::Routine;
    double delta_v_mps = 0.0;              ///< Recommended impulse magnitude [m/s]
    VectorRTN direction{};                 ///< Unit direction in RTN frame
    double execute_no_later_than_s = 0.0;  ///< Burn time, seconds after CDM epoch
    double lead_time_s = 0.0;              ///< Burn-to-TCA interval [s]
    double target_separation_m = 0.0;      ///< Separation the advisory aims for [m]
    double predicted_separation_m = 0.0;   ///< Miss distance after maneuver, quadrature model [m]
    bool within_regime_dv_cap = true;
};

/// One advisory as recorded in the mission ledger.
struct LedgerRecord {
    std::uint64_t cycle = 0;
    EvasionDirective directive{};
    double risk_score = 0.0;
    double priority = 0.0;
    RiskModel model = RiskModel::LogisticRegression;
    AdvisoryStatus status = AdvisoryStatus::Staged;
};

/// Status flag bits for SpacecraftState::status_flags.
namespace status {
constexpr std::uint32_t kNominal = 1u << 0;
constexpr std::uint32_t kAdvisoryPending = 1u << 1;
constexpr std::uint32_t kRollback = 1u << 2;
}  // namespace status

/// Host spacecraft state tracked by the Memento originator.
struct SpacecraftState {
    double altitude_km = 0.0;
    double dv_budget_mps = 0.0;        ///< Remaining collision-avoidance delta-v allocation
    double dv_reserved_mps = 0.0;      ///< Delta-v reserved by staged/committed advisories
    std::uint32_t advisories_reserved = 0;
    std::uint32_t status_flags = status::kNominal;
};

static_assert(std::is_trivially_copyable_v<ConjunctionTelemetry>);
static_assert(std::is_trivially_copyable_v<ScoredThreat>);
static_assert(std::is_trivially_copyable_v<EvasionDirective>);
static_assert(std::is_trivially_copyable_v<SpacecraftState>);
static_assert(std::is_trivially_copyable_v<LedgerRecord>);

/// Above this effective 1-sigma radius (sqrt of Tr C) a covariance carries no
/// usable information for conjunction assessment (the ESA set contains traces
/// up to 2.4e16 m^2, i.e. sigma ~ 1.5e8 m). Shared by the risk classifier and
/// the evasion sizing so both treat the same covariances as unusable.
inline constexpr double kMaxPlausibleSigmaM = 1.0e6;

/// True when cov_trace_m2 is finite, positive and below the plausibility limit.
[[nodiscard]] bool covariance_usable(double cov_trace_m2) noexcept;

const char* to_string(OrbitalRegime v) noexcept;
const char* to_string(ManeuverPlane v) noexcept;
const char* to_string(Urgency v) noexcept;
const char* to_string(RiskModel v) noexcept;
const char* to_string(ModelSelection v) noexcept;
const char* to_string(ConservativeRule v) noexcept;
const char* to_string(AdvisoryStatus v) noexcept;

}  // namespace ocas

#endif  // OCAS_CORE_TYPES_HPP
