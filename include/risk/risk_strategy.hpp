// ============================================================================
// OCAS Module 2: Explainable Risk Classifier & Strategy Engine
// ============================================================================
// Strategy pattern:
//   IRiskStrategy                  abstract interface, one virtual assess()
//   LogisticRegressionStrategy     trained 4-feature logistic model, O(1)
//   ConservativeThresholdStrategy  rule-based fallback, O(1)
//   RiskClassifier                 context: picks a strategy per CDM and
//                                  records *why* (ModelSelection)
//
// Feature engineering is identical to tools/train_lr_model.cpp:
//   f_d     = 1 - d / (d + ref_dist)            closer       -> higher
//   f_v     = v / (v + ref_vel)                 faster       -> higher
//   f_t     = exp(-t / ref_tau)                 sooner       -> higher
//   f_sigma = s / (s + ref_sigma), s = sqrt(Tr C)  more uncertain -> higher
//   z = w0 + sum(w_i f_i),  score = sigmoid(z)
//
// The logistic model was trained with a 226x positive-class weight, so its
// score is a ranking score, NOT a calibrated probability. The decision line
// is the training-time F1-optimal threshold stored with the weights.
// ============================================================================
#ifndef OCAS_RISK_RISK_STRATEGY_HPP
#define OCAS_RISK_RISK_STRATEGY_HPP

#include "core/types.hpp"

#include <string>

namespace ocas {

class IRiskStrategy {
public:
    virtual ~IRiskStrategy() = default;
    [[nodiscard]] virtual RiskAssessment assess(const ConjunctionTelemetry& t) const noexcept = 0;
    [[nodiscard]] virtual RiskModel id() const noexcept = 0;

protected:
    IRiskStrategy() = default;
    IRiskStrategy(const IRiskStrategy&) = default;
    IRiskStrategy& operator=(const IRiskStrategy&) = default;
};

/// Weights and normalisation constants. Defaults equal data/trained_weights.txt.
struct LogisticModelParams {
    double w_bias = -0.1221970958;
    double w_dist = 8.9519815856;
    double w_vel = -2.1527299613;
    double w_tca = -4.5139313277;
    double w_cov = -0.3941279100;
    double ref_dist_m = 1000.0;
    double ref_vel_km_s = 12.26;
    double ref_tau_s = 285689.0;
    double ref_sigma_m = 4256.0;
    double decision_threshold = 0.99;
};

/// Parse a weights file written by tools/train_lr_model. Init-time only.
/// Unknown keys are ignored; missing keys keep their defaults.
bool load_logistic_params(const std::string& path, LogisticModelParams& out, std::string& error);

class LogisticRegressionStrategy final : public IRiskStrategy {
public:
    explicit LogisticRegressionStrategy(const LogisticModelParams& p = {}) noexcept : p_(p) {}
    [[nodiscard]] RiskAssessment assess(const ConjunctionTelemetry& t) const noexcept override;
    [[nodiscard]] RiskModel id() const noexcept override { return RiskModel::LogisticRegression; }
    [[nodiscard]] const LogisticModelParams& params() const noexcept { return p_; }

private:
    LogisticModelParams p_;
};

struct ConservativeParams {
    double hard_body_radius_m = 20.0;        ///< Combined hard-body radius for Pc estimate
    double pc_action_threshold = 1.0e-4;     ///< Same line used to label training data
    double screening_radius_m = 1000.0;      ///< Act on any pass closer than this
    double geometric_radius_m = 5000.0;      ///< Wider screen when covariance is unusable (see covariance_usable)
};

/// Rule-based fallback. Biased toward alerting: when the covariance cannot be
/// trusted it ignores it and screens on geometry alone, because a diluted
/// covariance drives estimated Pc toward zero exactly when we know least.
class ConservativeThresholdStrategy final : public IRiskStrategy {
public:
    /// Score assigned at the decision line, shared by every rule.
    static constexpr double kActionScore = 0.6;

    explicit ConservativeThresholdStrategy(const ConservativeParams& p = {}) noexcept : p_(p) {}
    [[nodiscard]] RiskAssessment assess(const ConjunctionTelemetry& t) const noexcept override;
    [[nodiscard]] RiskModel id() const noexcept override { return RiskModel::ConservativeThreshold; }
    [[nodiscard]] const ConservativeParams& params() const noexcept { return p_; }

    /// Isotropic short-encounter Pc approximation:
    ///   sigma^2 = Tr(C)/3 per axis,  Pc ~= R^2 / (2 sigma^2) * exp(-d^2 / (2 sigma^2)), clamped to 1.
    /// Valid for R << sigma; for small sigma it saturates at 1, which errs toward alerting.
    [[nodiscard]] static double estimate_pc(double miss_m, double cov_trace_m2, double hard_body_radius_m) noexcept;

private:
    ConservativeParams p_;
};

/// Bounds of the logistic model's training data (ESA Kelvins, LEO only).
struct ModelEnvelope {
    double min_speed_km_s = 0.0;
    double max_speed_km_s = 17.5;  ///< Training max was 17.14 km/s
};

/// Strategy context. Holds both strategies by value; dispatches through the
/// IRiskStrategy vtable after choosing one per CDM.
class RiskClassifier {
public:
    RiskClassifier(const LogisticModelParams& lp, const ConservativeParams& cp, bool host_in_training_regime,
                   const ModelEnvelope& env = {}) noexcept;

    [[nodiscard]] ModelSelection select(const ConjunctionTelemetry& t) const noexcept;
    [[nodiscard]] const IRiskStrategy& strategy_for(ModelSelection s) const noexcept;
    [[nodiscard]] RiskAssessment assess(const ConjunctionTelemetry& t) const noexcept;

    [[nodiscard]] const LogisticRegressionStrategy& logistic() const noexcept { return logistic_; }
    [[nodiscard]] const ConservativeThresholdStrategy& conservative() const noexcept { return conservative_; }

private:
    LogisticRegressionStrategy logistic_;
    ConservativeThresholdStrategy conservative_;
    ModelEnvelope envelope_;
    bool host_in_training_regime_;
};

}  // namespace ocas

#endif  // OCAS_RISK_RISK_STRATEGY_HPP
