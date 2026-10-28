#include "risk/risk_strategy.hpp"

#include <algorithm>
#include <cmath>

namespace ocas {

RiskAssessment LstmStrategy::assess(const ConjunctionTelemetry& t) const noexcept {
    RiskAssessment a{};
    a.model = RiskModel::Lstm;
    a.logit = ml::lstm_step(p_, ml::standardize(ml::raw_features(t), p_.scaler), store_.acquire(t.alert_id));
    a.score = ml::sigmoid(a.logit);
    a.actionable = a.score >= p_.threshold;
    return a;
}

double ConservativeThresholdStrategy::estimate_pc(double miss_m, double cov_trace_m2,
                                                  double hard_body_radius_m) noexcept {
    const double sigma2 = cov_trace_m2 / 3.0;
    const double pc = (hard_body_radius_m * hard_body_radius_m) / (2.0 * sigma2) *
                      std::exp(-(miss_m * miss_m) / (2.0 * sigma2));
    return std::min(1.0, pc);
}

RiskAssessment ConservativeThresholdStrategy::assess(const ConjunctionTelemetry& t) const noexcept {
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

ModelSelection RiskClassifier::select(const ConjunctionTelemetry& t) const noexcept {
    if (!host_in_leo_) return ModelSelection::RegimeOutOfDomain;
    if (!std::isfinite(t.cov_trace_m2) || t.cov_trace_m2 <= 0.0) return ModelSelection::CovarianceMissing;
    if (!covariance_usable(t.cov_trace_m2)) return ModelSelection::CovarianceImplausible;
    if (t.relative_speed_km_s < 0.0 || t.relative_speed_km_s > kMaxSpeedKmS) {
        return ModelSelection::VelocityOutOfEnvelope;
    }
    return ModelSelection::InDomain;
}

RiskAssessment RiskClassifier::assess(const ConjunctionTelemetry& t) const noexcept {
    const ModelSelection sel = select(t);
    const IRiskStrategy& s = sel == ModelSelection::InDomain ? static_cast<const IRiskStrategy&>(lstm_) : conservative_;
    RiskAssessment a = s.assess(t);
    a.selection = sel;
    return a;
}

}  // namespace ocas
