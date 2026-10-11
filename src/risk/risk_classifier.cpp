// OCAS Module 2 implementation: LSTM strategy, conservative strategy and
// strategy selection.
#include "risk/risk_strategy.hpp"

#include <algorithm>
#include <cmath>

namespace ocas {

// ---------------------------------------------------------------------------
// LstmStrategy
// ---------------------------------------------------------------------------
RiskAssessment LstmStrategy::assess(const ConjunctionTelemetry& t) const noexcept {
    RiskAssessment a{};
    a.model = RiskModel::Lstm;
    a.features = ml::standardize(ml::raw_features(t), p_.scaler);
    ml::LstmState& s = store_.acquire(t.alert_id);
    a.logit = ml::lstm_step(p_, a.features, s);
    a.score = ml::sigmoid(a.logit);
    a.actionable = a.score >= p_.threshold;
    return a;
}

// ---------------------------------------------------------------------------
// ConservativeThresholdStrategy
// ---------------------------------------------------------------------------
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
        // Geometry-only screen. Score is continuous and equals kActionScore
        // exactly at the screening boundary.
        const double r = p_.geometric_radius_m;
        a.rule = ConservativeRule::GeometricOnly;
        a.actionable = d < r;
        a.score = a.actionable ? 1.0 - (1.0 - kActionScore) * (d / r) : kActionScore * r / d;
        return a;
    }

    // Map log10(Pc) from [-10, 0] onto [0, 1]; Pc = 1e-4 lands on kActionScore.
    const double pc = estimate_pc(d, t.cov_trace_m2, p_.hard_body_radius_m);
    a.pc_estimate = pc;
    a.score = std::clamp((std::log10(std::max(pc, 1.0e-10)) + 10.0) / 10.0, 0.0, 1.0);
    a.rule = ConservativeRule::PcEstimate;
    a.actionable = pc >= p_.pc_action_threshold;
    if (d < p_.screening_radius_m) {
        a.rule = ConservativeRule::ScreeningVolume;
        a.actionable = true;
        a.score = std::max(a.score, kActionScore);
    }
    return a;
}

// ---------------------------------------------------------------------------
// RiskClassifier (strategy context)
// ---------------------------------------------------------------------------
RiskClassifier::RiskClassifier(const RiskModelConfig& cfg, bool host_in_training_regime,
                               const ModelEnvelope& env) noexcept
    : lstm_(cfg.lstm),
      conservative_(cfg.conservative),
      primary_(&lstm_),
      envelope_(env),
      host_in_training_regime_(host_in_training_regime) {
}

ModelSelection RiskClassifier::select(const ConjunctionTelemetry& t) const noexcept {
    if (!host_in_training_regime_) {
        return ModelSelection::RegimeOutOfDomain;
    }
    if (!std::isfinite(t.cov_trace_m2) || t.cov_trace_m2 <= 0.0) {
        return ModelSelection::CovarianceMissing;
    }
    if (!covariance_usable(t.cov_trace_m2)) {
        return ModelSelection::CovarianceImplausible;
    }
    if (t.relative_speed_km_s < envelope_.min_speed_km_s || t.relative_speed_km_s > envelope_.max_speed_km_s) {
        return ModelSelection::VelocityOutOfEnvelope;
    }
    return ModelSelection::InDomain;
}

const IRiskStrategy& RiskClassifier::strategy_for(ModelSelection s) const noexcept {
    if (s == ModelSelection::InDomain) {
        return *primary_;
    }
    return conservative_;
}

RiskAssessment RiskClassifier::assess(const ConjunctionTelemetry& t) const noexcept {
    const ModelSelection sel = select(t);
    RiskAssessment a = strategy_for(sel).assess(t);  // virtual dispatch
    a.selection = sel;
    return a;
}

}  // namespace ocas
