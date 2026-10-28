#include "risk.hpp"

#include <algorithm>
#include <cmath>

namespace ocas {

double estimate_pc(double miss_m, double cov_trace_m2, double hard_body_radius_m) noexcept {
    const double sigma2 = cov_trace_m2 / 3.0;
    const double pc = (hard_body_radius_m * hard_body_radius_m) / (2.0 * sigma2) *
                      std::exp(-(miss_m * miss_m) / (2.0 * sigma2));
    return std::min(1.0, pc);
}

RiskAssessment conservative_assess(const ConjunctionTelemetry& t) noexcept {
    const double d = std::max(0.0, t.miss_distance_m);
    RiskAssessment a{};
    a.model = RiskModel::ConservativeThreshold;

    if (!covariance_usable(t.cov_trace_m2)) {
        // Continuous score, equal to kActionScore at the screening boundary.
        const double r = kGeometricRadiusM;
        a.rule = ConservativeRule::GeometricOnly;
        a.actionable = d < r;
        a.score = a.actionable ? 1.0 - (1.0 - kActionScore) * (d / r) : kActionScore * r / d;
        return a;
    }

    // log10(Pc) in [-10, 0] maps onto [0, 1]; Pc = 1e-4 lands on kActionScore.
    const double pc = estimate_pc(d, t.cov_trace_m2, kHardBodyRadiusM);
    a.score = std::clamp((std::log10(std::max(pc, 1.0e-10)) + 10.0) / 10.0, 0.0, 1.0);
    a.rule = ConservativeRule::PcEstimate;
    a.actionable = pc >= kPcActionThreshold;
    if (d < kScreeningRadiusM) {
        a.rule = ConservativeRule::ScreeningVolume;
        a.actionable = true;
        a.score = std::max(a.score, kActionScore);
    }
    return a;
}

ModelSelection select_model(const ConjunctionTelemetry& t, bool host_in_leo) noexcept {
    if (!host_in_leo) return ModelSelection::RegimeOutOfDomain;
    if (!std::isfinite(t.cov_trace_m2) || t.cov_trace_m2 <= 0.0) return ModelSelection::CovarianceMissing;
    if (!covariance_usable(t.cov_trace_m2)) return ModelSelection::CovarianceImplausible;
    if (t.relative_speed_km_s < 0.0 || t.relative_speed_km_s > kMaxSpeedKmS) {
        return ModelSelection::VelocityOutOfEnvelope;
    }
    return ModelSelection::InDomain;
}

ml::LstmState& LstmModel::state_for(std::uint32_t alert_id) noexcept {
    std::uint64_t z = alert_id + 0x9E3779B97F4A7C15ull;  // splitmix64 finaliser
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    const std::size_t home = static_cast<std::size_t>(z ^ (z >> 31)) & (kEvents - 1);
    std::size_t victim = home;
    for (std::size_t k = 0; k < kProbe; ++k) {
        const std::size_t i = (home + k) & (kEvents - 1);
        Slot& s = slots_[i];
        if (s.used && s.key == alert_id) {
            s.last_touch = ++clock_;
            return s.state;
        }
        if (!s.used) {
            victim = i;
            break;
        }
        if (s.last_touch < slots_[victim].last_touch) victim = i;
    }
    Slot& s = slots_[victim];
    if (!s.used) ++tracked_;
    s = Slot{{}, ++clock_, alert_id, true};
    return s.state;
}

RiskAssessment LstmModel::assess(const ConjunctionTelemetry& t) noexcept {
    RiskAssessment a{};
    a.model = RiskModel::Lstm;
    a.logit = ml::lstm_step(params, ml::standardize(ml::raw_features(t), params.scaler), state_for(t.alert_id));
    a.score = ml::sigmoid(a.logit);
    a.actionable = a.score >= params.threshold;
    return a;
}

RiskAssessment assess_risk(LstmModel& lstm, const ConjunctionTelemetry& t, bool host_in_leo) noexcept {
    const ModelSelection sel = select_model(t, host_in_leo);
    RiskAssessment a = sel == ModelSelection::InDomain ? lstm.assess(t) : conservative_assess(t);
    a.selection = sel;
    return a;
}

}  // namespace ocas
