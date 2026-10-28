#ifndef OCAS_RISK_RISK_STRATEGY_HPP
#define OCAS_RISK_RISK_STRATEGY_HPP

#include "core/types.hpp"
#include "ml/neural.hpp"
#include "ml/sequence_store.hpp"

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

// Rule-based fallback, biased toward alerting. An unusable covariance is ignored and the
// pass is screened on geometry alone, because a diluted covariance drives Pc toward zero
// exactly when knowledge is worst.
class ConservativeThresholdStrategy final : public IRiskStrategy {
public:
    static constexpr double kActionScore = 0.6;
    static constexpr double kHardBodyRadiusM = 20.0;
    static constexpr double kPcActionThreshold = 1.0e-4;
    static constexpr double kScreeningRadiusM = 1000.0;
    static constexpr double kGeometricRadiusM = 5000.0;

    [[nodiscard]] RiskAssessment assess(const ConjunctionTelemetry& t) const noexcept override;
    [[nodiscard]] RiskModel id() const noexcept override { return RiskModel::ConservativeThreshold; }

    // Isotropic estimate, sigma^2 = TrC/3: Pc ~= R^2/(2 sigma^2) * exp(-d^2/(2 sigma^2)), clamped to 1.
    [[nodiscard]] static double estimate_pc(double miss_m, double cov_trace_m2, double hard_body_radius_m) noexcept;
};

// Advances the LSTM state of the CDM's event and forecasts whether the event's final CDM
// will be high risk. CDMs of one event must arrive in time order.
class LstmStrategy final : public IRiskStrategy {
public:
    static constexpr std::size_t kEventCapacity = 2048;

    explicit LstmStrategy(const ml::LstmParams& p) noexcept : p_(p) {}
    [[nodiscard]] RiskAssessment assess(const ConjunctionTelemetry& t) const noexcept override;
    [[nodiscard]] RiskModel id() const noexcept override { return RiskModel::Lstm; }
    [[nodiscard]] std::size_t tracked_events() const noexcept { return store_.size(); }

private:
    ml::LstmParams p_;
    mutable ml::SequenceStateStore<kEventCapacity> store_;
};

// Strategy context: picks the LSTM inside its training envelope, else the fallback.
class RiskClassifier {
public:
    static constexpr double kMaxSpeedKmS = 17.5;  // training max was 17.14 km/s

    RiskClassifier(const ml::LstmParams& lstm, bool host_in_leo) noexcept : lstm_(lstm), host_in_leo_(host_in_leo) {}

    [[nodiscard]] ModelSelection select(const ConjunctionTelemetry& t) const noexcept;
    [[nodiscard]] RiskAssessment assess(const ConjunctionTelemetry& t) const noexcept;

    [[nodiscard]] const LstmStrategy& lstm() const noexcept { return lstm_; }
    [[nodiscard]] const ConservativeThresholdStrategy& conservative() const noexcept { return conservative_; }

private:
    LstmStrategy lstm_;
    ConservativeThresholdStrategy conservative_;
    bool host_in_leo_;
};

}  // namespace ocas

#endif
