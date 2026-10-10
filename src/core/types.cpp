// OCAS core enum-to-string helpers. Return static literals; never allocate.
#include "core/types.hpp"

#include <cmath>

namespace ocas {

bool covariance_usable(double cov_trace_m2) noexcept {
    return std::isfinite(cov_trace_m2) && cov_trace_m2 > 0.0 && std::sqrt(cov_trace_m2) <= kMaxPlausibleSigmaM;
}

const char* to_string(OrbitalRegime v) noexcept {
    switch (v) {
        case OrbitalRegime::LEO: return "LEO";
        case OrbitalRegime::MEO: return "MEO";
        case OrbitalRegime::GEO: return "GEO";
    }
    return "?";
}

const char* to_string(ManeuverPlane v) noexcept {
    switch (v) {
        case ManeuverPlane::AlongTrack: return "ALONG_TRACK";
        case ManeuverPlane::Radial:     return "RADIAL";
        case ManeuverPlane::CrossTrack: return "CROSS_TRACK";
    }
    return "?";
}

const char* to_string(Urgency v) noexcept {
    switch (v) {
        case Urgency::Routine:   return "ROUTINE";
        case Urgency::Priority:  return "PRIORITY";
        case Urgency::Immediate: return "IMMEDIATE";
    }
    return "?";
}

const char* to_string(RiskModel v) noexcept {
    switch (v) {
        case RiskModel::LogisticRegression:    return "LOGISTIC";
        case RiskModel::ConservativeThreshold: return "CONSERVATIVE";
    }
    return "?";
}

const char* to_string(ModelSelection v) noexcept {
    switch (v) {
        case ModelSelection::InDomain:              return "IN_DOMAIN";
        case ModelSelection::CovarianceMissing:     return "COV_MISSING";
        case ModelSelection::CovarianceImplausible: return "COV_IMPLAUSIBLE";
        case ModelSelection::VelocityOutOfEnvelope: return "VEL_OUT_OF_ENVELOPE";
        case ModelSelection::RegimeOutOfDomain:     return "REGIME_OUT_OF_DOMAIN";
    }
    return "?";
}

const char* to_string(ConservativeRule v) noexcept {
    switch (v) {
        case ConservativeRule::None:            return "NONE";
        case ConservativeRule::PcEstimate:      return "PC_ESTIMATE";
        case ConservativeRule::ScreeningVolume: return "SCREENING_VOLUME";
        case ConservativeRule::GeometricOnly:   return "GEOMETRIC_ONLY";
    }
    return "?";
}

const char* to_string(AdvisoryStatus v) noexcept {
    switch (v) {
        case AdvisoryStatus::Staged:         return "STAGED";
        case AdvisoryStatus::Committed:      return "COMMITTED";
        case AdvisoryStatus::RolledBack:     return "ROLLED_BACK";
        case AdvisoryStatus::RejectedDvCap:  return "REJECTED_DV_CAP";
        case AdvisoryStatus::RejectedBudget: return "REJECTED_BUDGET";
    }
    return "?";
}

}  // namespace ocas

#include "core/observer.hpp"

namespace ocas {

const char* to_string(InputRejectReason r) noexcept {
    switch (r) {
        case InputRejectReason::NonFinite:        return "NON_FINITE";
        case InputRejectReason::NegativeDistance: return "NEGATIVE_DISTANCE";
        case InputRejectReason::PastTca:          return "PAST_TCA";
    }
    return "?";
}

}  // namespace ocas
