// ============================================================================
// OCAS Module 2: Explainable Risk Classifier & Strategy Engine
// ============================================================================
// Strategy pattern:
//   IRiskStrategy                  abstract interface, one virtual assess()
//   LstmStrategy                   trained LSTM over each event's CDM history,
//                                  forecasts the event's final risk, O(1) per CDM
//   ConservativeThresholdStrategy  rule-based fallback, O(1)
//   RiskClassifier                 context: routes each CDM to the configured
//                                  primary model or the fallback, and records
//                                  *why* (ModelSelection)
// ============================================================================
#ifndef OCAS_RISK_RISK_STRATEGY_HPP
#define OCAS_RISK_RISK_STRATEGY_HPP

#include "core/types.hpp"
#include "ml/neural.hpp"
#include "ml/sequence_store.hpp"

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

/// Sequence model. Each call advances the LSTM state of that CDM's event
/// (alert_id) and returns the forecast that the event's FINAL CDM will be
/// high risk. Stateful: the per-event store is mutable, and CDMs of one event
/// must arrive in time order (they do: CDM updates are issued in time order).
class LstmStrategy final : public IRiskStrategy {
public:
    static constexpr std::size_t kEventCapacity = 2048;

    explicit LstmStrategy(const ml::LstmParams& p) noexcept : p_(p) {}
    [[nodiscard]] RiskAssessment assess(const ConjunctionTelemetry& t) const noexcept override;
    [[nodiscard]] RiskModel id() const noexcept override { return RiskModel::Lstm; }
    [[nodiscard]] const ml::LstmParams& params() const noexcept { return p_; }
    [[nodiscard]] std::size_t tracked_events() const noexcept { return store_.size(); }
    [[nodiscard]] std::uint64_t evictions() const noexcept { return store_.evictions(); }
    void reset() noexcept { store_.clear(); }

private:
    ml::LstmParams p_;
    mutable ml::SequenceStateStore<kEventCapacity> store_;
};

/// Everything the classifier needs to build its strategies.
struct RiskModelConfig {
    ml::LstmParams lstm{};
    ConservativeParams conservative{};
};

/// Bounds of the LSTM's training data (ESA Kelvins, LEO only).
struct ModelEnvelope {
    double min_speed_km_s = 0.0;
    double max_speed_km_s = 17.5;  ///< Training max was 17.14 km/s
};

/// Strategy context. Holds both strategies by value; dispatches through the
/// IRiskStrategy vtable after choosing one per CDM.
class RiskClassifier {
public:
    RiskClassifier(const RiskModelConfig& cfg, bool host_in_training_regime, const ModelEnvelope& env = {}) noexcept;
    RiskClassifier(const RiskClassifier&) = delete;  // primary_ points into *this
    RiskClassifier& operator=(const RiskClassifier&) = delete;

    [[nodiscard]] ModelSelection select(const ConjunctionTelemetry& t) const noexcept;
    [[nodiscard]] const IRiskStrategy& strategy_for(ModelSelection s) const noexcept;
    [[nodiscard]] RiskAssessment assess(const ConjunctionTelemetry& t) const noexcept;

    [[nodiscard]] const LstmStrategy& lstm() const noexcept { return lstm_; }
    [[nodiscard]] const ConservativeThresholdStrategy& conservative() const noexcept { return conservative_; }
    [[nodiscard]] const IRiskStrategy& primary() const noexcept { return *primary_; }

private:
    LstmStrategy lstm_;
    ConservativeThresholdStrategy conservative_;
    const IRiskStrategy* primary_;
    ModelEnvelope envelope_;
    bool host_in_training_regime_;
};

}  // namespace ocas

#endif  // OCAS_RISK_RISK_STRATEGY_HPP
