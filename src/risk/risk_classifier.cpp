// OCAS Module 2 implementation: logistic strategy, conservative strategy,
// strategy selection, and the init-time weights loader.
#include "risk/risk_strategy.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ocas {

// ---------------------------------------------------------------------------
// LogisticRegressionStrategy
// ---------------------------------------------------------------------------
RiskAssessment LogisticRegressionStrategy::assess(const ConjunctionTelemetry& t) const noexcept {
    const double d = std::max(0.0, t.miss_distance_m);
    const double v = std::max(0.0, t.relative_speed_km_s);
    const double tca = std::max(0.0, t.time_to_tca_sec);
    const double sigma = std::sqrt(std::max(0.0, t.cov_trace_m2));

    RiskAssessment a{};
    a.model = RiskModel::LogisticRegression;
    a.features = {1.0 - d / (d + p_.ref_dist_m), v / (v + p_.ref_vel_km_s), std::exp(-tca / p_.ref_tau_s),
                  sigma / (sigma + p_.ref_sigma_m)};
    a.contributions = {p_.w_dist * a.features[0], p_.w_vel * a.features[1], p_.w_tca * a.features[2],
                       p_.w_cov * a.features[3]};
    a.logit = std::clamp(p_.w_bias + a.contributions[0] + a.contributions[1] + a.contributions[2] +
                             a.contributions[3],
                         -30.0, 30.0);
    a.score = 1.0 / (1.0 + std::exp(-a.logit));
    a.actionable = a.score >= p_.decision_threshold;
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
RiskClassifier::RiskClassifier(const LogisticModelParams& lp, const ConservativeParams& cp,
                               bool host_in_training_regime, const ModelEnvelope& env) noexcept
    : logistic_(lp), conservative_(cp), envelope_(env), host_in_training_regime_(host_in_training_regime) {}

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
        return logistic_;
    }
    return conservative_;
}

RiskAssessment RiskClassifier::assess(const ConjunctionTelemetry& t) const noexcept {
    const ModelSelection sel = select(t);
    RiskAssessment a = strategy_for(sel).assess(t);  // virtual dispatch
    a.selection = sel;
    return a;
}

// ---------------------------------------------------------------------------
// Weights loader (init-time; allocates freely)
// ---------------------------------------------------------------------------
bool load_logistic_params(const std::string& path, LogisticModelParams& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    LogisticModelParams p = out;
    std::string line;
    int keys = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream ss(line);
        std::string key;
        double value = 0.0;
        if (!(ss >> key >> value) || !std::isfinite(value)) {
            error = "malformed line in " + path + ": " + line;
            return false;
        }
        double* slot = nullptr;
        if (key == "w_bias") slot = &p.w_bias;
        else if (key == "w_dist") slot = &p.w_dist;
        else if (key == "w_vel") slot = &p.w_vel;
        else if (key == "w_tca") slot = &p.w_tca;
        else if (key == "w_cov") slot = &p.w_cov;
        else if (key == "ref_dist_m") slot = &p.ref_dist_m;
        else if (key == "ref_vel_kms") slot = &p.ref_vel_km_s;
        else if (key == "ref_tau_s") slot = &p.ref_tau_s;
        else if (key == "ref_sigma_m") slot = &p.ref_sigma_m;
        else if (key == "decision_threshold") slot = &p.decision_threshold;
        if (slot != nullptr) {
            *slot = value;
            ++keys;
        }
    }
    if (keys < 5) {
        error = "too few recognised keys in " + path;
        return false;
    }
    if (p.ref_dist_m <= 0.0 || p.ref_vel_km_s <= 0.0 || p.ref_tau_s <= 0.0 || p.ref_sigma_m <= 0.0 ||
        p.decision_threshold <= 0.0 || p.decision_threshold >= 1.0) {
        error = "non-physical normalisation constant or threshold in " + path;
        return false;
    }
    out = p;
    return true;
}

}  // namespace ocas
