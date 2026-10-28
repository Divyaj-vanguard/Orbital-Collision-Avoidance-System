#ifndef OCAS_RISK_HPP
#define OCAS_RISK_HPP

#include "neural.hpp"
#include "types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ocas {

inline constexpr double kActionScore = 0.6;
inline constexpr double kHardBodyRadiusM = 20.0;
inline constexpr double kPcActionThreshold = 1.0e-4;
inline constexpr double kScreeningRadiusM = 1000.0;
inline constexpr double kGeometricRadiusM = 5000.0;
inline constexpr double kMaxSpeedKmS = 17.5;  // LSTM training max was 17.14 km/s

// Isotropic estimate, sigma^2 = TrC/3: Pc ~= R^2/(2 sigma^2) * exp(-d^2/(2 sigma^2)), clamped to 1.
double estimate_pc(double miss_m, double cov_trace_m2, double hard_body_radius_m) noexcept;

// Rule-based fallback, biased toward alerting. An unusable covariance is ignored and the
// pass is screened on geometry alone, because a diluted covariance drives Pc toward zero
// exactly when knowledge is worst.
RiskAssessment conservative_assess(const ConjunctionTelemetry& t) noexcept;

// Why the LSTM may or may not score this CDM (it was trained on LEO data only).
ModelSelection select_model(const ConjunctionTelemetry& t, bool host_in_leo) noexcept;

// LSTM over each event's CDM history; forecasts whether the event's final CDM is high risk.
// Per-event state lives in a fixed open-addressing table (no heap): at most kProbe slots
// are probed, and a full window evicts its least recently used event.
class LstmModel {
public:
    static constexpr std::size_t kEvents = 2048;
    static constexpr std::size_t kProbe = 16;

    LstmModel() = default;
    explicit LstmModel(const ml::LstmParams& p) : params(p) {}

    RiskAssessment assess(const ConjunctionTelemetry& t) noexcept;
    [[nodiscard]] std::size_t tracked_events() const noexcept { return tracked_; }

    ml::LstmParams params{};

private:
    struct Slot {
        ml::LstmState state{};
        std::uint64_t last_touch = 0;
        std::uint32_t key = 0;
        bool used = false;
    };
    ml::LstmState& state_for(std::uint32_t alert_id) noexcept;

    std::array<Slot, kEvents> slots_{};
    std::size_t tracked_ = 0;
    std::uint64_t clock_ = 0;
};

// LSTM when the CDM is in its training envelope, otherwise the conservative rule.
RiskAssessment assess_risk(LstmModel& lstm, const ConjunctionTelemetry& t, bool host_in_leo) noexcept;

}  // namespace ocas

#endif
